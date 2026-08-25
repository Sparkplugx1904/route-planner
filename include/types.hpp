#pragma once

#include <string>
#include <vector>
#include <utility>

namespace RoutePlanner {

// Basic types
using Matrix = std::vector<std::vector<double>>;
using Coord = std::pair<double, double>;  // (latitude, longitude)

// Routing mode selection
enum class RoutingMode {
    BALANCED,          // Fair workload balancing (α×distance + β×visits)
    NEAREST,           // Nearest-neighbor: assign to closest team
    FUEL_EFFICIENT,    // Minimize total distance across all teams
    EQUAL_DISTANCE,    // Equal distance per team, different visit counts
    COMPACT,           // Geographic clusters, prevent long jumps
    BALANCED_DISTANCE  // Balance both distance and visits with equal weight
};

// Convert string to RoutingMode
inline RoutingMode parse_routing_mode(const std::string& mode_str) {
    if (mode_str == "balanced") return RoutingMode::BALANCED;
    if (mode_str == "nearest") return RoutingMode::NEAREST;
    if (mode_str == "fuel-efficient" || mode_str == "fuel") return RoutingMode::FUEL_EFFICIENT;
    if (mode_str == "equal-distance" || mode_str == "equal") return RoutingMode::EQUAL_DISTANCE;
    if (mode_str == "compact") return RoutingMode::COMPACT;
    if (mode_str == "balanced-distance") return RoutingMode::BALANCED_DISTANCE;
    return RoutingMode::BALANCED; // default
}

// Convert RoutingMode to string
inline std::string routing_mode_to_string(RoutingMode mode) {
    switch (mode) {
        case RoutingMode::BALANCED: return "Balanced Workload";
        case RoutingMode::NEAREST: return "Nearest Neighbor";
        case RoutingMode::FUEL_EFFICIENT: return "Fuel Efficient";
        case RoutingMode::EQUAL_DISTANCE: return "Equal Distance";
        case RoutingMode::COMPACT: return "Compact Geographic";
        case RoutingMode::BALANCED_DISTANCE: return "Balanced Distance";
        default: return "Unknown";
    }
}

struct Location {
    std::string name;
    double latitude;
    double longitude;
    
    Location() : name(""), latitude(0.0), longitude(0.0) {}
    Location(const std::string& n, double lat, double lon)
        : name(n), latitude(lat), longitude(lon) {}
    
    Coord coord() const { return {latitude, longitude}; }
};

struct TeamRoute {
    int team_id;
    std::vector<int> ordered_indices;  // global indices including depot
    double distance_km;
    int num_visits;
    double workload;
    
    TeamRoute() : team_id(0), distance_km(0.0), num_visits(0), workload(0.0) {}
};

struct Config {
    std::string csv_path;
    std::string start_name;
    double start_lat;
    double start_lon;
    int num_teams;
    std::string output_path;
    int num_restarts;
    std::string profile;  // "driving", "bike", "foot"
    bool draw_road_lines;
    int http_workers;
    RoutingMode mode;      // Routing algorithm mode
    
    Config()
        : csv_path(""),
          start_name(""),
          start_lat(0.0),
          start_lon(0.0),
          num_teams(14),
          output_path("routes_v6.html"),
          num_restarts(10),
          profile("driving"),
          draw_road_lines(true),
          http_workers(8),
          mode(RoutingMode::BALANCED) {}
};

} // namespace RoutePlanner
