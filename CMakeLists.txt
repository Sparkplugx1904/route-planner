cmake_minimum_required(VERSION 3.15)
project(RoutePlanner VERSION 6.0 LANGUAGES CXX)

# C++17 required for filesystem
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

# Find OpenSSL (optional, for MD5 hash)
find_package(OpenSSL)
if(OPENSSL_FOUND)
    add_definitions(-DUSE_OPENSSL)
endif()

# Find OpenMP (optional, for parallelism)
find_package(OpenMP)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

# Source files
set(SOURCES
    src/utils.cpp
    src/csv_parser.cpp
    src/osrm_client.cpp
    src/tsp_solver.cpp
    src/clustering.cpp
    src/engine.cpp
    src/map_builder.cpp
    src/main.cpp
)

# Executable
add_executable(route_planner ${SOURCES})

# Link libraries
target_link_libraries(route_planner
    pthread
)

if(OPENSSL_FOUND)
    target_link_libraries(route_planner OpenSSL::SSL OpenSSL::Crypto)
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(route_planner OpenMP::OpenMP_CXX)
endif()

# Windows-specific: link ws2_32 for networking
if(WIN32)
    target_link_libraries(route_planner ws2_32 crypt32)
    # Define Windows 10 for cpp-httplib compatibility
    target_compile_definitions(route_planner PRIVATE _WIN32_WINNT=0x0A00)
endif()

# Installation
install(TARGETS route_planner DESTINATION bin)

# Print build info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "OpenSSL found: ${OPENSSL_FOUND}")
message(STATUS "OpenMP found: ${OpenMP_CXX_FOUND}")
