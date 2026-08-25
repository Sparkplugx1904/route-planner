#pragma once

#include "types.hpp"
#include "utils.hpp"
#include <vector>

namespace RoutePlanner {

struct WorkloadWeights {
    double alpha;  // distance weight
    double beta;   // visit count weight
    
    WorkloadWeights(double a = 1.0, double b = 5.0) : alpha(a), beta(b) {}
};

class Clustering {
public:
    // Estimate workload weights based on data characteristics
    static WorkloadWeights estimate_weights(const std::vector<Coord>& location_coords,
                                           const Matrix& full_matrix,
                                           int n_locations,
                                           int num_teams,
                                           int sample_size = 20);
    
    // Calculate workload for a team
    static double workload(double distance_km, int num_visits,
                          const WorkloadWeights& weights);
    
    // Estimate target workload per team
    static double target_workload_per_team(const std::vector<Coord>& location_coords,
                                          const Matrix& full_matrix,
                                          int n_locations,
                                          int num_teams,
                                          const WorkloadWeights& weights);
    
    // Find farthest-point seeds for clustering
    static std::vector<int> farthest_point_seeds(const Matrix& location_matrix,
                                                 int num_teams,
                                                 Random& rng);
    
    // Workload-aware greedy clustering
    static std::vector<int> workload_aware_clustering(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        const WorkloadWeights& weights,
        double target_workload,
        const std::vector<int>& seed_indices);
    
    // Progressive local search to balance workload
    static std::vector<int> progressive_local_search(
        const std::vector<Coord>& location_coords,
        const Matrix& full_matrix,
        const std::vector<int>& initial_assignment,
        int num_teams,
        int n_locations,
        const WorkloadWeights& weights,
        int max_iterations = 100);
    
    // Multi-restart clustering to find best solution
    static std::vector<int> best_workload_clustering(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        int n_locations,
        const WorkloadWeights& weights,
        int num_restarts = 10);
    
    // Main dispatcher: route by mode
    static std::vector<int> cluster_by_mode(
        RoutingMode mode,
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        int n_locations,
        const WorkloadWeights& weights,
        int num_restarts = 10);
    
    // Mode: Nearest-neighbor clustering
    static std::vector<int> cluster_nearest(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        int num_teams);
    
    // Mode: Fuel-efficient (minimize total distance)
    static std::vector<int> cluster_fuel_efficient(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        int n_locations,
        int num_restarts = 10);
    
    // Mode: Equal-distance per team
    static std::vector<int> cluster_equal_distance(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        int n_locations,
        int num_restarts = 10);
    
    // Mode: Compact geographic clustering
    static std::vector<int> cluster_compact(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        int num_teams);
    
    // Mode: Balanced distance (equal weight to distance and visits)
    static std::vector<int> cluster_balanced_distance(
        const std::vector<Coord>& location_coords,
        const Matrix& location_matrix,
        const Matrix& full_matrix,
        int num_teams,
        int n_locations,
        int num_restarts = 10);

private:
    // Helper: calculate team route distance
    static double team_route_distance(const Matrix& full_matrix,
                                     const std::vector<int>& team_locations,
                                     const std::string& algorithm = "auto");
    
    // Helper: compute workload variance
    static double compute_variance(const std::vector<double>& workloads);
    
    // Helper: centroid of a set of coordinates
    static Coord centroid_of(const std::vector<Coord>& coords,
                            const std::vector<int>& indices);
};

} // namespace RoutePlanner
