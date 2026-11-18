#!/bin/bash
# Build the FreeButton script engine RUNNER as a WebAssembly module: the web
# IDE's virtual device runs the user's script in the page with it
# (docs/EMULATOR.md, web/src/js/emulator/).
#
# Unlike build_wasm.sh (compile only, hardware APIs stubbed), this one links
# the firmware itself, as far as it is not hardware: the script engine with
# its JavaScript glue and bindings (src/scripting), ConfigManager and the
# components (display items, keys, lists, images, clocks, topics), the MQTT
# brokers with Home Assistant discovery, the hardware scan with the key and
# LED drivers, the sensors, and the firmware's UI (src/ui: boxes, key
# displays, text, styles, the clock, the TrueType fonts, media images) on
# LVGL itself, with the firmware's src/lv_conf.h. The list is FIRMWARE_CXX /
# FIRMWARE_C below. Underneath are the runner and shims of tools/js/emulator/
# (FreeRTOS, esp-mqtt, I2C, the display driver and the media store); what is
# left undefined is the outside world: wasm imports that
# web/src/js/emulator/wasm.ts implements.
#
# Output: web/public/assets/fb-emulator.wasm (a plain wasm module, no
# Emscripten JS loader: the loader is web/src/js/emulator/wasm.ts) and
# fb-emulator.stamp.txt (what it was built from; CI compares it with the
# sources: tools/js/wasm_stdlib_fingerprint.py --check).
#
# Requires emscripten (emcc). A DEV/RELEASE step like build_wasm.sh: the
# output is committed, so a fresh clone builds the web IDE without emcc.
# Re-run when the engine, the stdlib, the glue, a seam header or
# tools/js/emulator/ changes.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

MQ="$PROJECT_ROOT/lib/mquickjs"                 # engine (pinned submodule, READ-ONLY)
SRC="$PROJECT_ROOT/src"                         # firmware sources, compiled as they are
JS="$SRC/scripting/js"
EMU="$SCRIPT_DIR/emulator"                      # runner.c, device.cpp, *shim.cpp, seam_*.cpp, shim/
LIBS="$PROJECT_ROOT/.pio/libdeps/native"        # ArduinoJson, RapidJSON (PlatformIO fetches them)
LVGL="$PROJECT_ROOT/.pio/libdeps/esp32/lvgl"    # LVGL, the version platformio.ini pins
OUT="$PROJECT_ROOT/web/public/assets"           # committed output location
BUILD="$PROJECT_ROOT/.pio/emulator_wasm_build"  # scratch (keeps the submodule clean)

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc (Emscripten) not found on PATH. Install: https://emscripten.org/" >&2
    exit 1
fi

for lib in ArduinoJson/src/ArduinoJson.h RapidJSON/include/rapidjson/reader.h; do
    if [ ! -f "$LIBS/$lib" ]; then
        echo "Error: $LIBS/$lib not found. The firmware's JSON libraries come from PlatformIO:" >&2
        echo "  ~/.local/bin/pio pkg install -e native" >&2
        exit 1
    fi
done

if [ ! -f "$LVGL/lvgl.h" ] || [ ! -f "$LVGL/library.json" ]; then
    echo "Error: LVGL not found in $LVGL. PlatformIO fetches it (no build needed):" >&2
    echo "  ~/.local/bin/pio pkg install -e esp32" >&2
    exit 1
fi
LVGL_VERSION="$(sed -nE 's/.*"version": *"([0-9.]+)".*/\1/p' "$LVGL/library.json" | head -1)"
LVGL_PINNED="$(sed -nE 's/.*lvgl\/lvgl @ ([0-9.]+).*/\1/p' "$PROJECT_ROOT/platformio.ini" | head -1)"
if [ -z "$LVGL_VERSION" ] || [ "$LVGL_VERSION" != "$LVGL_PINNED" ]; then
    echo "Error: $LVGL is LVGL '$LVGL_VERSION', platformio.ini pins '$LVGL_PINNED'. Fetch the pinned one:" >&2
    echo "  ~/.local/bin/pio pkg install -e esp32" >&2
    exit 1
fi

echo "== FreeButton emulator wasm build =="
emcc --version | head -1
mkdir -p "$BUILD" "$OUT"

