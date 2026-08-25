cmake_minimum_required(VERSION 3.15)
project(RoutePlannerWasm VERSION 6.0 LANGUAGES CXX)

# WebAssembly build for the static web version.
# Build with the Emscripten toolchain:
#   cmake -DCMAKE_TOOLCHAIN_FILE=<emsdk>/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ..
# (see build_wasm.bat / build_wasm.sh)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Sources shared with the native build (NO osrm_client/map_builder/main/cxxopts:
# network sockets and file-based HTML output live on the JS side in the browser).
set(WASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/csv_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tsp_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/clustering.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/wasm_main.cpp
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party
)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")

# Emscripten link options
# - MODULARIZE: expose the module as a factory function (createRoutePlanner)
# - ALLOW_MEMORY_GROWTH: let WASM grow past INITIAL_MEMORY on demand
# - EXPORTED_FUNCTIONS: C-ABI surface used by js/wasm/worker-entry.js
# - EXPORTED_RUNTIME_METHODS: helpers used from JS
set(CMAKE_EXECUTABLE_SUFFIX ".js")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createRoutePlanner \
    -sALLOW_MEMORY_GROWTH=1 \
    -sMAXIMUM_MEMORY=4GB \
    -sINITIAL_MEMORY=64MB \
    -sEXPORTED_FUNCTIONS=_parse_csv,_haversine_matrix_fill,_compute_routes_wasm,_free_ptr,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=HEAPF64,HEAPU8,UTF8ToString,stringToUTF8")

add_executable(route_planner_wasm ${WASM_SOURCES})

# Copy artifact into the template/wasm output dir after build
add_custom_command(TARGET route_planner_wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:route_planner_wasm>
        ${CMAKE_CURRENT_SOURCE_DIR}/route_planner_wasm.js
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/route_planner_wasm.wasm
        ${CMAKE_CURRENT_SOURCE_DIR}/route_planner_wasm.wasm)

message(STATUS "WASM output dir: ${CMAKE_CURRENT_SOURCE_DIR}")