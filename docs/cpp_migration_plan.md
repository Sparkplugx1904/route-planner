# C++ Migration Plan - Route Planner v6

## Python Dependencies → C++ Equivalents

| Python Library | C++ Equivalent | Purpose |
|---------------|----------------|---------|
| `requests` | `cpp-httplib` (header-only) | HTTP client untuk OSRM API |
| `json` (implicit) | `nlohmann/json` (header-only) | JSON parsing OSRM response |
| `csv` | Custom parser | CSV parsing (simple, no external lib needed) |
| `folium` | Custom HTML generator | Generate Leaflet.js HTML map |
| `concurrent.futures` | `std::thread` + thread pool | Parallel processing |
| `pickle` | Binary serialization | Cache matrix ke disk |
| `hashlib.md5` | `<openssl/md5.h>` or custom | Hash koordinat untuk cache key |
| `argparse` | `cxxopts` (header-only) | CLI argument parsing |
| `pathlib.Path` | `<filesystem>` (C++17) | File operations |
| `heapq` | `std::priority_queue` | Min-heap untuk clustering |
| `random` | `<random>` | Random number generation |
| `math` | `<cmath>` | Mathematical functions |

## Project Structure

```
route_planner_cpp/
├── CMakeLists.txt
├── README.md
├── build.sh
├── include/
│   ├── utils.hpp           # Core utilities (haversine, hash, etc)
│   ├── types.hpp           # Data types (Location, Matrix, etc)
│   ├── csv_parser.hpp      # CSV loading
│   ├── osrm_client.hpp     # OSRM HTTP client with caching
│   ├── tsp_solver.hpp      # TSP algorithms (DP, NN+2opt)
│   ├── clustering.hpp      # Workload balancing & clustering
│   └── map_builder.hpp     # HTML map generation
├── src/
│   ├── utils.cpp
│   ├── csv_parser.cpp
│   ├── osrm_client.cpp
│   ├── tsp_solver.cpp
│   ├── clustering.cpp
│   ├── map_builder.cpp
│   └── main.cpp
└── third_party/            # Header-only libraries (bundled)
    ├── httplib.h           # https://github.com/yhirose/cpp-httplib
    ├── json.hpp            # https://github.com/nlohmann/json
    └── cxxopts.hpp         # https://github.com/jarro2783/cxxopts
```

## Key Implementation Challenges

1. **HTTP Client**: cpp-httplib is header-only, easy to use
2. **Parallel Processing**: std::thread pool manual implementation
3. **Matrix Caching**: Binary format with simple header (size + data)
4. **HTML Generation**: String templates with Leaflet.js
5. **TSP DP**: Bitmask DP same as Python (translate 1:1)
6. **Clustering**: Priority queue + workload tracking

## Compilation Requirements

- **C++17** minimum (for `<filesystem>`)
- **OpenSSL** (optional, for MD5 hash; fallback to std::hash)
- **OpenMP** (optional, for easy parallelism)

## Build Commands

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./route_planner --csv ../data.csv --start-name "Kantor" --start-lat -8.65 --start-lon 115.23 --teams 14
```
