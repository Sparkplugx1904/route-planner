#pragma once

#include "types.hpp"
#include <vector>
#include <string>

namespace RoutePlanner {

class CSVParser {
public:
    // Load locations from CSV file
    // Supports various column names: location/nama/name, latitude/lat, longitude/lon/lng
    // Tolerates extra whitespace, quotes, and empty lines
    static std::vector<Location> load_locations(const std::string& csv_path);

    // Parse locations from CSV content string (shared with WASM build)
    static std::vector<Location> parse_locations_from_string(
        const std::string& csv_content, const std::string& source_name = "CSV");

private:
    static std::string trim(const std::string& str);
    static std::string to_lower(const std::string& str);
    static std::vector<std::string> parse_line(const std::string& line);
    static int find_column(const std::vector<std::string>& header,
                          const std::vector<std::string>& candidates);
};

} // namespace RoutePlanner
