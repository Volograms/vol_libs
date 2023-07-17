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
CC=gcc
CPP="g++ -std=c++11"

emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']" \
-s SINGLE_FILE \
-s ALLOW_MEMORY_GROWTH \
-s EXPORT_ES6=1 \
-s MODULARIZE=1 \
-s EXPORT_NAME="VolWeb" \
-o vol_web.mjs \
--pre-js "pre.js" \
-g \
../src/vol_basis.cpp \
wasm_vol_geom.c \
../src/vol_geom.c \
../thirdparty/basis_universal/transcoder/basisu_transcoder.cpp \
-I ./ \
-I ../src/ \
-I ../thirdparty/ \
-I ../thirdparty/basis_universal/transcoder
echo "emcc done"

trap : 0
echo >&2 -e '\033[0;32m
********************
*** SUCCEEDED    ***
********************
\033[0m'
