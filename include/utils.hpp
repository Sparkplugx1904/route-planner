#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <cmath>

namespace RoutePlanner {

// Mathematical constants
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double PI = 3.14159265358979323846;

// Haversine distance in kilometers
double haversine_km(const Coord& a, const Coord& b);

// Convert degrees to radians
inline double deg_to_rad(double deg) {
    return deg * PI / 180.0;
}

// Create haversine distance matrix with circuity factor
Matrix haversine_matrix(const std::vector<Coord>& coords, double circuity = 1.3);

// Hash coordinates for caching (MD5 if available, otherwise simple hash)
std::string coords_hash(const std::vector<Coord>& coords);

// Simple hash for strings (fallback)
std::string simple_hash(const std::string& data);

// Route length calculation
double route_length(const Matrix& matrix, const std::vector<int>& indices, 
                   const std::vector<int>& order);

// Random number utilities
class Random {
private:
    unsigned int seed_;
    
public:
    explicit Random(unsigned int seed = 0) : seed_(seed) {}
    
    void set_seed(unsigned int seed) { seed_ = seed; }
    
    // Simple LCG random number generator
    int next_int(int max_val) {
        seed_ = (seed_ * 1103515245U + 12345U) & 0x7fffffffU;
        return static_cast<int>(seed_ % max_val);
    }
    
    double next_double() {
        return static_cast<double>(next_int(1000000)) / 1000000.0;
    }
    
    // Shuffle vector
    template<typename T>
    void shuffle(std::vector<T>& vec) {
        for (size_t i = vec.size() - 1; i > 0; --i) {
            size_t j = next_int(i + 1);
            std::swap(vec[i], vec[j]);
        }
    }
};

// Color palette for visualization
std::string get_color(int index);

// Format utilities
std::string format_distance(double km);
std::string format_time(double seconds);

} // namespace RoutePlanner
