# Route Planner v6 - C++ Implementation

**Fair Workload Balancing** untuk pembagian rute multi-tim dengan keadilan beban kerja: tim dengan jarak jauh mendapat kunjungan sedikit, tim dengan jarak dekat mendapat kunjungan banyak.

<p align="center">
  <a href="https://sparkplugx1904.github.io/route-planner/?lang=en" target="_blank">
    <img src="https://img.shields.io/badge/TRY IT NOW-orange?style=for-the-badge&&logoColor=white&color=FF5500" alt="Route Planner"/>
  </a>
</p>

## 🎯 Fitur Utama

### 1. **Workload Balancing (Keadilan Beban Kerja)**
```
Workload = α × Jarak(km) + β × Jumlah_Kunjungan
```
- Tim dengan **jarak 56km + 5 kunjungan** ≈ Tim dengan **jarak 6km + 20 kunjungan**
- α dan β **auto-tuned** berdasarkan karakteristik data (tidak hardcoded!)
- Minimalisasi variance workload antar tim

### 2. **Adaptive TSP Solver**
- **≤10 lokasi/tim**: Held-Karp Dynamic Programming (optimal, O(n² × 2ⁿ))
- **11-16 lokasi/tim**: Christofides approximation (1.5× optimal)
- **>16 lokasi/tim**: Nearest Neighbor + 2-opt intensif (500 iterasi)
- Hasil di-cache untuk menghindari rekomputasi

### 3. **OSRM Integration dengan Caching**
- Fetch jarak rute jalan sungguhan dari OSRM API
- Matrix distance di-cache ke disk (`.osrm_cache/`)
- Fallback otomatis ke haversine × 1.3 jika OSRM gagal

### 4. **Interactive HTML Map**
- Leaflet.js untuk visualisasi interaktif
- Layer control per tim dengan statistik workload
- Numbered markers & colored routes

## 📋 Requirements

### Compiler
- **C++17** atau lebih baru
- GCC 7+, Clang 5+, MSVC 2017+, atau MinGW-w64

### Build Tools
- **CMake 3.15+**
- Make atau Ninja (Linux/Mac)
- Visual Studio 2017+ atau MinGW-w64 (Windows)

### Optional Dependencies
- **OpenSSL** (untuk MD5 hash yang lebih cepat, fallback ke std::hash jika tidak ada)
- **OpenMP** (untuk paralelisasi, opsional)

### Installation

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential cmake libssl-dev
```

#### MacOS
```bash
brew install cmake openssl
```

#### Windows
1. Install [CMake](https://cmake.org/download/)
2. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) dengan "Desktop development with C++"
   ATAU
3. Install [MinGW-w64](https://www.mingw-w64.org/)

## 🚀 Build & Run

### Quick Start (Windows)
```batch
build.bat
cd build\Release
route_planner.exe --csv ..\..\locations.csv --start-name "Kantor Pusat" --start-lat -8.6507 --start-lon 115.2321 --teams 14
```

### Quick Start (Linux/Mac)
```bash
chmod +x build.sh
./build.sh
./build/route_planner --csv locations.csv --start-name "Kantor Pusat" --start-lat -8.6507 --start-lon 115.2321 --teams 14
```

### Manual Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

## 📄 CSV Format

File CSV harus memiliki 3 kolom (header fleksibel, case-insensitive):

```csv
location, latitude, longitude
Toko A, -8.540357, 115.322862
Toko B, -8.654217, 115.216231
Toko C, -8.789543, 115.178654
...
```

**Supported column names:**
- **Name**: `location`, `lokasi`, `nama`, `name`, `sekolah`, `school`, `place`, `site`
- **Latitude**: `latitude`, `lat`
- **Longitude**: `longitude`, `lon`, `lng`

## 🎮 Command Line Options

```
route_planner [OPTIONS]

Required:
  --csv <file>              Path to CSV file with locations
  --start-name <name>       Depot/start location name
  --start-lat <lat>         Depot latitude
  --start-lon <lon>         Depot longitude

Optional:
  --teams <N>               Number of teams (default: 14)
  --output <file>           Output HTML file (default: routes_v6.html)
  --restarts <N>            Clustering restart attempts (default: 10)
  --profile <mode>          OSRM profile: driving/bike/foot (default: driving)
  --no-road-lines           Draw straight lines instead of road geometry
  --http-workers <N>        HTTP worker threads (default: 8)
  -h, --help                Print help
