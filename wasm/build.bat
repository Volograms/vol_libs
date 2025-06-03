@echo off
setlocal EnableDelayedExpansion

echo Building WebAssembly modules (both MJS and JS versions)...
echo.
echo ===== Building MJS version =====
echo.

call build-mjs.bat
if %ERRORLEVEL% neq 0 (
    echo MJS build failed with error code: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo ===== Building JS version =====
echo.

call build-js.bat
if %ERRORLEVEL% neq 0 (
    echo JS build failed with error code: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo ===== All builds completed successfully =====
echo.

endlocal 