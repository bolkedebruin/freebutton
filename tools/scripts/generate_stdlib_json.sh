#!/bin/bash
# Generate stdlib.json from MQuickJS C code
# This script compiles and runs the C++ generator that extracts
# JavaScript API definitions from the actual C source files

set -e  # Exit on error

# Get the project root directory (two levels up from this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MQUICKJS_DIR="$PROJECT_ROOT/lib/mquickjs"
OUTPUT_DIR="$PROJECT_ROOT/web/src/js"

echo "=== Generating stdlib.json from MQuickJS C code ==="
echo "MQuickJS directory: $MQUICKJS_DIR"
echo "Output directory: $OUTPUT_DIR"

# Navigate to mquickjs directory
cd "$MQUICKJS_DIR"

# Clean up any existing build artifacts
if [ -f generate_stdlib_json ]; then
    rm -f generate_stdlib_json
fi
if [ -f mquickjs_build.o ]; then
    rm -f mquickjs_build.o
fi

# Compile mquickjs_build.c
echo "Compiling mquickjs_build.c..."
gcc -c mquickjs_build.c -o mquickjs_build.o
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile mquickjs_build.c"
    exit 1
fi

# Compile generate_stdlib_json.cpp
echo "Compiling generate_stdlib_json.cpp..."
g++ -std=c++17 -o generate_stdlib_json generate_stdlib_json.cpp mquickjs_build.o
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile generate_stdlib_json.cpp"
    exit 1
fi

# Run the generator and save output
echo "Generating stdlib.json..."
./generate_stdlib_json > "$OUTPUT_DIR/stdlib.json"
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
rm -f generate_stdlib_json mquickjs_build.o

echo "=== Done ==="