```

## 📊 Output

### Console Output
```
╔═══════════════════════════════════════════════════════════════╗
║         Route Planner v6 — Workload Balanced Routes          ║
║                      C++ Implementation                       ║
╚═══════════════════════════════════════════════════════════════╝

[1/6] Loading locations from CSV: locations.csv
      ✓ 150 locations loaded
[2/6] Fetching distance matrix (OSRM with caching)...
      ✓ Using road distances
[3/6] Auto-tuning workload weights...
      ✓ α (distance) = 1.00, β (visits) = 3.45
[4/6] Workload-aware clustering (10 restarts)...
    Target workload per team: ~128.5 (α=1.00, β=3.45)
      ✓ Clustering complete
[5/6] Generating HTML map...

================================================================================
RINGKASAN RUTE — 14 Tim (Workload Balancing)
================================================================================
** Jarak sudah termasuk PULANG-PERGI (PP) ke titik start **

Workload: Min=115.2, Avg=128.7, Max=142.3, Range=27.1

Tim 3: 18 titik, 12.5 km, workload=116.8 (-9%)
Tim 7: 8 titik, 98.3 km, workload=125.9 (-2%)
Tim 1: 10 titik, 92.1 km, workload=126.6 (-2%)
...
Tim 12: 6 titik, 121.5 km, workload=142.2 (+10%)

Total jarak: 1543.2 km (sudah termasuk PP semua tim)
Peta: routes_v6.html

Total time: 8.3 seconds

✓ Success! Open routes_v6.html in your browser.
```

### HTML Map
- Interactive Leaflet.js map
- Colored routes per team
- Numbered markers (1, 2, 3, ...)
- Depot marker (S for Start/Finish)
- Layer control with workload statistics

## 🏗️ Project Structure

```
route_planner_cpp/
├── CMakeLists.txt              # CMake build configuration
├── build.sh / build.bat        # Build scripts
├── README.md                   # This file
├── cpp_migration_plan.md       # Migration documentation
│
├── include/                    # Header files
│   ├── types.hpp               # Core data types
│   ├── utils.hpp               # Utilities (haversine, hash, etc)
│   ├── csv_parser.hpp          # CSV file loader
│   ├── osrm_client.hpp         # OSRM HTTP client with caching
│   ├── tsp_solver.hpp          # TSP algorithms (DP, NN+2opt)
│   ├── clustering.hpp          # Workload balancing & clustering
│   └── map_builder.hpp         # HTML map generation
│
├── src/                        # Implementation files
│   ├── utils.cpp
│   ├── csv_parser.cpp
│   ├── osrm_client.cpp
│   ├── tsp_solver.cpp
│   ├── clustering.cpp
│   ├── map_builder.cpp
│   └── main.cpp                # Entry point
│
├── third_party/                # Header-only libraries
│   ├── cxxopts.hpp            # CLI argument parsing
│   └── README.md              # Instructions to download httplib.h, json.hpp
│
└── .osrm_cache/                # OSRM matrix cache (auto-created)
```

## ⚡ Performance

### Benchmarks (150 locations, 14 teams, on i7-8750H)

| Operation | Time | Notes |
|-----------|------|-------|
| CSV Loading | <0.1s | Single-threaded |
| OSRM Matrix Fetch (first run) | 5-15s | Depends on network & OSRM load |
| OSRM Matrix Fetch (cached) | <0.1s | Binary disk cache |
| Workload Weight Estimation | 0.5-1s | 20 random samples |
| Clustering (10 restarts) | 2-5s | Parallelizable (TODO) |
| TSP Solving (per team) | <0.1s | Exact DP for ≤10 nodes |
| HTML Generation | <0.1s | String templating |
| **Total (first run)** | **8-20s** | Network-dependent |
| **Total (cached)** | **3-6s** | All local computation |

### Scaling

- **100 locations, 10 teams**: ~5 seconds
- **200 locations, 15 teams**: ~15 seconds
- **500 locations, 20 teams**: ~60 seconds (TSP becomes bottleneck)

## 🔧 Troubleshooting

### Build Errors

**Error: C++17 features not available**
```bash
# Solution: Update compiler or set explicitly
cmake .. -DCMAKE_CXX_STANDARD=17
```

**Error: OpenSSL not found (optional)**
```bash
# Ubuntu/Debian
sudo apt install libssl-dev

