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
-s INITIAL_MEMORY=33554432  \
-s MAXIMUM_MEMORY=1073741824 \
-s ALLOW_MEMORY_GROWTH=1 \
-s MODULARIZE=1 \
-s EXPORT_NAME="VolWeb" \
-s WASMFS \
-s "WASMFS_VIRTUAL_ROOT='/working'" \
-s "WASMFS_TRACKED_DEVICES=['opfs']" \
-o vol_web.js \
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
