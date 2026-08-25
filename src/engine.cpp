#include "engine.hpp"
#include "tsp_solver.hpp"
#include "utils.hpp"
#include <numeric>
#include <iostream>
#include <algorithm>

namespace RoutePlanner {

std::vector<TeamRoute> build_team_routes(const Matrix& matrix,
                                         const std::vector<int>& assignment,
                                         int num_teams,
                                         int n_locations,
                                         const WorkloadWeights& weights,
                                         EngineProgress progress) {
    std::vector<TeamRoute> team_routes;
    
    for (int team = 0; team < num_teams; ++team) {
        // Collect team locations (local indices 0..n_locations-1)
        std::vector<int> team_locs;
        for (int i = 0; i < n_locations; ++i) {
            if (assignment[i] == team) {
                team_locs.push_back(i);
            }
        }
        
        if (team_locs.empty()) {
            continue;
        }
        
        // Build indices for TSP: [0 (depot)] + [team_locs + 1]
        std::vector<int> indices = {0};
        for (int loc : team_locs) {
            indices.push_back(loc + 1);
        }
        
        // Solve TSP
        TSPResult tsp = TSPSolver::solve(matrix, indices);
        
        // Convert local order to global indices
        std::vector<int> ordered_global;
        for (int local_idx : tsp.order) {
            ordered_global.push_back(indices[local_idx]);
        }
        
        TeamRoute route;
        route.team_id = team;
        route.ordered_indices = ordered_global;
        route.distance_km = tsp.distance;
        route.num_visits = static_cast<int>(team_locs.size());
        route.workload = Clustering::workload(tsp.distance, route.num_visits, weights);
        
        team_routes.push_back(route);
        
        if (progress) {
            progress(13, static_cast<int>((team + 1) * 100 / num_teams));
        }
    }
    
    return team_routes;
}

EngineResult compute_routes(const std::vector<Coord>& all_coords,
                            const Matrix& full_matrix,
                            int num_teams,
                            RoutingMode mode,
                            int num_restarts,
                            EngineProgress progress) {
    EngineResult result;
    
    int n_locations = static_cast<int>(all_coords.size()) - 1;
    
    // Extract location coordinates and submatrix
    std::vector<Coord> location_coords(all_coords.begin() + 1, all_coords.end());
    Matrix location_matrix;
    location_matrix.resize(n_locations);
    for (int i = 0; i < n_locations; ++i) {
        location_matrix[i].resize(n_locations);
        for (int j = 0; j < n_locations; ++j) {
            location_matrix[i][j] = full_matrix[i + 1][j + 1];
        }
    }
    
    // Estimate workload weights (for balanced modes)
    WorkloadWeights weights(1.0, 1.0);
    if (mode == RoutingMode::BALANCED) {
        if (progress) progress(11, 10);
        weights = Clustering::estimate_weights(
            location_coords, full_matrix, n_locations, num_teams);
        if (progress) progress(11, 100);
    }
    
    // Mode-based clustering
    if (progress) progress(12, 30);
    
    result.weights = weights;
    result.assignment = Clustering::cluster_by_mode(
        mode, location_coords, location_matrix, full_matrix,
        num_teams, n_locations, weights, num_restarts);
    
    if (progress) progress(12, 100);
    
    result.team_routes = build_team_routes(full_matrix, result.assignment,
                                           num_teams, n_locations, weights, progress);
    
    return result;
}

} // namespace RoutePlanner