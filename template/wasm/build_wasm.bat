@echo off
REM ============================================================
REM Build Route Planner WASM (Windows)
REM Requires: Emscripten SDK (emsdk) installed, default C:\emsdk
REM ============================================================
setlocal

echo ============================================
echo Route Planner v6 - WASM Build Script
echo ============================================
echo.

if "%EMSDK%"=="" set "EMSDK=C:\emsdk"

if not exist "%EMSDK%\emsdk_env.bat" (
    echo Error: Emscripten SDK not found at "%EMSDK%"
    echo Set the EMSDK environment variable or edit this script.
    exit /b 1
)

call "%EMSDK%\emsdk_env.bat" >nul 2>&1
if errorlevel 1 (
    echo Error: Failed to load emsdk environment
    exit /b 1
)

set "THIS=%~dp0"
set "BUILD_DIR=%THIS%build-em"

where emcc >nul 2>nul
if errorlevel 1 (
    echo Error: emcc not found in PATH after emsdk_env. Check EMSDK location.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [1/2] Configuring with CMake (Emscripten toolchain)...
emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo Error: CMake configuration failed
    exit /b 1
)

echo.
echo [2/2] Building WASM module...
cmake --build . --config Release
if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)

echo.
echo Complete! Artifacts:
echo   %THIS%route_planner_wasm.js
echo   %THIS%route_planner_wasm.wasm
echo.
echo Serve the template/ folder over HTTP to use the app.

endlocal