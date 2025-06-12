@echo off
setlocal EnableDelayedExpansion

echo Building WebAssembly module (Simple JS version without pthread and OPFS)...

:: Set environment variables
set CC=gcc
set CPP=g++ -std=c++20

:: Create error handling
set ERRORLEVEL=0

set PATH=%PATH%;..\..\emsdk\upstream\emscripten\

echo emcc...
emcc -O3 -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 ^
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']" ^
-s "EXPORTED_FUNCTIONS=['_malloc','_free']" ^
-s MALLOC=emmalloc ^
-s SINGLE_FILE ^
-s ALLOW_MEMORY_GROWTH=1 ^
-s MODULARIZE=1 ^
-s EXPORT_NAME="VolWeb" ^
-s ENVIRONMENT=web ^
-o vol_web.js ^
--pre-js "pre.js" ^
-g1 ^
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