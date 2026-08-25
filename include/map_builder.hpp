#pragma once

#include "types.hpp"
#include "clustering.hpp"
#include "osrm_client.hpp"
#include <string>
#include <vector>

namespace RoutePlanner {

class MapBuilder {
public:
    // Build interactive HTML map with Leaflet.js
    static void build_html_map(const std::vector<Location>& all_locations,
                              const Matrix& matrix,
                              const std::vector<int>& assignment,
                              int num_teams,
                              int n_locations,
                              const std::string& output_path,
                              const WorkloadWeights& weights,
                              OSRMClient* osrm_client = nullptr,
                              const std::string& profile = "driving",
                              bool draw_road_lines = true);

private:
    // Generate HTML header with Leaflet.js CDN
    static std::string generate_header(double center_lat, double center_lon);
    
    // Generate marker for depot (start/finish)
    static std::string generate_depot_marker(const Location& depot);
    
    // Generate markers and routes for a team
    static std::string generate_team_layer(int team_id,
                                          const std::vector<Location>& all_locations,
                                          const std::vector<int>& ordered_indices,
                                          double distance_km,
                                          int num_visits,
                                          double workload,
                                          const std::string& color,
                                          OSRMClient* osrm_client = nullptr,
                                          const std::string& profile = "driving",
                                          bool draw_road_lines = true);
    
    // Generate layer control
    static std::string generate_layer_control();
    
    // Generate HTML footer
    static std::string generate_footer();
    
    // Escape string for JavaScript
    static std::string escape_js(const std::string& str);
};

} // namespace RoutePlanner
