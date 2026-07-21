#!/bin/bash
# Generate stdlib.json from MQuickJS C code
# This script compiles and runs the C++ generator that extracts
# JavaScript API definitions from the actual C source files

set -e  # Exit on error

# Get the project root directory (two levels up from this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MQUICKJS_DIR="$PROJECT_ROOT/lib/mquickjs"          # engine submodule (mquickjs_build.c/.h, json.hpp)
JS_DIR="$PROJECT_ROOT/src/scripting/js"            # relocated FreeButton stdlib source
TOOLS_JS_DIR="$PROJECT_ROOT/tools/js"              # generate_stdlib_json.cpp
OUTPUT_DIR="$PROJECT_ROOT/web/src/js"
BUILD_DIR="$PROJECT_ROOT/src/scripting/generated"  # scratch build dir (gitignored)

echo "=== Generating stdlib.json from MQuickJS C code ==="
echo "MQuickJS directory: $MQUICKJS_DIR"
echo "JS source directory: $JS_DIR"
echo "Output directory: $OUTPUT_DIR"

mkdir -p "$BUILD_DIR"

# Compile from a neutral CWD. All paths below are absolute, so this is safe —
# and it avoids the repo-root VERSION file shadowing libc++'s <version> header:
# clang searches the CWD, and on case-insensitive macOS filesystems
# `#include <version>` (pulled in transitively by <string>/<iostream>) resolves
# to ./VERSION when the compiler runs from the repo root.
cd "$BUILD_DIR"

# Clean up any existing build artifacts
rm -f "$BUILD_DIR/generate_stdlib_json" "$BUILD_DIR/mquickjs_build.json.o"

# Compile mquickjs_build.c (from the engine submodule)
echo "Compiling mquickjs_build.c..."
gcc -I"$MQUICKJS_DIR" -c "$MQUICKJS_DIR/mquickjs_build.c" -o "$BUILD_DIR/mquickjs_build.json.o"
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile mquickjs_build.c"
    exit 1
fi

# Compile generate_stdlib_json.cpp (from tools/js).
# It includes json.hpp + mquickjs_build.h from the engine submodule and
# freebutton_stdlib.c from src/scripting/js (which pulls in mqjs_stdlib.c).
echo "Compiling generate_stdlib_json.cpp..."
g++ -std=c++17 \
    -I"$MQUICKJS_DIR" -I"$JS_DIR" -I"$PROJECT_ROOT/src/scripting" \
    -o "$BUILD_DIR/generate_stdlib_json" \
    "$TOOLS_JS_DIR/generate_stdlib_json.cpp" "$BUILD_DIR/mquickjs_build.json.o"
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile generate_stdlib_json.cpp"
    exit 1
fi

# Run the generator and save output
echo "Generating stdlib.json..."
"$BUILD_DIR/generate_stdlib_json" > "$OUTPUT_DIR/stdlib.json"
if [ $? -ne 0 ]; then
    echo "Error: Failed to generate stdlib.json"
    exit 1
fi

# Verify the output file was created
if [ ! -f "$OUTPUT_DIR/stdlib.json" ]; then
    echo "Error: stdlib.json was not created"
    exit 1
fi

echo "✓ Successfully generated stdlib.json"
echo "  Output: $OUTPUT_DIR/stdlib.json"
echo "  Size: $(wc -c < "$OUTPUT_DIR/stdlib.json") bytes"

# Clean up build artifacts
rm -f "$BUILD_DIR/generate_stdlib_json" "$BUILD_DIR/mquickjs_build.json.o"

echo "=== Done ==="