# Step 1: the 32-bit FreeButton stdlib + atom headers, exactly as build_wasm.sh
# makes them (the same table the firmware has: wasm32 is 32-bit like ESP32).
# (cd into BUILD so a repo-root 'VERSION' file cannot shadow libc's <version> header.)
echo "Step 1: generating FreeButton stdlib headers..."
cd "$BUILD"
gcc -O2 -Wall -D_GNU_SOURCE -I"$MQ" -c "$MQ/mquickjs_build.c" -o "$BUILD/mquickjs_build.host.o"
gcc -O2 -Wall -D_GNU_SOURCE -I"$MQ" -I"$SRC/scripting" -c "$JS/freebutton_stdlib.c" -o "$BUILD/freebutton_stdlib.host.o"
gcc "$BUILD/freebutton_stdlib.host.o" "$BUILD/mquickjs_build.host.o" -o "$BUILD/freebutton_stdlib"
"$BUILD/freebutton_stdlib" -m32     > "$BUILD/freebutton_stdlib.h"
"$BUILD/freebutton_stdlib" -a -m32  > "$BUILD/mquickjs_atom.h"

# Step 2: constants the firmware keeps where a C file cannot include them.
number_from() {  # <file> <sed pattern with one group>
    local value
    value="$(sed -nE "s/$2/\1/p" "$1" | head -1)"
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
        echo "Error: cannot read a constant from $1 (pattern: $2)" >&2
        exit 1
    fi
    echo "$value"
}
# ButtonManager::DEFAULT_UPDATE_INTERVAL: the period of the key scan.
BUTTON_INTERVAL="$(number_from "$SRC/drivers/ButtonManager.h" '.*DEFAULT_UPDATE_INTERVAL *= *([0-9]+);.*')"
# esp-mqtt's in-buffer: larger messages are dropped (mqtt/Broker.cpp).
MQTT_BUFFER="$(number_from "$PROJECT_ROOT/sdkconfig.esp32" '^CONFIG_MQTT_BUFFER_SIZE=([0-9]+)$')"
# loop()'s delay(): the period of MqttBinding::processPending / processTimers.
LOOP_DELAY="$(number_from "$SRC/main.cpp" '^    delay\(([0-9]+)\);$')"
# NeoPixelController::NUMBER_OF_LEDS: the LED strip (led_hw_set_color's range).
LED_COUNT="$(number_from "$SRC/drivers/NeoPixel.h" '.*NUMBER_OF_LEDS *= *([0-9]+);.*')"
# esp-mqtt's event queue: > 1 lets another task wake the MQTT task (mqtt/Broker.cpp).
MQTT_EVENT_QUEUE="$(number_from "$PROJECT_ROOT/sdkconfig.esp32" '^CONFIG_MQTT_EVENT_QUEUE_SIZE=([0-9]+)$')"
# lvgl_task()'s vTaskDelay: the period of the LVGL task.
LVGL_TASK="$(number_from "$SRC/main.cpp" '^        vTaskDelay\(pdMS_TO_TICKS\(([0-9]+)\)\);$')"
echo "Step 2: key scan ${BUTTON_INTERVAL} ms, MQTT in-buffer ${MQTT_BUFFER} B, loop delay ${LOOP_DELAY} ms"

