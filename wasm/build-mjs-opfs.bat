@echo off
setlocal EnableDelayedExpansion

echo Building WebAssembly module (ES6 module version) with WasmFS OPFS support...

:: Set environment variables
set CC=gcc
set CPP=g++ -std=c++20

:: Create error handling
set ERRORLEVEL=0

set PATH=%PATH%;..\..\emsdk\upstream\emscripten\

echo emcc...
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 ^
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','PThread']" ^
-s "EXPORTED_FUNCTIONS=['_malloc','_free','_setup_opfs_wasmfs','_test_opfs_functionality','_opfs_file_exists','_opfs_file_size']" ^
-s SINGLE_FILE ^
-s ALLOW_MEMORY_GROWTH=1 ^
-s MODULARIZE=1 ^
-s EXPORT_NAME="VolWeb" ^
-s EXPORT_ES6=1 ^
-s WASMFS=1 ^
-s FORCE_FILESYSTEM=1 ^
-pthread ^
-s PTHREAD_POOL_SIZE=1 ^
-s ENVIRONMENT=web,worker ^
-o vol_web_opfs.mjs ^
--pre-js "pre.js" ^
-g1 ^
..\src\vol_basis.cpp ^
wasm_vol_geom.c ^
..\src\vol_geom.c ^
wasm_opfs_setup.cpp ^
..\thirdparty\basis_universal\transcoder\basisu_transcoder.cpp ^
-lpthread ^
-I .\ ^
-I ..\src\ ^
-I ..\thirdparty\ ^
-I ..\thirdparty\basis_universal\transcoder

if %ERRORLEVEL% neq 0 (
    echo Build failed with error %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

echo Build completed successfully!
pause

endlocal 