#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace RoutePlanner {

class OSRMClient {
public:
    explicit OSRMClient(const std::string& base_url = "http://router.project-osrm.org",
                       const std::string& cache_dir = ".osrm_cache");
    
    // Fetch distance matrix from OSRM (with caching)
    std::optional<Matrix> fetch_matrix(const std::vector<Coord>& coords,
                                      const std::string& profile = "driving",
                                      int chunk_size = 60,
                                      int timeout_seconds = 30);
    
    // Fetch route geometry between two points
    std::optional<std::vector<Coord>> fetch_geometry(const Coord& from,
                                                     const Coord& to,
                                                     const std::string& profile = "driving",
                                                     int timeout_seconds = 15);

private:
    std::string base_url_;
    std::filesystem::path cache_dir_;
    
    // Cache management
    std::string get_cache_key(const std::vector<Coord>& coords, const std::string& profile);
    bool load_from_cache(const std::string& key, Matrix& matrix);
    void save_to_cache(const std::string& key, const Matrix& matrix);
    
    // HTTP utilities (simplified, will use httplib in actual implementation)
    std::optional<std::string> http_get(const std::string& url, int timeout_seconds);
    
    // Fetch a single chunk of the distance matrix.
    // used_fallback diset true bila chunk gagal dan diisi haversine (tidak boleh dipersist).
    bool fetch_chunk(const std::vector<Coord>& all_coords,
                    const std::vector<int>& source_indices,
                    const std::string& profile,
                    int timeout_seconds,
                    std::vector<std::vector<double>>& result,
                    bool& used_fallback);
};

} // namespace RoutePlanner
