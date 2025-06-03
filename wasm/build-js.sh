#!/bin/bash

# Build script for vol_web.js with WasmFS OPFS support
# Requires Emscripten 3.1.0+ for WasmFS OPFS backend support
# -s WASMFS=1 enables WasmFS with OPFS support
# -s FORCE_FILESYSTEM=1 ensures filesystem support is available
# -pthread enables pthread support required for WasmFS OPFS
# -g includes debug symbols

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

echo "Building WebAssembly module (JS version) with WasmFS OPFS support..."

CC=gcc
CPP="g++ -std=c++11"

echo "emcc..."
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']" \
-s "EXPORTED_FUNCTIONS=['_malloc','_free','_setup_opfs_wasmfs','_test_opfs_functionality']" \
-s SINGLE_FILE \
-s MALLOC=emmalloc \
-s INITIAL_MEMORY=33554432 \
-s MAXIMUM_MEMORY=1073741824 \
-s ALLOW_MEMORY_GROWTH=1 \
-s MODULARIZE=1 \
-s EXPORT_NAME="VolWeb" \
-s WASMFS=1 \
-s FORCE_FILESYSTEM=1 \
-pthread \
-s PTHREAD_POOL_SIZE=navigator.hardwareConcurrency \
-s ENVIRONMENT=web,worker \
-o vol_web.js \
--pre-js "pre.js" \
-g \
--debug \
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
