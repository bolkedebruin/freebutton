#!/bin/bash
# Build the mquickjs JS->bytecode compiler as a WebAssembly module WITH the
# FreeButton stdlib, so the browser IDE compiles bytecode whose atom table
# matches the on-device firmware (led, button, sensor, mqtt, display, ... APIs).
#
# This is the FreeButton-flavored web wasm builder. It is deliberately NOT the
# engine fork's build_wasm_mquickjs.sh (that one builds a GENERIC engine against
# example_stdlib and lives in the pinned submodule).
#
# Requires emscripten (emcc). This is a DEV/RELEASE step: the produced artifacts
# (web/public/assets/mquickjs.{js,wasm}) are committed real files so a fresh
# clone + `npm run build` works WITHOUT emcc. Re-run this only when the engine
# (submodule) or the FreeButton stdlib changes.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

MQ="$PROJECT_ROOT/lib/mquickjs"                 # engine (pinned submodule, READ-ONLY)
JS="$PROJECT_ROOT/src/scripting/js"             # freebutton_stdlib.c, stdlib_export.c
TOOLS_JS="$PROJECT_ROOT/tools/js"               # freebutton_stubs_wasm.c/.h
SCRIPTING="$PROJECT_ROOT/src/scripting"         # firmware headers (file_hardware.h ...)
OUT="$PROJECT_ROOT/web/public/assets"           # committed output location
BUILD="$PROJECT_ROOT/.pio/wasm_build"           # scratch (keeps the submodule clean)

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc (Emscripten) not found on PATH. Install: https://emscripten.org/" >&2
    exit 1
fi

echo "== FreeButton wasm build =="
emcc --version | head -1
mkdir -p "$BUILD" "$OUT"

# Step 1: generate the 32-bit FreeButton stdlib + atom headers into the scratch dir.
# (cd into BUILD so a repo-root 'VERSION' file cannot shadow libc's <version> header.)
echo "Step 1: generating FreeButton stdlib headers..."
cd "$BUILD"
gcc -O2 -Wall -D_GNU_SOURCE -I"$MQ" -c "$MQ/mquickjs_build.c" -o "$BUILD/mquickjs_build.host.o"
gcc -O2 -Wall -D_GNU_SOURCE -I"$MQ" -I"$SCRIPTING" -c "$JS/freebutton_stdlib.c" -o "$BUILD/freebutton_stdlib.host.o"
gcc "$BUILD/freebutton_stdlib.host.o" "$BUILD/mquickjs_build.host.o" -o "$BUILD/freebutton_stdlib"
"$BUILD/freebutton_stdlib" -m32     > "$BUILD/freebutton_stdlib.h"
"$BUILD/freebutton_stdlib" -a -m32  > "$BUILD/mquickjs_atom.h"
echo "  -> $BUILD/freebutton_stdlib.h, mquickjs_atom.h"

# Step 2: compile to WebAssembly.
# freebutton_stubs_wasm.c provides no-op stubs for the hardware APIs so their
# names land in the atom table; the real implementations run on-device only.
echo "Step 2: compiling to WebAssembly..."
EXPORTED_FUNCTIONS='["_compile_js_to_bytecode","_get_bytecode_buffer","_get_error_message","_get_bytecode_size","_malloc","_free"]'
EXPORTED_RUNTIME='["ccall","cwrap","HEAPU8","UTF8ToString","stringToUTF8"]'

emcc \
    "$MQ/mquickjs.c" \
    "$MQ/cutils.c" \
    "$MQ/dtoa.c" \
    "$MQ/libm.c" \
    "$MQ/emscripten_wrapper.c" \
    "$TOOLS_JS/freebutton_stubs_wasm.c" \
    "$JS/freebutton_stdlib.c" \
    "$JS/stdlib_export.c" \
    -o "$OUT/mquickjs.js" \
    -I"$BUILD" -I"$MQ" -I"$TOOLS_JS" -I"$JS" -I"$SCRIPTING" \
    -s WASM=1 \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
    -s EXPORTED_RUNTIME_METHODS="$EXPORTED_RUNTIME" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=16MB \
    -s MAXIMUM_MEMORY=64MB \
    -s STACK_SIZE=1MB \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='createMQuickJSModule' \
    -s ENVIRONMENT='web,worker' \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -O3 \
    -DEMSCRIPTEN -D_GNU_SOURCE \
    -DCONFIG_FREEBUTTON_LED \
    -DCONFIG_FREEBUTTON_BUTTON \
    -DCONFIG_FREEBUTTON_SENSOR \
    -DCONFIG_FREEBUTTON_MQTT \
    -DCONFIG_FREEBUTTON_DISPLAY

# Fingerprint of the stdlib / atoms / engine this wasm was built from. CI
# (wasm_stdlib_fingerprint.py --check) compares it with the firmware's.
python3 "$TOOLS_JS/wasm_stdlib_fingerprint.py" "$BUILD" > "$OUT/mquickjs.stdlib.txt"

echo ""
echo "✓ Built:"
ls -lh "$OUT/mquickjs.js" "$OUT/mquickjs.wasm" "$OUT/mquickjs.stdlib.txt"
echo "Commit these real files in web/public/assets (they replace the old symlinks)."
