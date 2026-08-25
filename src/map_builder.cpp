#include "map_builder.hpp"
#include "engine.hpp"
#include "tsp_solver.hpp"
#include "utils.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <numeric>

namespace RoutePlanner {

std::string MapBuilder::escape_js(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

std::string MapBuilder::generate_header(double center_lat, double center_lon) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    
    oss << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Route Planner - Workload Balanced Routes</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <style>
        body { margin: 0; padding: 0; }
        #map { width: 100%; height: 100vh; }
        .leaflet-control-layers { font-family: Arial, sans-serif; font-size: 13px; }
    </style>
</head>
<body>
    <div id="map"></div>
    <script>
        var map = L.map('map').setView([)" << center_lat << ", " << center_lon << R"(], 12);
        
        L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>',
            maxZoom: 20
        }).addTo(map);
        
        var overlays = {};
)";
    
    return oss.str();
}

std::string MapBuilder::generate_depot_marker(const Location& depot) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    
    oss << R"(
        // Depot marker (start/finish for all teams)
        var depotIcon = L.divIcon({
            html: '<div style="background:#111;color:#ffd700;border-radius:50%;' +
                  'width:26px;height:26px;text-align:center;line-height:26px;' +
                  'font-size:12px;font-weight:bold;border:2px solid #ffd700;' +
                  'box-shadow:0 0 5px rgba(0,0,0,0.7);">S</div>',
            className: '',
            iconSize: [26, 26],
            iconAnchor: [13, 13]
        });
        
        L.marker([)" << depot.latitude << ", " << depot.longitude << R"(], {icon: depotIcon})
            .bindPopup('<b>START &amp; FINISH</b><br>)" << escape_js(depot.name) << R"(')
            .addTo(map);
)";
    
    return oss.str();
}

std::string MapBuilder::generate_team_layer(int team_id,
                                           const std::vector<Location>& all_locations,
                                           const std::vector<int>& ordered_indices,
                                           double distance_km,
                                           int num_visits,
                                           double workload,
                                           const std::string& color,
                                           OSRMClient* osrm_client,
                                           const std::string& profile,
                                           bool draw_road_lines) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    
    oss << "\n        // Team " << team_id << "\n";
    oss << "        var team" << team_id << " = L.featureGroup();\n";
    
    // Generate markers
    int seq = 1;
    for (size_t i = 0; i < ordered_indices.size(); ++i) {
        int global_idx = ordered_indices[i];
        const Location& loc = all_locations[global_idx];
        
        // Skip depot at start and end
        if (global_idx == 0) {
            if (i == 0) {
                seq = 1;  // First visit after depot
            }
            continue;
        }
        
        oss << "        L.marker([" << loc.latitude << ", " << loc.longitude << "], {\n";
        oss << "            icon: L.divIcon({\n";
        oss << "                html: '<div style=\"background:" << color << ";color:white;border-radius:50%;' +\n";
        oss << "                      'width:24px;height:24px;text-align:center;line-height:24px;' +\n";
        oss << "                      'font-size:11px;font-weight:bold;border:2px solid white;\">" << seq << "</div>',\n";
        oss << "                className: '', iconSize: [24, 24], iconAnchor: [12, 12]\n";
        oss << "            })\n";
        oss << "        }).bindPopup('<b>Tim " << team_id << "</b><br>Stop " << seq << ": " 
            << escape_js(loc.name) << "').addTo(team" << team_id << ");\n";
        
        seq++;
    }
    
    // Generate route polylines (with road geometry if available)
    if (draw_road_lines && osrm_client != nullptr && ordered_indices.size() >= 2) {
        // Fetch geometry for each segment
        for (size_t i = 0; i < ordered_indices.size() - 1; ++i) {
            int from_idx = ordered_indices[i];
            int to_idx = ordered_indices[i + 1];
            
            const Location& from = all_locations[from_idx];
            const Location& to = all_locations[to_idx];
            
            // Fetch route geometry
            auto geometry = osrm_client->fetch_geometry(
                from.coord(), to.coord(), profile, 10);
            
            if (geometry.has_value() && geometry.value().size() >= 2) {
                // Draw polyline with route geometry
                oss << "        L.polyline([";
                for (size_t j = 0; j < geometry.value().size(); ++j) {
                    if (j > 0) oss << ", ";
                    const auto& coord = geometry.value()[j];
                    oss << "[" << coord.first << ", " << coord.second << "]";
                }
                oss << "], {color: '" << color << "', weight: 4, opacity: 0.85}).addTo(team" << team_id << ");\n";
            } else {
                // Fallback to straight line
                oss << "        L.polyline([[" << from.latitude << ", " << from.longitude 
                    << "], [" << to.latitude << ", " << to.longitude 
                    << "]], {color: '" << color << "', weight: 4, opacity: 0.85}).addTo(team" << team_id << ");\n";
            }
        }
    } else {
        // Draw simple straight lines between points
        oss << "        L.polyline([";
        for (size_t i = 0; i < ordered_indices.size(); ++i) {
            int global_idx = ordered_indices[i];
            const Location& loc = all_locations[global_idx];
            if (i > 0) oss << ", ";
            oss << "[" << loc.latitude << ", " << loc.longitude << "]";
        }
        oss << "], {color: '" << color << "', weight: 4, opacity: 0.85, dashArray: '5, 10'}).addTo(team" << team_id << ");\n";
    }
    
    // Add to overlays
    oss << std::fixed << std::setprecision(1);
    oss << "        overlays['Tim " << team_id << " — " << num_visits << " titik, " 
        << distance_km << " km, workload=" << workload << "'] = team" << team_id << ";\n";
    oss << "        team" << team_id << ".addTo(map);\n";
    
    return oss.str();
}

