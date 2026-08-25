#pragma once

#include "types.hpp"
#include <vector>
#include <utility>
#include <map>

namespace RoutePlanner {

// TSP algorithm thresholds
constexpr int EXACT_TSP_MAX = 10;
constexpr int CHRISTOFIDES_MAX = 16;

struct TSPResult {
    std::vector<int> order;  // Order of indices (local to input indices)
    double distance;
    
    TSPResult() : distance(0.0) {}
    TSPResult(const std::vector<int>& o, double d) : order(o), distance(d) {}
};

class TSPSolver {
public:
    // Solve TSP for given indices (indices[0] = depot, forced start/end)
    // Returns order (local indices) and total distance
    static TSPResult solve(const Matrix& matrix, 
                          const std::vector<int>& indices,
                          const std::string& force_algorithm = "auto");
    
    // Clear internal cache
    static void clear_cache();

private:
    // Cache: key = (depot, sorted non-depot indices), value = (global order, distance)
    static std::map<std::pair<int, std::vector<int>>, std::pair<std::vector<int>, double>> cache_;
    
    // Exact solver: Held-Karp DP (O(n^2 * 2^n))
    static TSPResult solve_exact_dp(const Matrix& matrix, const std::vector<int>& indices);
    
    // Approximation: Nearest Neighbor + 2-opt (fast heuristic)
    static TSPResult solve_nn_2opt(const Matrix& matrix, const std::vector<int>& indices,
                                   int num_iterations = 500);
    
    // 2-opt improvement
    static void improve_2opt(const Matrix& matrix, const std::vector<int>& indices,
                            std::vector<int>& order, int num_iterations);
};

} // namespace RoutePlanner