# Step 3: compile and link.
# -UEMSCRIPTEN: the glue's EMSCRIPTEN branches are for the compile-only wasm
# (no load(), no js_bindings_reset); this build wants the firmware branches.
echo "Step 3: compiling to WebAssembly..."
rm -f "$BUILD"/*.d
DEFS=(-DESP_PLATFORM -D_GNU_SOURCE -UEMSCRIPTEN
      -DFB_BUTTON_UPDATE_INTERVAL_MS="$BUTTON_INTERVAL"
      -DFB_MQTT_BUFFER_SIZE="$MQTT_BUFFER"
      -DFB_LOOP_DELAY_MS="$LOOP_DELAY"
      -DFB_LED_COUNT="$LED_COUNT"
      -DFB_LVGL_TASK_MS="$LVGL_TASK"
      -DCONFIG_MQTT_EVENT_QUEUE_SIZE="$MQTT_EVENT_QUEUE")
# The shim comes first: it stands in for the headers of what is not there
# (Arduino, FreeRTOS, esp-mqtt, the display owner, the hardware scanner).
# LVGL reads "lv_conf.h" (LV_CONF_INCLUDE_SIMPLE): $EMU/lv_conf.h, which is
# the firmware's src/lv_conf.h plus the emulator's overrides.
DEFS+=(-DLV_CONF_INCLUDE_SIMPLE)
INC=(-I"$EMU/shim" -I"$EMU" -I"$BUILD" -I"$MQ" -I"$SRC" -I"$SRC/scripting" -I"$JS" -I"$PROJECT_ROOT/include"
     -I"$LIBS/ArduinoJson/src" -I"$LIBS/RapidJSON/include" -I"$LVGL")
# Three firmware headers are included by relative path from firmware sources
# (drivers/LvglDisplay.h ...), where -I cannot put the shim first. Included up
# front, the shim defines the firmware header's include guard.
SHIM_FIRST=(-include drivers/LvglDisplay.h -include drivers/DisplayManager.h -include drivers/BusAwareFileSystem.h)
# The two -Wno: warnings in firmware sources that the ESP32 toolchain does not give.
CXXFLAGS=(-Oz -flto -std=gnu++23 -fno-exceptions -fno-rtti -Wno-format -Wno-defaulted-function-deleted
          -Wno-vla-cxx-extension -Wno-keyword-macro "${SHIM_FIRST[@]}")

OBJS=()
cxx() {  # <source> <object name>
    em++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INC[@]}" -MD -MF "$BUILD/$2.d" -c "$1" -o "$BUILD/$2.o"
    OBJS+=("$BUILD/$2.o")
}
# The firmware, unchanged. (mqtt_binding.cpp, button_hardware.cpp and
# HomeAssistantDiscovery.cpp are compiled through the seam_*.cpp files that
# include them.)
FIRMWARE_CXX=(
    scripting/device_hardware.cpp scripting/sensor_binding.cpp scripting/displayitem_binding.cpp
    scripting/list_binding.cpp scripting/style_binding.cpp scripting/image_binding.cpp scripting/led_hardware.cpp
    scripting/system_binding.cpp scripting/file_hardware.cpp
    components/ConfigManager.cpp components/DisplayItem.cpp components/Button.cpp components/List.cpp
    components/ImageItem.cpp components/Clock.cpp components/TopicHandler.cpp components/ButtonEventPlan.cpp
    mqtt/Broker.cpp mqtt/Registry.cpp mqtt/Component.cpp mqtt/HaEntities.cpp mqtt/HaSlug.cpp
    drivers/PushButton.cpp drivers/ButtonManager.cpp drivers/ButtonLed.cpp drivers/NeoPixel.cpp
    drivers/Sensors.cpp drivers/HardwareScanner.cpp core/HardwareInitializer.cpp
    ui/PendingRender.cpp ui/MainPage.cpp ui/MediaImage.cpp json/FilteredReaderHandler.cpp
)
# (ui/fonts/fonts.c, ui/ui_clock.c and ui/ui_box.c are compiled through
# seam_fonts.c, seam_clock.c and seam_box.c, which include them.)
FIRMWARE_C=(ui/style_logic.c ui/text_spans.c ui/fonts/font_logic.c ui/fonts/ttf_check.c ui/media_image_check.c
            ui/ui_text.c ui/ui_button.c ui/ui_style.c)
for f in "${FIRMWARE_CXX[@]}"; do
    cxx "$SRC/$f" "fw_$(echo "${f%.cpp}" | tr '/' '_')"
done
for f in device records display mqtt_client_shim seam_mqtt seam_discovery seam_button; do
    cxx "$EMU/$f.cpp" "$f"
done

# The parser reports a syntax error with longjmp(). Without a JS loader that
# needs the wasm exception instructions (every browser since 2022, Node 18).
SJLJ=(-s SUPPORT_LONGJMP=wasm)
cc() {  # <source> <object name>
    emcc -Oz -flto "${SJLJ[@]}" "${DEFS[@]}" "${INC[@]}" -MD -MF "$BUILD/$2.d" -c "$1" -o "$BUILD/$2.o"
    OBJS+=("$BUILD/$2.o")
}
cc "$MQ/mquickjs.c" mquickjs
cc "$MQ/cutils.c"   cutils
cc "$MQ/dtoa.c"     dtoa
cc "$MQ/libm.c"     libm
cc "$EMU/runner.c"  runner
cc "$EMU/tz.c"      tz
cc "$EMU/media_emu.c"   media_emu
cc "$EMU/seam_fonts.c"  seam_fonts
cc "$EMU/seam_clock.c"  seam_clock
cc "$EMU/seam_box.c"    seam_box
for f in "${FIRMWARE_C[@]}"; do
    cc "$SRC/$f" "fw_$(echo "${f%.c}" | tr '/' '_')"
done

# LVGL: every source of the library with the firmware's configuration, at the
# same optimisation level and LTO as the rest (the linker keeps what the
# firmware uses). 483 files, so the archive is kept between builds, keyed by
# the compiler, the flags, the LVGL version and both lv_conf.h.
# lv_tiny_ttf.c gets lv_malloc / lv_free renamed, as tools/scripts/render_build.py
# does for the firmware (stb_truetype's scratch comes from the heap: fonts.c).
LVGL_FLAGS=(-Oz -flto -DLV_CONF_INCLUDE_SIMPLE -I"$EMU" -I"$LVGL")
LVGL_KEY="$( { emcc --version | head -1; echo "${LVGL_FLAGS[*]}" "$LVGL_VERSION"; cat "$EMU/lv_conf.h" "$SRC/lv_conf.h"; } | shasum -a 256 | cut -c1-16)"
LVGL_LIB="$BUILD/lvgl-$LVGL_KEY/liblvgl.a"
if [ ! -f "$LVGL_LIB" ]; then
    echo "Step 3b: compiling LVGL $LVGL_VERSION (once per configuration)..."
    rm -rf "$BUILD"/lvgl-*
    mkdir -p "$BUILD/lvgl-$LVGL_KEY"
    (cd "$LVGL/src" && find . -name '*.c' | sort) > "$BUILD/lvgl-$LVGL_KEY/sources.txt"
    export LVGL LVGL_OBJ="$BUILD/lvgl-$LVGL_KEY" LVGL_FLAGS_STR="${LVGL_FLAGS[*]}"
    # shellcheck disable=SC2016
    xargs -P "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" -I{} sh -c '
        src="$1"; obj="$LVGL_OBJ/$(echo "${src#./}" | tr "/" "_" | sed "s/\.c$/.o/")"
        extra=""
        case "$src" in */lv_tiny_ttf.c) extra="-Dlv_malloc=fb_tiny_ttf_malloc -Dlv_free=fb_tiny_ttf_free";; esac
        # shellcheck disable=SC2086
        emcc $LVGL_FLAGS_STR $extra -c "$LVGL/src/$src" -o "$obj" || exit 255
    ' _ {} < "$BUILD/lvgl-$LVGL_KEY/sources.txt"
    emar rcs "$LVGL_LIB.tmp" "$BUILD/lvgl-$LVGL_KEY"/*.o
    mv "$LVGL_LIB.tmp" "$LVGL_LIB"
fi
OBJS+=("$LVGL_LIB")

# No JS loader (-o *.wasm, no entry point): the module's imports are the seam,
# its exports the fbe_* functions (export_name in the sources).
# ERROR_ON_UNDEFINED_SYMBOLS=0: the seam functions the host implements under
# their own name stay undefined here; check_imports below lists them, so a
# symbol that is undefined by mistake fails the build.
em++ -Oz -flto "${OBJS[@]}" \
    -o "$OUT/fb-emulator.wasm" \
    --no-entry \
    -fno-exceptions -fno-rtti "${SJLJ[@]}" \
    -s STANDALONE_WASM=1 \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s WARN_ON_UNDEFINED_SYMBOLS=0 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=2MB \
    -s MAXIMUM_MEMORY=64MB \
    -s STACK_SIZE=512KB \
    -s EXPORTED_FUNCTIONS='[]'

# Step 4: the imports must be exactly the documented seam.
python3 "$EMU/check_imports.py" "$OUT/fb-emulator.wasm" "$EMU/imports.txt"

# Step 5: what this wasm was built from. Every firmware and emulator file a
# compilation read (the compilers' dependency lists) goes into sources.txt;
# the stamp has a hash of each, so CI sees a stale wasm.
python3 "$EMU/list_sources.py" "$PROJECT_ROOT" "$BUILD" > "$EMU/sources.txt"
python3 "$SCRIPT_DIR/wasm_stdlib_fingerprint.py" --emulator-stamp "$BUILD" > "$OUT/fb-emulator.stamp.txt"
if ! grep -q "^$LVGL_VERSION  LVGL$" "$OUT/fb-emulator.stamp.txt"; then
    echo "Error: the stamp does not name LVGL $LVGL_VERSION" >&2
    exit 1
fi

echo ""
echo "✓ Built:"
ls -l "$OUT/fb-emulator.wasm" "$OUT/fb-emulator.stamp.txt"
echo "gzip: $(gzip -9 -c "$OUT/fb-emulator.wasm" | wc -c | tr -d ' ') bytes"
echo "Commit both files in web/public/assets."
