#!/bin/bash

# -sASYNCIFY allows async operations like emscripten_wget()
# -g includes debug symbols

#-sASYNCIFY \

abort() {
    echo >&2 -e '\033[0;31m
********************
*** BUILD FAILED ***
********************
\033[0m'
    echo "Exiting with error..." >&2
    exit 1
}
trap 'abort' 0

set -e

echo "emcc..."
emcc \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
-g \
wasm_vol_geom.c ../src/vol_geom.c \
-I ../src/ \
-o vol_geom.js
echo "emcc done"

trap : 0
echo >&2 -e '\033[0;32m
********************
*** SUCCEEDED    ***
********************
\033[0m'
