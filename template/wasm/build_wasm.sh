#!/usr/bin/env bash
# Build Route Planner WASM (Linux/macOS)
# Requires: Emscripten SDK (emsdk) available in PATH or $EMSDK
set -euo pipefail

cd "$(dirname "$0")"

EMSDK_ROOT="${EMSDK:-$HOME/emsdk}"
if command -v emcc >/dev/null 2>&1; then
  : # already in PATH
elif [ -f "$EMSDK_ROOT/emsdk_env.sh" ]; then
  # shellcheck source=/dev/null
  source "$EMSDK_ROOT/emsdk_env.sh" >/dev/null 2>&1
else
  echo "Error: Emscripten not found. Set EMSDK or add emcc to PATH."
  exit 1
fi

BUILD_DIR="build-em"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[1/2] Configuring with CMake (Emscripten toolchain)..."
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$EMSDK_ROOT/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"

echo "[2/2] Building WASM module..."
cmake --build . --config Release

echo "Complete! Artifacts: route_planner_wasm.js / route_planner_wasm.wasm"