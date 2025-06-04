#!/bin/bash

# Build script for vol_web.mjs with WasmFS OPFS support (ES6 module version)
# Requires Emscripten 3.1.0+ for WasmFS OPFS backend support
# -s WASMFS=1 enables WasmFS with OPFS support
# -s FORCE_FILESYSTEM=1 ensures filesystem support is available
# -pthread enables pthread support required for WasmFS OPFS
# -s EXPORT_ES6=1 creates ES6 module instead of UMD
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

echo "Building WebAssembly module (ES6 module version) with WasmFS OPFS support..."

CC=gcc
CPP="g++ -std=c++11"

echo "emcc..."
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','PThread']" \
-s "EXPORTED_FUNCTIONS=['_malloc','_free','_setup_opfs_wasmfs','_test_opfs_functionality']" \
-s SINGLE_FILE \
-s MALLOC=mimalloc \
-s INITIAL_MEMORY=52428800 \
-s MAXIMUM_MEMORY=1073741824 \
-s ALLOW_MEMORY_GROWTH=1 \
-s MODULARIZE=1 \
-s EXPORT_NAME="VolWeb" \
-s EXPORT_ES6=1 \
-s WASMFS=1 \
-s FORCE_FILESYSTEM=1 \
-pthread \
-s PTHREAD_POOL_SIZE=2 \
-s ENVIRONMENT=web,worker \
-o vol_web_opfs.mjs \
--pre-js "pre.js" \
-g1 \
../src/vol_basis.cpp \
wasm_vol_geom.c \
../src/vol_geom.c \
wasm_opfs_setup.cpp \
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
