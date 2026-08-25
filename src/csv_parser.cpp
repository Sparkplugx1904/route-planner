#include "csv_parser.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>

namespace RoutePlanner {

std::string CSVParser::trim(const std::string& str) {
    size_t start = 0;
    size_t end = str.length();
    
    while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    
    std::string result = str.substr(start, end - start);
    
    // Remove quotes
    if (!result.empty() && result.front() == '"') {
        result = result.substr(1);
    }
    if (!result.empty() && result.back() == '"') {
        result.pop_back();
    }
    
    return result;
}

std::string CSVParser::to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<std::string> CSVParser::parse_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    
    fields.push_back(trim(field));
    return fields;
}

int CSVParser::find_column(const std::vector<std::string>& header,
                           const std::vector<std::string>& candidates) {
    for (size_t i = 0; i < header.size(); ++i) {
        std::string col = to_lower(trim(header[i]));
        for (const auto& candidate : candidates) {
            if (col == candidate) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::vector<Location> CSVParser::load_locations(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + csv_path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return parse_locations_from_string(buffer.str(), csv_path);
}

std::vector<Location> CSVParser::parse_locations_from_string(
    const std::string& csv_content, const std::string& source_name) {
    std::vector<Location> locations;
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(csv_content);
    
    // Read all non-empty lines
    while (std::getline(stream, line)) {
        // Trim line (strip BOM if present on first line)
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            if (lines.empty() && trimmed.size() >= 3 &&
                static_cast<unsigned char>(trimmed[0]) == 0xEF &&
                static_cast<unsigned char>(trimmed[1]) == 0xBB &&
                static_cast<unsigned char>(trimmed[2]) == 0xBF) {
                trimmed = trimmed.substr(3);
            }
            lines.push_back(trimmed);
        }
    }
    
    if (lines.empty()) {
        throw std::runtime_error("CSV " + source_name + " is empty");
    }
    
    // Parse header
    std::vector<std::string> header = parse_line(lines[0]);
    
    // Find column indices
    std::vector<std::string> name_candidates = {
        "location", "lokasi", "sekolah", "school", "nama", "name", "place", "site"
    };
    std::vector<std::string> lat_candidates = {"latitude", "lat"};
    std::vector<std::string> lon_candidates = {"longitude", "lon", "lng"};
    
    int name_idx = find_column(header, name_candidates);
    int lat_idx = find_column(header, lat_candidates);
    int lon_idx = find_column(header, lon_candidates);
    
    size_t data_start = 1;
    
    // If header not recognized, assume first line is data
    if (name_idx == -1 || lat_idx == -1 || lon_idx == -1) {
        name_idx = 0;
        lat_idx = 1;
        lon_idx = 2;
        data_start = 0;
    }
    
    // Parse data rows
    for (size_t row = data_start; row < lines.size(); ++row) {
        std::vector<std::string> fields = parse_line(lines[row]);
        
        int max_idx = std::max({name_idx, lat_idx, lon_idx});
        if (static_cast<int>(fields.size()) <= max_idx) {
            std::cerr << "[Warning] Row " << (row + 1) << " skipped (insufficient columns)" << std::endl;
            continue;
        }
        
        std::string name = trim(fields[name_idx]);
        if (name.empty()) {
            continue;
        }
        
        try {
            double lat = std::stod(trim(fields[lat_idx]));
            double lon = std::stod(trim(fields[lon_idx]));
            
            locations.emplace_back(name, lat, lon);
        } catch (const std::exception& e) {
            std::cerr << "[Warning] Row " << (row + 1) << " skipped (invalid lat/lon): " 
                      << e.what() << std::endl;
        }
    }
    
    if (locations.empty()) {
        throw std::runtime_error("No valid locations found in " + source_name);
    }
    
    return locations;
}

} // namespace RoutePlanner
