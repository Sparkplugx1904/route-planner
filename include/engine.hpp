#pragma once

#include "types.hpp"
#include "clustering.hpp"
#include <vector>
#include <string>

namespace RoutePlanner {

// Optional progress reporter (used by WASM build; native passes nullptr)
// stage_id: 11=weights, 12=clustering, 13=tsp
using EngineProgress = void (*)(int stage_id, int percent);

struct EngineResult {
    WorkloadWeights weights;                    // alpha/beta (auto-tuned for BALANCED)
    std::vector<int> assignment;                // per location index (0..n_locations-1)
    std::vector<TeamRoute> team_routes;         // TSP-ordered routes per team
};

// Full compute pipeline: weight estimation + mode-based clustering + TSP routes.
// all_coords[0] MUST be the depot; full_matrix is sized n*n (n = all_coords.size())
// and contains road distances (km). Mirrors the native main.cpp flow.
EngineResult compute_routes(const std::vector<Coord>& all_coords,
                            const Matrix& full_matrix,
                            int num_teams,
                            RoutingMode mode,
                            int num_restarts = 10,
                            EngineProgress progress = nullptr);

// Build TSP-ordered team routes from assignment (shared with MapBuilder)
std::vector<TeamRoute> build_team_routes(const Matrix& matrix,
                                         const std::vector<int>& assignment,
                                         int num_teams,
                                         int n_locations,
                                         const WorkloadWeights& weights,
                                         EngineProgress progress = nullptr);

} // namespace RoutePlanner