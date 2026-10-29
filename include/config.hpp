#pragma once

#include "types.hpp"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace isp {

class Config {
public:
    Config() = default;

    // Load configurations
    bool load_from_file(const std::string& filepath);
    bool load_from_string(const std::string& content);

    // Query values
    bool has(const std::string& key) const;
    std::string get_string(const std::string& key, const std::string& default_val = "") const;
    int64_t get_int(const std::string& key, int64_t default_val = 0) const;
    double get_double(const std::string& key, double default_val = 0.0) const;
    bool get_bool(const std::string& key, bool default_val = false) const;

    // Set/update values
    void set(const std::string& key, const std::string& val);

    // Export to ResourcePolicy struct
    ResourcePolicy to_resource_policy() const;

    // Direct access to key-value map
    const std::unordered_map<std::string, std::string>& get_all() const { return entries_; }

    // Utilities
    static std::string trim(const std::string& s);

private:
    std::unordered_map<std::string, std::string> entries_;

    static std::string strip_comment(const std::string& s);
    static bool parse_bool(const std::string& s, bool default_val);
};

} // namespace isp
