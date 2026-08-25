# Third-Party Header-Only Libraries

Please download these header files into the `third_party/` directory:

## 1. cpp-httplib (HTTP client)
- URL: https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
- Save as: `third_party/httplib.h`

## 2. nlohmann/json (JSON parser)
- URL: https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
- Save as: `third_party/json.hpp`

## 3. cxxopts (CLI parser)
- URL: https://raw.githubusercontent.com/jarro2783/cxxopts/master/include/cxxopts.hpp
- Save as: `third_party/cxxopts.hpp`

## Download Commands (PowerShell)

```powershell
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h" -OutFile "third_party/httplib.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile "third_party/json.hpp"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/jarro2783/cxxopts/master/include/cxxopts.hpp" -OutFile "third_party/cxxopts.hpp"
```

## Download Commands (Linux/Mac)

```bash
curl -o third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
curl -o third_party/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
curl -o third_party/cxxopts.hpp https://raw.githubusercontent.com/jarro2783/cxxopts/master/include/cxxopts.hpp
```
