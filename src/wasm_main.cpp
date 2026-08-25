#include <emscripten/emscripten.h>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>

#include "types.hpp"
#include "utils.hpp"
#include "csv_parser.hpp"
#include "engine.hpp"
#include "json.hpp"

using namespace RoutePlanner;
using json = nlohmann::json;

// Progress stage ids: 11=weights, 12=clustering, 13=tsp (see engine.hpp)
extern "C" {

static void report_progress(int stage_id, int percent) {
    EM_ASM({ reportProgress($0, $1); }, stage_id, percent);
}

// Allocate a NUL-terminated C buffer (caller must free with free_ptr())
static char* to_c_str(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

static std::string err_json(const std::string& msg) {
    json j;
    j["error"] = msg;
    return j.dump();
}

// Parse CSV content -> JSON array [{name,lat,lon},...]
EMSCRIPTEN_KEEPALIVE
char* parse_csv(const char* data, int len) {
    try {
        std::string csv(data, static_cast<size_t>(len));
        auto locs = CSVParser::parse_locations_from_string(csv, "upload");
        json j = json::array();
        for (const auto& l : locs) {
            j.push_back({ {"name", l.name}, {"lat", l.latitude}, {"lon", l.longitude} });
        }
        return to_c_str(j.dump());
    } catch (const std::exception& e) {
        return to_c_str(err_json(e.what()));
    }
}

// Fill output buffer with haversine distance matrix (km, *circuity).
// coords: n*2 doubles [lat,lon,lat,lon,...]; out: n*n doubles, row-major.
EMSCRIPTEN_KEEPALIVE
void haversine_matrix_fill(double* coords, double* out, int n, double circuity) {
    std::vector<Coord> c;
    c.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        c.emplace_back(coords[i * 2], coords[i * 2 + 1]);
    }
    Matrix m = haversine_matrix(c, circuity);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            out[i * n + j] = m[i][j];
        }
    }
}

// Full compute pipeline.
// coords: n*2 doubles [lat,lon,...], [0]=depot; matrix: n*n doubles (km);
// mode_id: 0=balanced 1=nearest 2=fuel-efficient 3=equal-distance 4=compact 5=balanced-distance
// Returns JSON {weights, assignment, teamRoutes, stats} or {error}.
EMSCRIPTEN_KEEPALIVE
char* compute_routes_wasm(double* coords, double* matrix_data, int n,
                          int teams, int mode_id, int restarts) {
    try {
        // Build coordinate list and matrix
        std::vector<Coord> all_coords;
        all_coords.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            all_coords.emplace_back(coords[i * 2], coords[i * 2 + 1]);
        }
        Matrix full_matrix(static_cast<size_t>(n),
                           std::vector<double>(static_cast<size_t>(n), 0.0));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                full_matrix[i][j] = matrix_data[i * n + j];
            }
        }

        RoutingMode rm = static_cast<RoutingMode>(mode_id);
        EngineResult result = compute_routes(
            all_coords, full_matrix, teams, rm, restarts,
            &report_progress);

        json j;
        j["weights"] = { {"alpha", result.weights.alpha}, {"beta", result.weights.beta} };
        j["assignment"] = result.assignment;

        json routes = json::array();
        double sum = 0.0, min_w = 1e300, max_w = -1e300;
        for (const auto& tr : result.team_routes) {
            double w = tr.workload;
            sum += w;
            if (w < min_w) min_w = w;
            if (w > max_w) max_w = w;
            routes.push_back({
                {"teamId", tr.team_id},
                {"order", tr.ordered_indices},
                {"distanceKm", tr.distance_km},
                {"numVisits", tr.num_visits},
                {"workload", w}
            });
        }
        j["teamRoutes"] = routes;
        size_t cnt = result.team_routes.size();
        j["stats"] = {
            {"avg", cnt ? sum / static_cast<double>(cnt) : 0.0},
            {"min", cnt ? min_w : 0.0},
            {"max", cnt ? max_w : 0.0}
        };

        return to_c_str(j.dump());
    } catch (const std::exception& e) {
        return to_c_str(err_json(e.what()));
    }
}

// Free a buffer previously returned by an exported function.
EMSCRIPTEN_KEEPALIVE
void free_ptr(char* ptr) {
    std::free(ptr);
}

} // extern "C"