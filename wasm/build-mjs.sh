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
emcc -O3 \
--pre-js ./pre.js \
-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS \
-g \
wasm_vol_geom.c \
../src/vol_geom.c \
-sENVIRONMENT='web' \
-sSINGLE_FILE \
-sFORCE_FILESYSTEM \
-sASYNCIFY \
-sALLOW_MEMORY_GROWTH \
-I ../src/ \
-o vol_web.mjs
echo "emcc done"

trap : 0
echo >&2 -e '\033[0;32m
********************
*** SUCCEEDED    ***
********************
\033[0m'
