#include "osrm_client.hpp"
#include "utils.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <future>
#include <algorithm>

// Define Windows version before including httplib.h to avoid version check error
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10
#endif
#endif

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

namespace RoutePlanner {

OSRMClient::OSRMClient(const std::string& base_url, const std::string& cache_dir)
    : base_url_(base_url), cache_dir_(cache_dir) {
    std::filesystem::create_directories(cache_dir_);
}

std::string OSRMClient::get_cache_key(const std::vector<Coord>& coords, 
                                     const std::string& profile) {
    return coords_hash(coords) + "_" + profile;
}

bool OSRMClient::load_from_cache(const std::string& key, Matrix& matrix) {
    std::filesystem::path cache_file = cache_dir_ / (key + ".cache");
    
    if (!std::filesystem::exists(cache_file)) {
        return false;
    }
    
    std::ifstream file(cache_file, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        // Read matrix dimensions
        size_t rows, cols;
        file.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        file.read(reinterpret_cast<char*>(&cols), sizeof(cols));
        
        // Read matrix data
        matrix.resize(rows, std::vector<double>(cols));
        for (size_t i = 0; i < rows; ++i) {
            file.read(reinterpret_cast<char*>(matrix[i].data()), 
                     cols * sizeof(double));
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void OSRMClient::save_to_cache(const std::string& key, const Matrix& matrix) {
    std::filesystem::path cache_file = cache_dir_ / (key + ".cache");
    
    std::ofstream file(cache_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Warning] Cannot write cache file: " << cache_file << std::endl;
        return;
    }
    
    // Write matrix dimensions
    size_t rows = matrix.size();
    size_t cols = matrix.empty() ? 0 : matrix[0].size();
    file.write(reinterpret_cast<const char*>(&rows), sizeof(rows));
    file.write(reinterpret_cast<const char*>(&cols), sizeof(cols));
    
    // Write matrix data
    for (const auto& row : matrix) {
        file.write(reinterpret_cast<const char*>(row.data()), 
                  cols * sizeof(double));
    }
}

std::optional<std::string> OSRMClient::http_get(const std::string& url, 
                                                int timeout_seconds) {
    try {
        // Parse URL
        std::string scheme, host, path;
        size_t scheme_end = url.find("://");
        if (scheme_end != std::string::npos) {
            scheme = url.substr(0, scheme_end);
            size_t host_start = scheme_end + 3;
            size_t path_start = url.find('/', host_start);
            
            if (path_start != std::string::npos) {
                host = url.substr(host_start, path_start - host_start);
                path = url.substr(path_start);
            } else {
                host = url.substr(host_start);
                path = "/";
            }
        } else {
            // No scheme, assume http://
            size_t path_start = url.find('/');
            if (path_start != std::string::npos) {
                host = url.substr(0, path_start);
                path = url.substr(path_start);
            } else {
                host = url;
                path = "/";
            }
            scheme = "http";
        }
        
        // Create HTTP client
        httplib::Client cli(scheme + "://" + host);
        cli.set_read_timeout(timeout_seconds, 0);
        cli.set_connection_timeout(timeout_seconds, 0);
        
        // Make GET request
        auto res = cli.Get(path.c_str());
        
        if (res && res->status == 200) {
            return res->body;
        } else {
            if (res) {
                std::cerr << "[Warning] HTTP GET failed: " << res->status << std::endl;
            } else {
                std::cerr << "[Warning] HTTP GET failed: connection error" << std::endl;
            }
            return std::nullopt;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Warning] HTTP GET exception: " << e.what() << std::endl;
        return std::nullopt;
    }
}

bool OSRMClient::fetch_chunk(const std::vector<Coord>& all_coords,
                             const std::vector<int>& source_indices,
                             const std::string& profile,
                             int timeout_seconds,
                             std::vector<std::vector<double>>& result) {
    try {
        // Build coordinates string: "lon1,lat1;lon2,lat2;..."
        std::ostringstream coords_str;
        for (size_t i = 0; i < all_coords.size(); ++i) {
            if (i > 0) coords_str << ";";
            coords_str << std::fixed << std::setprecision(6) 
                      << all_coords[i].second << "," << all_coords[i].first;
        }
        
        // Build sources string: "0;1;2;..."
        std::ostringstream sources_str;
        for (size_t i = 0; i < source_indices.size(); ++i) {
            if (i > 0) sources_str << ";";
            sources_str << source_indices[i];
        }
        
        // Build OSRM table API URL
        std::string url = base_url_ + "/table/v1/" + profile + "/" + 
                         coords_str.str() + "?sources=" + sources_str.str();
        
        // Fetch from OSRM
        auto response = http_get(url, timeout_seconds);
        if (!response.has_value()) {
            // Fallback to haversine
            std::cerr << "[Warning] OSRM fetch failed, using haversine fallback" << std::endl;
            size_t n = all_coords.size();
            result.resize(source_indices.size(), std::vector<double>(n));
            
            for (size_t i = 0; i < source_indices.size(); ++i) {
                int src = source_indices[i];
                for (size_t j = 0; j < n; ++j) {
                    if (src == static_cast<int>(j)) {
                        result[i][j] = 0.0;
                    } else {
                        result[i][j] = haversine_km(all_coords[src], all_coords[j]) * 1.3;
                    }
                }
            }
            return true;
        }
        
        // Parse JSON response
        json j = json::parse(response.value());
        
        if (j["code"] != "Ok") {
            std::cerr << "[Warning] OSRM error: " << j["code"] << std::endl;
            return false;
        }
        
        // Extract distance matrix (distances are in meters, convert to km)
        result.resize(source_indices.size());
        
        // Check if distances are available (preferred) or use durations
        bool has_distances = j.contains("distances");
        auto data = has_distances ? j["distances"] : j["durations"];
        
        for (size_t i = 0; i < source_indices.size(); ++i) {
            result[i].resize(all_coords.size());
            for (size_t j_idx = 0; j_idx < all_coords.size(); ++j_idx) {
                if (has_distances) {
                    // Distance in meters, convert to km
                    double distance_meters = data[i][j_idx];
                    result[i][j_idx] = distance_meters / 1000.0;
                } else {
                    // Duration in seconds, estimate distance assuming 40 km/h
                    double duration_seconds = data[i][j_idx];
                    result[i][j_idx] = (duration_seconds / 3600.0) * 40.0;
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Error] fetch_chunk exception: " << e.what() << std::endl;
        return false;
    }
}

std::optional<Matrix> OSRMClient::fetch_matrix(const std::vector<Coord>& coords,
                                              const std::string& profile,
                                              int chunk_size,
                                              int timeout_seconds) {
    std::string cache_key = get_cache_key(coords, profile);
    
    // Try to load from cache
    Matrix matrix;
    if (load_from_cache(cache_key, matrix)) {
        std::cout << "    -> Using cached matrix" << std::endl;
        return matrix;
    }
    
    std::cout << "    -> Fetching from OSRM (or fallback to haversine)..." << std::endl;
    
    size_t n = coords.size();
    matrix.resize(n, std::vector<double>(n, 0.0));
    
    // Create chunks
    std::vector<std::vector<int>> chunks;
    for (size_t start = 0; start < n; start += chunk_size) {
        std::vector<int> chunk;
        for (size_t i = start; i < std::min(start + chunk_size, n); ++i) {
            chunk.push_back(static_cast<int>(i));
        }
        chunks.push_back(chunk);
    }
    
    // Fetch chunks (could be parallelized)
    for (const auto& chunk : chunks) {
        std::vector<std::vector<double>> chunk_result;
        if (!fetch_chunk(coords, chunk, profile, timeout_seconds, chunk_result)) {
            std::cerr << "[Error] Failed to fetch chunk" << std::endl;
            return std::nullopt;
        }
        
        // Copy chunk result to matrix
        for (size_t i = 0; i < chunk.size(); ++i) {
            matrix[chunk[i]] = chunk_result[i];
        }
    }
    
    // Save to cache
    save_to_cache(cache_key, matrix);
    
    return matrix;
}

// Decode polyline encoded string to coordinates
// Based on Google's Polyline Algorithm Format
std::vector<Coord> decode_polyline(const std::string& encoded) {
    std::vector<Coord> coords;
    size_t index = 0;
    int lat = 0, lng = 0;
    
    while (index < encoded.size()) {
        int b, shift = 0, result = 0;
        do {
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20);
        int dlat = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lat += dlat;
        
        shift = 0;
        result = 0;
        do {
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20);
        int dlng = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lng += dlng;
        
        coords.push_back({lat * 1e-5, lng * 1e-5});
    }
    
    return coords;
}

std::optional<std::vector<Coord>> OSRMClient::fetch_geometry(const Coord& from,
                                                             const Coord& to,
                                                             const std::string& profile,
                                                             int timeout_seconds) {
    try {
        // Build OSRM route API URL
        std::ostringstream url_stream;
        url_stream << base_url_ << "/route/v1/" << profile << "/"
                  << std::fixed << std::setprecision(6)
                  << from.second << "," << from.first << ";"
                  << to.second << "," << to.first
                  << "?overview=full&geometries=polyline";
        
        std::string url = url_stream.str();
        
        // Fetch from OSRM
        auto response = http_get(url, timeout_seconds);
        if (!response.has_value()) {
            // Fallback to straight line
            return std::vector<Coord>{from, to};
        }
        
        // Parse JSON response
        json j = json::parse(response.value());
        
        if (j["code"] != "Ok") {
            return std::vector<Coord>{from, to};
        }
        
        // Extract geometry (polyline encoded)
        std::string geometry = j["routes"][0]["geometry"];
        
        // Decode polyline
        return decode_polyline(geometry);
        
    } catch (const std::exception& e) {
        std::cerr << "[Warning] fetch_geometry exception: " << e.what() << std::endl;
        return std::vector<Coord>{from, to};
    }
}

} // namespace RoutePlanner