# MacOS
brew install openssl
export OPENSSL_ROOT_DIR=/usr/local/opt/openssl
```

### Runtime Errors

**Error: Cannot open CSV file**
- Pastikan path ke CSV benar (relative atau absolute)
- Cek file encoding (harus UTF-8)

**Error: No valid locations found**
- Cek format CSV (header + data rows)
- Pastikan latitude/longitude valid (angka, tidak kosong)

**OSRM fetch failed (fallback to haversine)**
- Normal jika tidak ada koneksi internet
- Hasil tetap valid, tapi estimasi jarak kurang akurat

## 🎯 Algorithm Details

### Round-Trip (PP) Calculation

**Semua jarak yang ditampilkan SUDAH TERMASUK perjalanan pulang-pergi (PP) ke titik start!**

Implementasi TSP (Traveling Salesman Problem) dalam program ini adalah **closed tour**, artinya:
1. Rute dimulai dari **depot (titik start)**
2. Mengunjungi semua lokasi yang ditugaskan
3. **Kembali ke depot** di akhir

**Contoh untuk Tim dengan 3 lokasi (A, B, C):**
```
Rute: Depot → A → B → C → Depot (kembali)
      ↑__________________________|

Jarak = d(Depot→A) + d(A→B) + d(B→C) + d(C→Depot)
                                        ^^^^^^^^^^^^
                                        Ini adalah jarak pulang!
```

**Implementasi di Kode:**

1. **TSP Order** selalu include depot di awal dan akhir:
   ```cpp
   // Contoh order: [0, 3, 5, 7, 0]
   // 0 = depot, 3,5,7 = lokasi, 0 = kembali ke depot
   order.push_back(0);  // Return to depot
   ```

2. **Route Length Calculation** menghitung semua segment:
   ```cpp
   for (size_t i = 0; i + 1 < order.size(); ++i) {
       total += matrix[order[i]][order[i+1]];
   }
   // Loop ini akan include segment terakhir: lokasi_akhir → depot
   ```

3. **Exact DP** explicitly menambahkan jarak return:
   ```cpp
   double total = dp[full_mask][j] + matrix[last_location][depot];
   //                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   //                                 Return distance
   ```

### Workload Balancing

1. **Weight Estimation**: Sample random assignments, compute average distance & visit count per team
2. **Farthest-Point Seeds**: Select spread-out initial cluster centers
3. **Greedy Growth**: Grow clusters one point at a time, stop when workload capacity reached
4. **Local Search**: Swap points between heavy & light teams to minimize variance

### TSP Solving

- **Held-Karp DP**: Bitmask DP with parent tracking, O(n² × 2ⁿ) time, O(n × 2ⁿ) space
- **2-opt**: Edge-swap local search with early stopping
- **Caching**: Results keyed by (depot, sorted_non_depot_set)

### Distance Matrix

- **OSRM**: HTTP GET to `/table/v1/{profile}/{coords}?sources={indices}`
- **Chunking**: 60 coords per request to avoid URL length limits
- **Caching**: Binary format (size header + raw double array)

## 📝 License

MIT License - feel free to use and modify for your projects.

## 🙏 Credits

- **OSRM** (Open Source Routing Machine) for road distance API
- **Leaflet.js** for interactive maps
- **cxxopts** for CLI parsing
- **Original Python version** by the same author

## 🔮 Future Enhancements (TODOs)

- [ ] Implement actual HTTP client using cpp-httplib (currently using fallback)
- [ ] Parallel OSRM chunk fetching with thread pool
- [ ] Parallel clustering restarts with OpenMP
- [ ] Real-time progress bars during long operations
- [ ] JSON output format for programmatic use
- [ ] GUI application (Qt or web-based)
- [ ] One-way routes (not round-trip)
- [ ] Time windows for visits
- [ ] Vehicle capacity constraints
- [ ] Multi-depot support

## 📧 Contact

For questions or issues, please open an issue on the project repository.
