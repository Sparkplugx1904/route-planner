@echo off
REM Build script for Route Planner C++ (Windows)

echo ============================================
echo Route Planner v6 - C++ Build Script
echo ============================================
echo.

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Error: CMake not found. Please install CMake first.
    echo Download from: https://cmake.org/download/
    exit /b 1
)

REM Create build directory
if not exist build mkdir build
cd build

echo [1/3] Configuring with CMake...
cmake .. -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo Error: CMake configuration failed
    cd ..
    exit /b 1
)

echo.
echo [2/3] Building project...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo Error: Build failed
    cd ..
    exit /b 1
)

echo.
echo [3/3] Build complete!
echo.
echo Executable location: build\Release\route_planner.exe
echo.
echo To run:
echo   cd build\Release
echo   route_planner.exe --csv ..\..\data.csv --start-name "Kantor" --start-lat -8.65 --start-lon 115.23 --teams 14
echo.

cd ..
