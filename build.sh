#!/bin/bash
# Build script for Route Planner C++ (Linux/Mac)

set -e

echo "============================================"
echo "Route Planner v6 - C++ Build Script"
echo "============================================"
echo ""

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake not found. Please install CMake first."
    echo "  Ubuntu/Debian: sudo apt install cmake build-essential"
    echo "  MacOS: brew install cmake"
    exit 1
fi

# Create build directory
mkdir -p build
cd build

echo "[1/3] Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo ""
echo "[2/3] Building project..."
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "[3/3] Build complete!"
echo ""
echo "Executable location: build/route_planner"
echo ""
echo "To run:"
echo "  ./build/route_planner --csv data.csv --start-name \"Kantor\" --start-lat -8.65 --start-lon 115.23 --teams 14"
echo ""