std::string MapBuilder::generate_layer_control() {
    return R"(
        L.control.layers(null, overlays, {collapsed: false}).addTo(map);
    </script>
</body>
</html>
)";
}

void MapBuilder::build_html_map(const std::vector<Location>& all_locations,
                                const Matrix& matrix,
                                const std::vector<int>& assignment,
                                int num_teams,
                                int n_locations,
                                const std::string& output_path,
                                const WorkloadWeights& weights,
                                OSRMClient* osrm_client,
                                const std::string& profile,
                                bool draw_road_lines) {
    
    std::cout << "[5/6] Generating HTML map..." << std::endl;
    
    // Calculate center
    double lat_sum = 0.0, lon_sum = 0.0;
    for (const auto& loc : all_locations) {
        lat_sum += loc.latitude;
        lon_sum += loc.longitude;
    }
    double center_lat = lat_sum / all_locations.size();
    double center_lon = lon_sum / all_locations.size();
    
    // Build team routes (shared with WASM engine)
    std::vector<TeamRoute> team_routes = build_team_routes(
        matrix, assignment, num_teams, n_locations, weights);
    
    // Generate HTML
    std::ofstream html(output_path);
    if (!html.is_open()) {
        std::cerr << "[Error] Cannot write to " << output_path << std::endl;
        return;
    }
    
    html << generate_header(center_lat, center_lon);
    html << generate_depot_marker(all_locations[0]);
    
    for (const auto& route : team_routes) {
        std::string color = get_color(route.team_id);
        html << generate_team_layer(route.team_id + 1, all_locations,
                                    route.ordered_indices, route.distance_km,
                                    route.num_visits, route.workload, color,
                                    osrm_client, profile, draw_road_lines);
    }
    
    html << generate_layer_control();
    html.close();
    
    // Print summary
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "RINGKASAN RUTE — " << num_teams << " Tim (Workload Balancing)\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << "** Jarak sudah termasuk PULANG-PERGI (PP) ke titik start **\n\n";
    
    // Calculate statistics
    std::vector<double> workloads;
    for (const auto& route : team_routes) {
        workloads.push_back(route.workload);
    }
    
    if (!workloads.empty()) {
        double sum = std::accumulate(workloads.begin(), workloads.end(), 0.0);
        double avg = sum / workloads.size();
        double min_wl = *std::min_element(workloads.begin(), workloads.end());
        double max_wl = *std::max_element(workloads.begin(), workloads.end());
        
        std::cout << "Workload: Min=" << std::fixed << std::setprecision(1) << min_wl
                  << ", Avg=" << avg << ", Max=" << max_wl
                  << ", Range=" << (max_wl - min_wl) << "\n\n";
        
        // Sort by workload
        std::sort(team_routes.begin(), team_routes.end(),
                 [](const TeamRoute& a, const TeamRoute& b) {
                     return a.workload < b.workload;
                 });
        
        for (const auto& route : team_routes) {
            double deviation = (avg > 0) ? ((route.workload - avg) / avg * 100.0) : 0.0;
            std::string flag = (std::abs(deviation) > 10.0) ? 
                              std::string(" (") + (deviation >= 0 ? "+" : "") + 
                              std::to_string(static_cast<int>(deviation)) + "%)" : "";
            
            std::cout << "Tim " << (route.team_id + 1) << ": "
                      << route.num_visits << " titik, "
                      << std::fixed << std::setprecision(1) << route.distance_km << " km, "
                      << "workload=" << route.workload << flag << "\n";
        }
        
        double total_dist = 0.0;
        for (const auto& route : team_routes) {
            total_dist += route.distance_km;
        }
        
        std::cout << "\nTotal jarak: " << total_dist << " km\n";
    }
    
    std::cout << "Peta: " << output_path << "\n";
}

} // namespace RoutePlanner
