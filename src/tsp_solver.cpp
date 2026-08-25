#include "tsp_solver.hpp"
#include "utils.hpp"
#include <algorithm>
#include <limits>
#include <iostream>

namespace RoutePlanner {

// Static cache initialization
std::map<std::pair<int, std::vector<int>>, std::pair<std::vector<int>, double>> TSPSolver::cache_;

void TSPSolver::clear_cache() {
    cache_.clear();
}

TSPResult TSPSolver::solve(const Matrix& matrix,
                           const std::vector<int>& indices,
                           const std::string& force_algorithm) {
    if (indices.size() <= 1) {
        return TSPResult({0}, 0.0);
    }
    
    // Check cache
    std::vector<int> non_depot(indices.begin() + 1, indices.end());
    std::sort(non_depot.begin(), non_depot.end());
    auto cache_key = std::make_pair(indices[0], non_depot);
    
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) {
        // Convert cached global order to local order
        const auto& global_order = it->second.first;
        double dist = it->second.second;
        
        std::map<int, int> global_to_local;
        for (size_t i = 0; i < indices.size(); ++i) {
            global_to_local[indices[i]] = static_cast<int>(i);
        }
        
        std::vector<int> local_order;
        for (int global_idx : global_order) {
            local_order.push_back(global_to_local[global_idx]);
        }
        
        return TSPResult(local_order, dist);
    }
    
    // Choose algorithm
    int m = static_cast<int>(indices.size()) - 1;  // non-depot count
    TSPResult result;
    
    if (force_algorithm == "exact" || 
        (force_algorithm == "auto" && m <= EXACT_TSP_MAX)) {
        result = solve_exact_dp(matrix, indices);
    } else {
        result = solve_nn_2opt(matrix, indices);
    }
    
    // Cache result (convert local order to global)
    std::vector<int> global_order;
    for (int local_idx : result.order) {
        global_order.push_back(indices[local_idx]);
    }
    cache_[cache_key] = {global_order, result.distance};
    
    return result;
}

TSPResult TSPSolver::solve_exact_dp(const Matrix& matrix, const std::vector<int>& indices) {
    int n = static_cast<int>(indices.size());
    int m = n - 1;  // non-depot count
    
    if (m == 0) {
        return TSPResult({0}, 0.0);
    }
    
    const double INF = std::numeric_limits<double>::infinity();
    
    // dp[mask][j] = min distance to reach node j with visited set = mask
    int full_mask = (1 << m) - 1;
    std::vector<std::vector<double>> dp(1 << m, std::vector<double>(m, INF));
    std::vector<std::vector<int>> parent(1 << m, std::vector<int>(m, -1));
    
    // Initialize: single-node paths from depot
    for (int j = 0; j < m; ++j) {
        int mask = 1 << j;
        dp[mask][j] = matrix[indices[0]][indices[j + 1]];
    }
    
    // Precompute distance submatrix for non-depot nodes
    std::vector<std::vector<double>> dmat(m, std::vector<double>(m));
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < m; ++j) {
            dmat[i][j] = matrix[indices[i + 1]][indices[j + 1]];
        }
    }
    
    // DP: iterate over all subsets
    for (int mask = 1; mask < (1 << m); ++mask) {
        for (int j = 0; j < m; ++j) {
            if (!(mask & (1 << j)) || dp[mask][j] == INF) {
                continue;
            }
            
            double cur_dist = dp[mask][j];
            
            // Extend to unvisited nodes
            for (int k = 0; k < m; ++k) {
                if (mask & (1 << k)) {
                    continue;
                }
                
                int new_mask = mask | (1 << k);
                double new_dist = cur_dist + dmat[j][k];
                
                if (new_dist < dp[new_mask][k]) {
                    dp[new_mask][k] = new_dist;
                    parent[new_mask][k] = j;
                }
            }
        }
    }
    
    // Find best ending node (must return to depot)
    double best_dist = INF;
    int best_last = -1;
    
    for (int j = 0; j < m; ++j) {
        double total = dp[full_mask][j] + matrix[indices[j + 1]][indices[0]];
        if (total < best_dist) {
            best_dist = total;
            best_last = j;
        }
    }
    
    if (best_last == -1) {
        // Fallback: just return depot
        return TSPResult({0}, 0.0);
    }
    
    // Reconstruct path
    std::vector<int> path_rest;
    int mask = full_mask;
    int j = best_last;
    
    while (j != -1) {
        path_rest.push_back(j + 1);  // +1 for local index (depot = 0)
        int prev_j = parent[mask][j];
        if (prev_j != -1) {
            mask ^= (1 << j);
        }
        j = prev_j;
    }
    
    std::reverse(path_rest.begin(), path_rest.end());
    
    // Build full order: depot -> path -> depot
    std::vector<int> order = {0};
    order.insert(order.end(), path_rest.begin(), path_rest.end());
    order.push_back(0);
    
    return TSPResult(order, best_dist);
}

TSPResult TSPSolver::solve_nn_2opt(const Matrix& matrix, const std::vector<int>& indices,
                                   int num_iterations) {
    int n = static_cast<int>(indices.size());
    
    // Nearest neighbor construction
    std::vector<bool> visited(n, false);
    std::vector<int> order;
    order.push_back(0);
    visited[0] = true;
    
    for (int step = 1; step < n; ++step) {
        int last = order.back();
        int best_next = -1;
        double best_dist = std::numeric_limits<double>::infinity();
        
        for (int j = 1; j < n; ++j) {
            if (!visited[j]) {
                double dist = matrix[indices[last]][indices[j]];
                if (dist < best_dist) {
                    best_dist = dist;
                    best_next = j;
                }
            }
        }
        
        if (best_next != -1) {
            order.push_back(best_next);
            visited[best_next] = true;
        }
    }
    
    // Close tour
    order.push_back(0);
    
    // 2-opt improvement
    improve_2opt(matrix, indices, order, num_iterations);
    
    double total_dist = route_length(matrix, indices, order);
    return TSPResult(order, total_dist);
}

void TSPSolver::improve_2opt(const Matrix& matrix, const std::vector<int>& indices,
                             std::vector<int>& order, int num_iterations) {
    int n = static_cast<int>(order.size());
    
    for (int iter = 0; iter < num_iterations; ++iter) {
        bool improved = false;
        
        for (int i = 1; i < n - 2; ++i) {
            for (int j = i + 1; j < n - 1; ++j) {
                int a = indices[order[i - 1]];
                int b = indices[order[i]];
                int c = indices[order[j]];
                int d = indices[order[j + 1]];
                
                double before = matrix[a][b] + matrix[c][d];
                double after = matrix[a][c] + matrix[b][d];
                
                if (after + 1e-9 < before) {
                    // Reverse segment [i, j]
                    std::reverse(order.begin() + i, order.begin() + j + 1);
                    improved = true;
                }
            }
        }
        
        if (!improved) {
            break;
        }
    }
}

} // namespace RoutePlanner
