#include "config.hpp"
#include "logger.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace isp {

std::string Config::trim(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string Config::strip_comment(const std::string& s) {
    size_t hash_pos = s.find('#');
    size_t semi_pos = s.find(';');
    size_t comment_pos = std::min(hash_pos, semi_pos);
    if (comment_pos != std::string::npos) {
        return s.substr(0, comment_pos);
    }
    return s;
}

bool Config::parse_bool(const std::string& s, bool default_val) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    return default_val;
}

bool Config::load_from_string(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        std::string clean = trim(strip_comment(line));
        if (clean.empty()) {
            continue;
        }
        size_t delimiter_pos = clean.find('=');
        if (delimiter_pos == std::string::npos) {
            continue; // Skip lines without '='
        }
        std::string key = trim(clean.substr(0, delimiter_pos));
        std::string val = trim(clean.substr(delimiter_pos + 1));
        if (!key.empty()) {
            entries_[key] = val;
        }
    }
    return true;
}

bool Config::load_from_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        ISP_LOG_WARN("Unable to open config file: " << filepath);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return load_from_string(buffer.str());
}

bool Config::has(const std::string& key) const {
    return entries_.find(key) != entries_.end();
}

std::string Config::get_string(const std::string& key, const std::string& default_val) const {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return it->second;
    }
    return default_val;
}

int64_t Config::get_int(const std::string& key, int64_t default_val) const {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        try {
            return std::stoll(it->second);
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
}

double Config::get_double(const std::string& key, double default_val) const {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        try {
            return std::stod(it->second);
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
}

bool Config::get_bool(const std::string& key, bool default_val) const {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return parse_bool(it->second, default_val);
    }
    return default_val;
}

void Config::set(const std::string& key, const std::string& val) {
    entries_[key] = val;
}

ResourcePolicy Config::to_resource_policy() const {
    ResourcePolicy p;
    p.protected_cpu_weight = static_cast<uint32_t>(get_int("protected_cpu_weight", p.protected_cpu_weight));
    p.protected_io_weight = static_cast<uint32_t>(get_int("protected_io_weight", p.protected_io_weight));
    p.background_cpu_weight = static_cast<uint32_t>(get_int("background_cpu_weight", p.background_cpu_weight));
    p.starvation_floor_cpu_weight = static_cast<uint32_t>(get_int("starvation_floor_cpu_weight", p.starvation_floor_cpu_weight));
    p.aging_increment = static_cast<uint32_t>(get_int("aging_increment", p.aging_increment));
    p.aging_interval_sec = static_cast<uint32_t>(get_int("aging_interval_sec", p.aging_interval_sec));
    p.aging_ceiling_cpu_weight = static_cast<uint32_t>(get_int("aging_ceiling_cpu_weight", p.aging_ceiling_cpu_weight));
    p.background_memory_high_percent = static_cast<uint32_t>(get_int("background_memory_high_percent", p.background_memory_high_percent));
    p.poll_interval_ms = static_cast<uint32_t>(get_int("poll_interval_ms", p.poll_interval_ms));
    p.hysteresis_delay_sec = static_cast<uint32_t>(get_int("hysteresis_delay_sec", p.hysteresis_delay_sec));
    p.video_device = get_string("video_device", p.video_device);
    p.detect_audio = get_bool("detect_audio", p.detect_audio);
    p.detect_pipewire = get_bool("detect_pipewire", p.detect_pipewire);
    p.status_file = get_string("status_file", p.status_file);
    p.cgroup_mount = get_string("cgroup_mount", p.cgroup_mount);
    p.proc_mount = get_string("proc_mount", p.proc_mount);
    p.dry_run = get_bool("dry_run", p.dry_run);
    return p;
}

} // namespace isp
