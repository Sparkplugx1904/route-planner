#include "types.hpp"
#include "utils.hpp"
#include "csv_parser.hpp"
#include "osrm_client.hpp"
#include "engine.hpp"
#include "map_builder.hpp"
#include "cxxopts.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <exception>

using namespace RoutePlanner;

int main(int argc, char* argv[]) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Parse command line arguments
        cxxopts::Options options("route_planner", 
            "Route Planner v6 — Multi-Mode Vehicle Routing (C++ Edition)\n"
            "\n"
            "ROUTING MODES:\n"
            "  balanced         : Fair workload (α×distance + β×visits). Default mode.\n"
            "                     Teams with far locations get fewer visits.\n"
            "  nearest          : Nearest-neighbor assignment to closest team.\n"
            "                     Fast, simple, geographically compact clusters.\n"
            "  fuel-efficient   : Minimize total distance across ALL teams.\n"
            "                     Most economical fuel usage, compact routes.\n"
            "  equal-distance   : Each team travels equal distance.\n"
            "                     Visit counts may vary per team.\n"
            "  compact          : Geographic clustering, prevents long jumps.\n"
            "                     Teams stay in their region, no cross-assignments.\n"
            "  balanced-distance: Balance both distance AND visit counts equally.\n"
            "                     Hybrid between balanced and equal modes.\n");
        
        options.add_options()
            ("csv", "CSV file with locations (required)", 
             cxxopts::value<std::string>())
            ("start-name", "Depot/start location name (required)", 
             cxxopts::value<std::string>())
            ("start-lat", "Depot latitude (required)", 
             cxxopts::value<double>())
            ("start-lon", "Depot longitude (required)", 
             cxxopts::value<double>())
            ("teams", "Number of teams", 
             cxxopts::value<int>()->default_value("14"))
            ("mode", "Routing mode (balanced/nearest/fuel-efficient/equal-distance/compact/balanced-distance)", 
             cxxopts::value<std::string>()->default_value("balanced"))
            ("output", "Output HTML file", 
             cxxopts::value<std::string>()->default_value("routes_v6.html"))
            ("restarts", "Number of clustering restarts for optimization", 
             cxxopts::value<int>()->default_value("10"))
            ("profile", "OSRM profile (driving/bike/foot)", 
             cxxopts::value<std::string>()->default_value("driving"))
            ("no-road-lines", "Draw straight lines instead of road geometry")
            ("http-workers", "Number of HTTP worker threads", 
             cxxopts::value<int>()->default_value("8"))
            ("h,help", "Print this help message");
        
        auto result = options.parse(argc, argv);
        
        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::cout << "\nEXAMPLES:\n";
            std::cout << "  # Balanced workload (default)\n";
            std::cout << "  route_planner --csv data.csv --start-name \"HQ\" --start-lat -8.65 --start-lon 115.23 --teams 10\n\n";
            std::cout << "  # Fuel-efficient mode\n";
            std::cout << "  route_planner --csv data.csv --start-name \"HQ\" --start-lat -8.65 --start-lon 115.23 --teams 10 --mode fuel-efficient\n\n";
            std::cout << "  # Equal distance per team\n";
            std::cout << "  route_planner --csv data.csv --start-name \"HQ\" --start-lat -8.65 --start-lon 115.23 --teams 10 --mode equal-distance\n\n";
            return 0;
        }
        
        // Validate required arguments
        if (!result.count("csv") || !result.count("start-name") ||
            !result.count("start-lat") || !result.count("start-lon")) {
            std::cerr << "Error: Missing required arguments\n\n";
            std::cout << options.help() << std::endl;
            return 1;
        }
        
        Config config;
        config.csv_path = result["csv"].as<std::string>();
        config.start_name = result["start-name"].as<std::string>();
        config.start_lat = result["start-lat"].as<double>();
        config.start_lon = result["start-lon"].as<double>();
        config.num_teams = result["teams"].as<int>();
        config.output_path = result["output"].as<std::string>();
        config.num_restarts = result["restarts"].as<int>();
        config.profile = result["profile"].as<std::string>();
        config.draw_road_lines = !result.count("no-road-lines");
        config.http_workers = result["http-workers"].as<int>();
        config.mode = parse_routing_mode(result["mode"].as<std::string>());
        
        // Banner
        std::cout << "\n";
        std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║         Route Planner v6 — Multi-Mode Vehicle Routing        ║\n";
        std::cout << "║                      C++ Implementation                       ║\n";
        std::cout << "║                 Mode: " << std::left << std::setw(37) 
                  << routing_mode_to_string(config.mode) << "║\n";
        std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
        std::cout << "\n";
        
        // Step 1: Load locations
        std::cout << "[1/6] Loading locations from CSV: " << config.csv_path << std::endl;
        std::vector<Location> locations = CSVParser::load_locations(config.csv_path);
        std::cout << "      ✓ " << locations.size() << " locations loaded" << std::endl;
        
        // Add depot as first location
        Location depot(config.start_name, config.start_lat, config.start_lon);
        std::vector<Location> all_locations = {depot};
        all_locations.insert(all_locations.end(), locations.begin(), locations.end());
        
        // Extract coordinates
        std::vector<Coord> all_coords;
        for (const auto& loc : all_locations) {
            all_coords.push_back(loc.coord());
        }
        
        std::vector<Coord> location_coords(all_coords.begin() + 1, all_coords.end());
        int n_locations = static_cast<int>(locations.size());
        
        // Step 2: Fetch distance matrix
        std::cout << "[2/6] Fetching distance matrix (OSRM with caching)..." << std::endl;
        OSRMClient osrm_client;
        auto matrix_opt = osrm_client.fetch_matrix(all_coords, config.profile);
        
        Matrix full_matrix;
        Matrix location_matrix;
        
        if (matrix_opt.has_value()) {
            full_matrix = matrix_opt.value();
            std::cout << "      ✓ Using road distances" << std::endl;
            
            // Extract location submatrix
            location_matrix.resize(n_locations);
            for (int i = 0; i < n_locations; ++i) {
                location_matrix[i].resize(n_locations);
                for (int j = 0; j < n_locations; ++j) {
                    location_matrix[i][j] = full_matrix[i + 1][j + 1];
                }
            }
        } else {
            std::cout << "      ! Fallback to haversine × 1.3" << std::endl;
            full_matrix = haversine_matrix(all_coords);
            location_matrix = haversine_matrix(location_coords);
        }
        
        // Step 3 & 4: Estimate weights + mode-based clustering + TSP routes
        std::cout << "[3/6] " << (config.mode == RoutingMode::BALANCED
                                      ? "Auto-tuning workload weights..."
                                      : "Preparing clustering for mode: " + routing_mode_to_string(config.mode))
                  << std::endl;
        EngineResult engine = compute_routes(
            all_coords, full_matrix,
            config.num_teams, config.mode, config.num_restarts);
        
        if (config.mode == RoutingMode::BALANCED) {
            std::cout << "      ✓ α (distance) = " << engine.weights.alpha 
                      << ", β (visits) = " << engine.weights.beta << std::endl;
        }
        std::cout << "      ✓ Clustering complete" << std::endl;
        
        // Step 5 & 6: Generate map (includes route summary)
        MapBuilder::build_html_map(all_locations, full_matrix, engine.assignment,
                                   config.num_teams, n_locations,
                                   config.output_path, engine.weights,
                                   &osrm_client, config.profile, config.draw_road_lines);
        
        // Timing
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        std::cout << "\nTotal time: " << (duration / 1000.0) << " seconds\n";
        std::cout << "\n✓ Success! Open " << config.output_path << " in your browser.\n\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
