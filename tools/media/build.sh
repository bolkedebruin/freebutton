#!/bin/sh
# Builds tools/media/mkmedia (host) from mkmedia.cpp and the firmware's store
# core. Usage: tools/media/build.sh [OUTPUT]   (default: tools/media/build/mkmedia)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$HERE/build/mkmedia}
mkdir -p "$(dirname "$OUT")"
# CPATH with an empty element would let the repo-root VERSION shadow <version>.
unset CPATH
CC=${CC:-cc}
CXX=${CXX:-c++}
"$CC" -std=c11 -O2 -Wall -Wextra -DMEDIA_HOST_LOG -c "$ROOT/src/media/media_core.c" -o "$OUT.core.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$HERE/mkmedia.cpp" "$OUT.core.o" -o "$OUT"
rm -f "$OUT.core.o"
echo "$OUT"
