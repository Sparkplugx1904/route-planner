#include "clustering.hpp"
#include "tsp_solver.hpp"
#include <queue>
#include <set>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>

namespace RoutePlanner {

double Clustering::workload(double distance_km, int num_visits,
                            const WorkloadWeights& weights) {
    return weights.alpha * distance_km + weights.beta * num_visits;
}

double Clustering::team_route_distance(const Matrix& full_matrix,
                                       const std::vector<int>& team_locations,
                                       const std::string& algorithm) {
    if (team_locations.empty()) {
        return 0.0;
    }
    
    // Build indices: [0 (depot)] + [team_locations mapped to full_matrix indices]
    std::vector<int> indices = {0};
    for (int loc : team_locations) {
        indices.push_back(loc + 1);  // +1 because depot is at index 0
    }
    
    if (indices.size() <= 1) {
        return 0.0;
    }
    
    TSPResult result = TSPSolver::solve(full_matrix, indices, algorithm);
    return result.distance;
}

WorkloadWeights Clustering::estimate_weights(const std::vector<Coord>& location_coords,
                                             const Matrix& full_matrix,
                                             int n_locations,
                                             int num_teams,
                                             int sample_size) {
    Random rng(42);
    std::vector<double> distances;
    std::vector<int> counts;
    
    // Sample random assignments
    for (int trial = 0; trial < std::min(sample_size, 20); ++trial) {
        std::vector<int> assignment(n_locations);
        for (int i = 0; i < n_locations; ++i) {
            assignment[i] = rng.next_int(num_teams);
        }
        
        for (int team = 0; team < num_teams; ++team) {
            std::vector<int> team_locs;
            for (int i = 0; i < n_locations; ++i) {
                if (assignment[i] == team) {
                    team_locs.push_back(i);
                }
            }
            
            if (!team_locs.empty()) {
                double dist = team_route_distance(full_matrix, team_locs, "heuristic");
                distances.push_back(dist);
                counts.push_back(static_cast<int>(team_locs.size()));
            }
        }
    }
    
    if (distances.empty()) {
        return WorkloadWeights(1.0, 5.0);
    }
    
    double avg_dist = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();
    double avg_count = std::accumulate(counts.begin(), counts.end(), 0.0) / counts.size();
    
    // Tune: 1 visit ≈ penalty_per_visit_km equivalent
    double penalty_per_visit_km = (avg_count > 0) ? (avg_dist / (avg_count * 3.0)) : 2.0;
    
    return WorkloadWeights(1.0, penalty_per_visit_km);
}

double Clustering::target_workload_per_team(const std::vector<Coord>& location_coords,
                                            const Matrix& full_matrix,
                                            int n_locations,
                                            int num_teams,
                                            const WorkloadWeights& weights) {
    // Sample a subset to estimate average distance per location
    Random rng(123);
    int sample_size = std::min(n_locations, 30);
    std::vector<int> sample_locs;
    
    std::vector<int> all_locs(n_locations);
    std::iota(all_locs.begin(), all_locs.end(), 0);
    rng.shuffle(all_locs);
    
    for (int i = 0; i < sample_size; ++i) {
        sample_locs.push_back(all_locs[i]);
    }
    
    double sample_dist = team_route_distance(full_matrix, sample_locs, "heuristic");
    double avg_dist_per_loc = sample_locs.empty() ? 5.0 : (sample_dist / sample_locs.size());
    
    double total_estimated_dist = avg_dist_per_loc * n_locations;
    double total_workload = workload(total_estimated_dist, n_locations, weights);
    
    return total_workload / num_teams;
}

std::vector<int> Clustering::farthest_point_seeds(const Matrix& location_matrix,
                                                  int num_teams,
                                                  Random& rng) {
    int n = static_cast<int>(location_matrix.size());
    if (n == 0 || num_teams == 0) {
        return {};
    }
    
    std::vector<int> seeds;
    int start_idx = rng.next_int(n);
    seeds.push_back(start_idx);
    
    std::vector<double> min_dist(n);
    for (int i = 0; i < n; ++i) {
        min_dist[i] = location_matrix[start_idx][i];
    }
    
    while (static_cast<int>(seeds.size()) < num_teams && static_cast<int>(seeds.size()) < n) {
        std::set<int> seed_set(seeds.begin(), seeds.end());
        
        int best_idx = -1;
        double best_dist = -1.0;
        
        for (int i = 0; i < n; ++i) {
            if (seed_set.count(i) == 0 && min_dist[i] > best_dist) {
                best_dist = min_dist[i];
                best_idx = i;
            }
        }
        
        if (best_idx == -1) {
            break;
        }
        
        seeds.push_back(best_idx);
        
        // Update min distances
        for (int i = 0; i < n; ++i) {
            if (location_matrix[best_idx][i] < min_dist[i]) {
                min_dist[i] = location_matrix[best_idx][i];
            }
        }
    }
    
    return seeds;
}

std::vector<int> Clustering::workload_aware_clustering(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    const WorkloadWeights& weights,
    double target_workload,
    const std::vector<int>& seed_indices) {
    
    int n = static_cast<int>(location_matrix.size());
    std::vector<int> assigned(n, -1);
    std::vector<double> workloads(num_teams, 0.0);
    std::vector<std::vector<int>> clusters(num_teams);
    
    // Priority queue: (distance, point, cluster)
    using PQEntry = std::tuple<double, int, int>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
    
    // Assign seeds
    for (size_t c = 0; c < seed_indices.size() && c < static_cast<size_t>(num_teams); ++c) {
        int s = seed_indices[c];
        assigned[s] = static_cast<int>(c);
        clusters[c].push_back(s);
        workloads[c] = weights.beta;  // 1 visit penalty
    }
    
    // Populate priority queue
    for (size_t c = 0; c < seed_indices.size() && c < static_cast<size_t>(num_teams); ++c) {
        int s = seed_indices[c];
        for (int p = 0; p < n; ++p) {
            if (assigned[p] == -1) {
                pq.push({location_matrix[s][p], p, static_cast<int>(c)});
            }
        }
    }
    
    // Greedy growth
    while (!pq.empty()) {
        auto [dist, p, c] = pq.top();
        pq.pop();
        
        if (assigned[p] != -1) {
            continue;
        }
        
        // Check capacity
        std::vector<int> new_locs = clusters[c];
        new_locs.push_back(p);
        double new_dist = team_route_distance(full_matrix, new_locs, "heuristic");
        double new_workload = workload(new_dist, static_cast<int>(new_locs.size()), weights);
        
        if (new_workload <= target_workload * 1.2) {
            assigned[p] = c;
            clusters[c].push_back(p);
            workloads[c] = new_workload;
            
            // Add edges from p
            for (int q = 0; q < n; ++q) {
                if (assigned[q] == -1) {
                    pq.push({location_matrix[p][q], q, c});
                }
            }
        }
    }
    
    // Assign remaining points to least loaded team
    for (int p = 0; p < n; ++p) {
        if (assigned[p] == -1) {
            int best_team = 0;
            double min_workload = workloads[0];
            
            for (int c = 1; c < num_teams; ++c) {
                if (workloads[c] < min_workload) {
                    min_workload = workloads[c];
                    best_team = c;
                }
            }
            
            assigned[p] = best_team;
            clusters[best_team].push_back(p);
            
            double new_dist = team_route_distance(full_matrix, clusters[best_team], "heuristic");
            workloads[best_team] = workload(new_dist, static_cast<int>(clusters[best_team].size()), weights);
        }
    }
    
    return assigned;
}

double Clustering::compute_variance(const std::vector<double>& workloads) {
    if (workloads.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(workloads.begin(), workloads.end(), 0.0);
    double mean = sum / workloads.size();
    
    double sq_sum = 0.0;
    for (double wl : workloads) {
        sq_sum += (wl - mean) * (wl - mean);
    }
    
    return sq_sum / workloads.size();
}

Coord Clustering::centroid_of(const std::vector<Coord>& coords,
                              const std::vector<int>& indices) {
    if (indices.empty()) {
        return {0.0, 0.0};
    }
    
    double lat_sum = 0.0;
    double lon_sum = 0.0;
    
    for (int idx : indices) {
        lat_sum += coords[idx].first;
        lon_sum += coords[idx].second;
    }
    
    return {lat_sum / indices.size(), lon_sum / indices.size()};
}

std::vector<int> Clustering::progressive_local_search(
    const std::vector<Coord>& location_coords,
    const Matrix& full_matrix,
    const std::vector<int>& initial_assignment,
    int num_teams,
    int n_locations,
    const WorkloadWeights& weights,
    int max_iterations) {
    
    std::vector<int> assignment = initial_assignment;
    
    // Build team points
    std::vector<std::vector<int>> team_points(num_teams);
    for (int i = 0; i < n_locations; ++i) {
        team_points[assignment[i]].push_back(i);
    }
    
    // Calculate initial workloads
    std::vector<double> workloads(num_teams);
    for (int c = 0; c < num_teams; ++c) {
        double dist = team_route_distance(full_matrix, team_points[c]);
        workloads[c] = workload(dist, static_cast<int>(team_points[c].size()), weights);
    }
    
    double best_variance = compute_variance(workloads);
    
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        bool improved = false;
        
        // Sort teams by workload
        std::vector<int> team_order(num_teams);
        std::iota(team_order.begin(), team_order.end(), 0);
        std::sort(team_order.begin(), team_order.end(),
                 [&](int a, int b) { return workloads[a] < workloads[b]; });
        
        // Focus on extremes
        std::vector<int> lightest(team_order.begin(), team_order.begin() + std::min(3, num_teams));
        std::vector<int> heaviest(team_order.end() - std::min(3, num_teams), team_order.end());
        
        // Try swaps between heavy and light teams
        for (int ti : heaviest) {
            for (int tj : lightest) {
                if (ti == tj) continue;
                
                int sample_limit_i = std::min(10, static_cast<int>(team_points[ti].size()));
                int sample_limit_j = std::min(10, static_cast<int>(team_points[tj].size()));
                
                for (int idx_i = 0; idx_i < sample_limit_i; ++idx_i) {
                    int i = team_points[ti][idx_i];
                    
                    for (int idx_j = 0; idx_j < sample_limit_j; ++idx_j) {
                        int j = team_points[tj][idx_j];
                        
                        // Compute new team compositions
                        std::vector<int> new_ti, new_tj;
                        for (int p : team_points[ti]) {
                            if (p != i) new_ti.push_back(p);
                        }
                        new_ti.push_back(j);
                        
                        for (int p : team_points[tj]) {
                            if (p != j) new_tj.push_back(p);
                        }
                        new_tj.push_back(i);
                        
                        double dist_ti = team_route_distance(full_matrix, new_ti, "heuristic");
                        double dist_tj = team_route_distance(full_matrix, new_tj, "heuristic");
                        
                        double wl_ti = workload(dist_ti, static_cast<int>(new_ti.size()), weights);
                        double wl_tj = workload(dist_tj, static_cast<int>(new_tj.size()), weights);
                        
                        std::vector<double> new_workloads = workloads;
                        new_workloads[ti] = wl_ti;
                        new_workloads[tj] = wl_tj;
                        
                        double new_variance = compute_variance(new_workloads);
                        
                        if (new_variance + 1e-6 < best_variance) {
                            // Apply swap
                            assignment[i] = tj;
                            assignment[j] = ti;
                            team_points[ti] = new_ti;
                            team_points[tj] = new_tj;
                            workloads = new_workloads;
                            best_variance = new_variance;
                            improved = true;
                            goto next_iteration;
                        }
                    }
                }
            }
        }
        
        next_iteration:
        if (!improved) {
            break;
        }
    }
    
    return assignment;
}

std::vector<int> Clustering::best_workload_clustering(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    int n_locations,
    const WorkloadWeights& weights,
    int num_restarts) {
    
    double target_wl = target_workload_per_team(location_coords, full_matrix,
                                                n_locations, num_teams, weights);
    
    std::cout << "    Target workload per team: ~" << target_wl
              << " (α=" << weights.alpha << ", β=" << weights.beta << ")" << std::endl;
    
    std::vector<int> best_assignment;
    double best_variance = std::numeric_limits<double>::infinity();
    
    for (int restart = 0; restart < num_restarts; ++restart) {
        Random rng(restart);
        
        std::vector<int> seed_indices = farthest_point_seeds(location_matrix, num_teams, rng);
        
        std::vector<int> assignment = workload_aware_clustering(
            location_coords, location_matrix, full_matrix, num_teams,
            weights, target_wl, seed_indices);
        
        assignment = progressive_local_search(
            location_coords, full_matrix, assignment, num_teams, n_locations,
            weights, 50);
        
        // Compute variance
        std::vector<std::vector<int>> clusters(num_teams);
        for (int i = 0; i < n_locations; ++i) {
            clusters[assignment[i]].push_back(i);
        }
        
        std::vector<double> workloads(num_teams);
        for (int c = 0; c < num_teams; ++c) {
            double dist = team_route_distance(full_matrix, clusters[c]);
            workloads[c] = workload(dist, static_cast<int>(clusters[c].size()), weights);
        }
        
        double variance = compute_variance(workloads);
        
        if (variance < best_variance) {
            best_variance = variance;
            best_assignment = assignment;
        }
    }
    
    return best_assignment;
}


// ============================================================================
// MODE-BASED CLUSTERING IMPLEMENTATIONS
// ============================================================================

std::vector<int> Clustering::cluster_by_mode(
    RoutingMode mode,
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    int n_locations,
    const WorkloadWeights& weights,
    int num_restarts) {
    
    switch (mode) {
        case RoutingMode::BALANCED:
            return best_workload_clustering(location_coords, location_matrix,
                                           full_matrix, num_teams, n_locations,
                                           weights, num_restarts);
        
        case RoutingMode::NEAREST:
            return cluster_nearest(location_coords, location_matrix, num_teams);
        
        case RoutingMode::FUEL_EFFICIENT:
            return cluster_fuel_efficient(location_coords, location_matrix,
                                         full_matrix, num_teams, n_locations,
                                         num_restarts);
        
        case RoutingMode::EQUAL_DISTANCE:
            return cluster_equal_distance(location_coords, location_matrix,
                                         full_matrix, num_teams, n_locations,
                                         num_restarts);
        
        case RoutingMode::COMPACT:
            return cluster_compact(location_coords, location_matrix, num_teams);
        
        case RoutingMode::BALANCED_DISTANCE:
            return cluster_balanced_distance(location_coords, location_matrix,
                                            full_matrix, num_teams, n_locations,
                                            num_restarts);
        
        default:
            return best_workload_clustering(location_coords, location_matrix,
                                           full_matrix, num_teams, n_locations,
                                           weights, num_restarts);
    }
}

// Mode: Nearest-neighbor clustering
std::vector<int> Clustering::cluster_nearest(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    int num_teams) {
    
    int n = static_cast<int>(location_coords.size());
    std::vector<int> assignment(n, -1);
    
    // Select team seeds (spread out)
    Random rng(42);
    std::vector<int> seeds = farthest_point_seeds(location_matrix, num_teams, rng);
    
    // Assign each location to nearest seed
    for (int i = 0; i < n; ++i) {
        int best_team = 0;
        double best_dist = std::numeric_limits<double>::infinity();
        
        for (int t = 0; t < num_teams; ++t) {
            double dist = location_matrix[i][seeds[t]];
            if (dist < best_dist) {
                best_dist = dist;
                best_team = t;
            }
        }
        
        assignment[i] = best_team;
    }
    
    return assignment;
}

// Mode: Fuel-efficient (minimize total distance)
std::vector<int> Clustering::cluster_fuel_efficient(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    int n_locations,
    int num_restarts) {
    
    std::vector<int> best_assignment;
    double best_total_distance = std::numeric_limits<double>::infinity();
    
    Random rng(42);
    
    for (int restart = 0; restart < num_restarts; ++restart) {
        // Random seeds
        std::vector<int> seeds = farthest_point_seeds(location_matrix, num_teams, rng);
        std::vector<int> assignment(n_locations, -1);
        std::vector<std::vector<int>> teams(num_teams);
        
        // Greedy assignment: minimize incremental distance
        std::vector<bool> assigned(n_locations, false);
        
        // Seed each team with one location so every team is used
        int n_teams_used = std::min(num_teams, static_cast<int>(seeds.size()));
        for (int t = 0; t < n_teams_used; ++t) {
            int s = seeds[t];
            assignment[s] = t;
            teams[t].push_back(s);
            assigned[s] = true;
        }
        
        for (int round = 0; round < n_locations; ++round) {
            int best_loc = -1;
            int best_team = -1;
            double best_increase = std::numeric_limits<double>::infinity();
            
            for (int i = 0; i < n_locations; ++i) {
                if (assigned[i]) continue;
                
                for (int t = 0; t < num_teams; ++t) {
                    // Calculate distance increase if we add location i to team t
                    double increase;
                    if (teams[t].empty()) {
                        // First location: just distance from depot
                        increase = full_matrix[0][i + 1] * 2; // round-trip
                    } else {
                        // Approximate: current route + new location
                        std::vector<int> temp_team = teams[t];
                        temp_team.push_back(i);
                        double new_dist = team_route_distance(full_matrix, temp_team);
                        double old_dist = team_route_distance(full_matrix, teams[t]);
                        increase = new_dist - old_dist;
                    }
                    
                    if (increase < best_increase) {
                        best_increase = increase;
                        best_loc = i;
                        best_team = t;
                    }
                }
            }
            
            if (best_loc != -1) {
                assignment[best_loc] = best_team;
                teams[best_team].push_back(best_loc);
                assigned[best_loc] = true;
            }
        }
        
        // Calculate total distance
        double total_distance = 0.0;
        for (int t = 0; t < num_teams; ++t) {
            if (!teams[t].empty()) {
                total_distance += team_route_distance(full_matrix, teams[t]);
            }
        }
        
        if (total_distance < best_total_distance) {
            best_total_distance = total_distance;
            best_assignment = assignment;
        }
    }
    
    return best_assignment;
}

// Mode: Equal-distance per team
std::vector<int> Clustering::cluster_equal_distance(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    int n_locations,
    int num_restarts) {
    
    std::vector<int> best_assignment;
    double best_variance = std::numeric_limits<double>::infinity();
    
    Random rng(42);
    
    for (int restart = 0; restart < num_restarts; ++restart) {
        std::vector<int> seeds = farthest_point_seeds(location_matrix, num_teams, rng);
        std::vector<int> assignment(n_locations, -1);
        std::vector<std::vector<int>> teams(num_teams);
        std::vector<double> team_distances(num_teams, 0.0);
        
        // Greedy balancing: assign to team with minimum current distance
        std::vector<bool> assigned(n_locations, false);
        
        for (int round = 0; round < n_locations; ++round) {
            int best_loc = -1;
            int best_team = -1;
            double best_balance = std::numeric_limits<double>::infinity();
            
            for (int i = 0; i < n_locations; ++i) {
                if (assigned[i]) continue;
                
                for (int t = 0; t < num_teams; ++t) {
                    // Calculate new distance if we add this location
                    std::vector<int> temp_team = teams[t];
                    temp_team.push_back(i);
                    double new_dist = team_route_distance(full_matrix, temp_team);
                    
                    // How far from average?
                    double avg_target = (best_variance == std::numeric_limits<double>::infinity()) ?
                                       0.0 : team_distances[t];
                    double balance_score = std::abs(new_dist - avg_target);
                    
                    if (balance_score < best_balance) {
                        best_balance = balance_score;
                        best_loc = i;
                        best_team = t;
                    }
                }
            }
            
            if (best_loc != -1) {
                assignment[best_loc] = best_team;
                teams[best_team].push_back(best_loc);
                team_distances[best_team] = team_route_distance(full_matrix, teams[best_team]);
                assigned[best_loc] = true;
            }
        }
        
        // Calculate variance of distances
        double variance = compute_variance(team_distances);
        
        if (variance < best_variance) {
            best_variance = variance;
            best_assignment = assignment;
        }
    }
    
    return best_assignment;
}

// Mode: Compact geographic clustering
std::vector<int> Clustering::cluster_compact(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    int num_teams) {
    
    int n = static_cast<int>(location_coords.size());
    std::vector<int> assignment(n, -1);
    
    // K-means style clustering for geographic compactness
    Random rng(42);
    std::vector<int> seeds = farthest_point_seeds(location_matrix, num_teams, rng);
    std::vector<Coord> centroids(num_teams);
    
    // Initialize centroids with seed locations
    for (int t = 0; t < num_teams; ++t) {
        centroids[t] = location_coords[seeds[t]];
    }
    
    // Iterate until convergence
    for (int iter = 0; iter < 20; ++iter) {
        bool changed = false;
        
        // Assign to nearest centroid
        for (int i = 0; i < n; ++i) {
            int best_team = 0;
            double best_dist = haversine_km(location_coords[i], centroids[0]);
            
            for (int t = 1; t < num_teams; ++t) {
                double dist = haversine_km(location_coords[i], centroids[t]);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_team = t;
                }
            }
            
            if (assignment[i] != best_team) {
                assignment[i] = best_team;
                changed = true;
            }
        }
        
        if (!changed) break;
        
        // Update centroids
        for (int t = 0; t < num_teams; ++t) {
            std::vector<int> team_locs;
            for (int i = 0; i < n; ++i) {
                if (assignment[i] == t) {
                    team_locs.push_back(i);
                }
            }
            
            if (!team_locs.empty()) {
                centroids[t] = centroid_of(location_coords, team_locs);
            }
        }
    }
    
    return assignment;
}

// Mode: Balanced distance (equal weight to distance and visits)
std::vector<int> Clustering::cluster_balanced_distance(
    const std::vector<Coord>& location_coords,
    const Matrix& location_matrix,
    const Matrix& full_matrix,
    int num_teams,
    int n_locations,
    int num_restarts) {
    
    // Use workload balancing but with equal weights
    WorkloadWeights equal_weights(1.0, 1.0);
    
    return best_workload_clustering(location_coords, location_matrix,
                                   full_matrix, num_teams, n_locations,
                                   equal_weights, num_restarts);
}

} // namespace RoutePlanner
