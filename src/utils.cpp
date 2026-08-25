#include "utils.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef USE_OPENSSL
#include <openssl/md5.h>
#endif

namespace RoutePlanner {

double haversine_km(const Coord& a, const Coord& b) {
    double lat1 = a.first;
    double lon1 = a.second;
    double lat2 = b.first;
    double lon2 = b.second;
    
    double phi1 = deg_to_rad(lat1);
    double phi2 = deg_to_rad(lat2);
    double dphi = deg_to_rad(lat2 - lat1);
    double dlambda = deg_to_rad(lon2 - lon1);
    
    double a_val = std::sin(dphi / 2.0) * std::sin(dphi / 2.0) +
                   std::cos(phi1) * std::cos(phi2) *
                   std::sin(dlambda / 2.0) * std::sin(dlambda / 2.0);
    
    double c = 2.0 * std::asin(std::sqrt(a_val));
    return EARTH_RADIUS_KM * c;
}

Matrix haversine_matrix(const std::vector<Coord>& coords, double circuity) {
    size_t n = coords.size();
    Matrix matrix(n, std::vector<double>(n, 0.0));
    
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i != j) {
                matrix[i][j] = haversine_km(coords[i], coords[j]) * circuity;
            }
        }
    }
    
    return matrix;
}

std::string simple_hash(const std::string& data) {
    // Simple hash function (FNV-1a)
    uint64_t hash = 14695981039346656037ULL;
    for (char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::string coords_hash(const std::vector<Coord>& coords) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    
    for (const auto& coord : coords) {
        oss << coord.first << "," << coord.second << ";";
    }
    
    std::string data = oss.str();
    
#ifdef USE_OPENSSL
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), digest);
    
    std::ostringstream hash_oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        hash_oss << std::hex << std::setw(2) << std::setfill('0') 
                 << static_cast<int>(digest[i]);
    }
    return hash_oss.str();
#else
    return simple_hash(data);
#endif
}

double route_length(const Matrix& matrix, const std::vector<int>& indices,
                   const std::vector<int>& order) {
    double total = 0.0;
    for (size_t i = 0; i + 1 < order.size(); ++i) {
        int from = indices[order[i]];
        int to = indices[order[i + 1]];
        total += matrix[from][to];
    }
    return total;
}

std::string get_color(int index) {
    static const std::vector<std::string> palette = {
        "#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4",
        "#42d4f4", "#f032e6", "#808000", "#469990", "#9A6324",
        "#800000", "#000075", "#e6beff", "#a9a9a9", "#bfef45",
        "#fabed4", "#aaffc3", "#ffd8b1", "#dcbeff", "#000000"
    };
    
    if (index < static_cast<int>(palette.size())) {
        return palette[index];
    }
    
    // Generate random color for indices beyond palette
    Random rng(index);
    int r = rng.next_int(256);
    int g = rng.next_int(256);
    int b = rng.next_int(256);
    
    std::ostringstream oss;
    oss << "#" << std::hex << std::setw(2) << std::setfill('0') << r
        << std::setw(2) << std::setfill('0') << g
        << std::setw(2) << std::setfill('0') << b;
    return oss.str();
}

std::string format_distance(double km) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << km << " km";
    return oss.str();
}

std::string format_time(double seconds) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << seconds << " s";
    return oss.str();
}

} // namespace RoutePlanner
