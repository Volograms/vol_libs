@echo off
setlocal EnableDelayedExpansion

echo Building WebAssembly module (Simple JS version without pthread and OPFS)...

:: Set environment variables
set CC=gcc
set CPP=g++ -std=c++11

:: Create error handling
set ERRORLEVEL=0

set PATH=%PATH%;C:\work\code\emsdk\upstream\emscripten\

echo emcc...
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 ^
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']" ^
-s "EXPORTED_FUNCTIONS=['_malloc','_free']" ^
-s SINGLE_FILE ^
-s MALLOC=emmalloc ^
-s INITIAL_MEMORY=33554432 ^
-s MAXIMUM_MEMORY=1073741824 ^
-s ALLOW_MEMORY_GROWTH=1 ^
-s MODULARIZE=1 ^
-s EXPORT_NAME="VolWeb" ^
-s ENVIRONMENT=web ^
-o vol_web_simple.js ^
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
    echo Build failed with error %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

echo Build completed successfully! Created vol_web_simple.js (no pthread/SharedArrayBuffer/OPFS)
pause

endlocal