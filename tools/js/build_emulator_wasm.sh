#!/bin/bash
# Build the FreeButton script engine RUNNER as a WebAssembly module: the web
# IDE's virtual device runs the user's script in the page with it
# (docs/EMULATOR.md, web/src/js/emulator/).
#
# Unlike build_wasm.sh (compile only, hardware APIs stubbed), this one links
# the firmware's own JavaScript glue (src/scripting/mquickjs_stdlib.c with
# src/scripting/js/*.c), the fb tree builder (device_hardware.cpp), the MQTT
# script binding with its event ring (mqtt_binding.cpp, mqtt/Registry.cpp,
# mqtt/Component.cpp), the sensor binding (sensor_binding.cpp) and the key
# timing (drivers/PushButton.cpp), plus the runner and the seam of
# tools/js/emulator/. What is left undefined is the device state: wasm imports
# that web/src/js/emulator/wasm.ts implements.
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
EMU="$SCRIPT_DIR/emulator"                      # runner.c, seam*.cpp, fbh.h, shim/
OUT="$PROJECT_ROOT/web/public/assets"           # committed output location
BUILD="$PROJECT_ROOT/.pio/emulator_wasm_build"  # scratch (keeps the submodule clean)

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc (Emscripten) not found on PATH. Install: https://emscripten.org/" >&2
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
echo "Step 2: key scan ${BUTTON_INTERVAL} ms, MQTT in-buffer ${MQTT_BUFFER} B, loop delay ${LOOP_DELAY} ms"

# Step 3: compile and link.
# -UEMSCRIPTEN: the glue's EMSCRIPTEN branches are for the compile-only wasm
# (no load(), no js_bindings_reset); this build wants the firmware branches.
echo "Step 3: compiling to WebAssembly..."
DEFS=(-DESP_PLATFORM -D_GNU_SOURCE -UEMSCRIPTEN
      -DFB_BUTTON_UPDATE_INTERVAL_MS="$BUTTON_INTERVAL"
      -DFB_MQTT_BUFFER_SIZE="$MQTT_BUFFER"
      -DFB_LOOP_DELAY_MS="$LOOP_DELAY"
      -DFB_LED_COUNT="$LED_COUNT")
# The shim comes first: it stands in for the headers of what is not there
# (Arduino, FreeRTOS, esp-mqtt, LVGL, ConfigManager, the hardware scanner).
INC=(-I"$EMU/shim" -I"$EMU" -I"$BUILD" -I"$MQ" -I"$SRC" -I"$SRC/scripting" -I"$JS" -I"$PROJECT_ROOT/include")
# The two -Wno: warnings in firmware sources that the ESP32 toolchain does not give.
CXXFLAGS=(-Oz -flto -std=gnu++20 -fno-exceptions -fno-rtti -Wno-format -Wno-defaulted-function-deleted)

OBJS=()
cxx() {  # <source> <object name>
    em++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INC[@]}" -c "$1" -o "$BUILD/$2.o"
    OBJS+=("$BUILD/$2.o")
}
cxx "$SRC/scripting/device_hardware.cpp" device_hardware
cxx "$SRC/scripting/sensor_binding.cpp"  sensor_binding
cxx "$SRC/drivers/PushButton.cpp"        PushButton
cxx "$SRC/mqtt/Registry.cpp"             Registry
cxx "$SRC/mqtt/Component.cpp"            Component
cxx "$EMU/seam.cpp"                      seam
cxx "$EMU/seam_mqtt.cpp"                 seam_mqtt

# The parser reports a syntax error with longjmp(). Without a JS loader that
# needs the wasm exception instructions (every browser since 2022, Node 18).
SJLJ=(-s SUPPORT_LONGJMP=wasm)
cc() {  # <source> <object name>
    emcc -Oz -flto "${SJLJ[@]}" "${DEFS[@]}" "${INC[@]}" -c "$1" -o "$BUILD/$2.o"
    OBJS+=("$BUILD/$2.o")
}
cc "$MQ/mquickjs.c" mquickjs
cc "$MQ/cutils.c"   cutils
cc "$MQ/dtoa.c"     dtoa
cc "$MQ/libm.c"     libm
cc "$EMU/runner.c"  runner

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

# Step 5: what this wasm was built from.
python3 "$SCRIPT_DIR/wasm_stdlib_fingerprint.py" --emulator-stamp "$BUILD" > "$OUT/fb-emulator.stamp.txt"

echo ""
echo "✓ Built:"
ls -l "$OUT/fb-emulator.wasm" "$OUT/fb-emulator.stamp.txt"
echo "gzip: $(gzip -9 -c "$OUT/fb-emulator.wasm" | wc -c | tr -d ' ') bytes"
echo "Commit both files in web/public/assets."
