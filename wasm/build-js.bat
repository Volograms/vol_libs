@echo off
setlocal EnableDelayedExpansion

echo Building WebAssembly module (JS version)...

:: Set environment variables
set CC=gcc
set CPP=g++ -std=c++11

:: Create error handling
set ERRORLEVEL=0

set PATH=%PATH%;C:\work\code\emsdk\upstream\emscripten\

echo emcc...
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 ^
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']" ^
-s SINGLE_FILE ^
-s MALLOC=emmalloc ^
-s INITIAL_MEMORY=33554432 ^
-s MAXIMUM_MEMORY=1073741824 ^
-s ALLOW_MEMORY_GROWTH=1 ^
-s MODULARIZE=1 ^
-s EXPORT_NAME="VolWeb" ^
-s WASMFS=1 -lwasmfs.js -lopfs.js ^
-sWASMFS -sFORCE_FILESYSTEM ^
-s FORCE_FILESYSTEM=1 ^
-o vol_web.js ^
--pre-js "pre.js" ^
-g ^
--debug ^
..\src\vol_basis.cpp ^
wasm_vol_geom.c ^
..\src\vol_geom.c ^
..\thirdparty\basis_universal\transcoder\basisu_transcoder.cpp ^
-I .\ ^
-I ..\src\ ^
-I ..\thirdparty\ ^
-I ..\thirdparty\basis_universal\transcoder

if %ERRORLEVEL% neq 0 (
    echo.
    echo ********************
    echo *** BUILD FAILED ***
    echo ********************
    echo.
    echo Exiting with error...
    exit /b %ERRORLEVEL%
)

echo emcc done
echo.
echo ********************
echo *** SUCCEEDED    ***
echo ********************
echo.

endlocal