#include "overlay.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <cmath>

namespace fs = std::filesystem;

namespace isp {

void OverlayStatus::compute_percentages() {
    if (state == "ACTIVE" || state == "IN_HYSTERESIS") {
        uint32_t total_weight = prot_cpu_weight + bg_cpu_weight + norm_cpu_weight;
        if (total_weight > 0) {
            video_call_pct = (static_cast<float>(prot_cpu_weight) / total_weight) * 100.0f;
            background_pct = (static_cast<float>(bg_cpu_weight) / total_weight) * 100.0f;
            critical_sys_pct = (static_cast<float>(norm_cpu_weight) / total_weight) * 100.0f;
        } else {
            video_call_pct = 0.0f;
            background_pct = 0.0f;
            critical_sys_pct = 100.0f;
        }
    } else if (state == "INACTIVE") {
        video_call_pct = 0.0f;
        background_pct = 50.0f;
        critical_sys_pct = 50.0f;
    } else { // STOPPED or UNKNOWN
        video_call_pct = 0.0f;
        background_pct = 0.0f;
        critical_sys_pct = 100.0f;
    }
}

StatusReader::StatusReader(std::string pidfile, std::string status_file)
    : pidfile_(std::move(pidfile)), status_file_(std::move(status_file)) {}

void StatusReader::set_mock_mode(bool enable, const std::string& mock_state) {
    mock_mode_ = enable;
    mock_state_ = mock_state;
    mock_start_time_ = std::chrono::steady_clock::now();
}

OverlayStatus StatusReader::poll() {
    OverlayStatus status;

    if (mock_mode_) {
        status.daemon_running = true;
        status.daemon_pid = 99999;
        status.mode = "SIMULATION (MOCK)";
        status.state = mock_state_;
        status.prot_cpu_weight = 800;
        status.bg_cpu_weight = 20;
        status.norm_cpu_weight = 100;
        
        if (status.state == "IN_HYSTERESIS") {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - mock_start_time_).count();
            int rem = 5 - static_cast<int>(elapsed % 6);
            status.hysteresis_remaining_sec = (rem < 0) ? 0 : rem;
        } else {
            status.hysteresis_remaining_sec = 0;
        }

        if (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS") {
            status.protected_procs = {
                { 14210, "google-chrome (Google Meet)" },
                { 15302, "pipewire (WebRTC Audio/Video)" }
            };
            status.background_procs = {
                { 28411, "dd (Heavy Disk I/O)" },
                { 28415, "cc1plus (Kernel Build Task)" }
            };
        }
        status.compute_percentages();
        return status;
    }

    // Check PID file
    std::ifstream pf(pidfile_);
    if (!pf.is_open()) {
        status.daemon_running = false;
        status.state = "STOPPED";
        status.compute_percentages();
        return status;
    }

    pid_t daemon_pid = 0;
    pf >> daemon_pid;
    pf.close();

    std::error_code ec;
    if (daemon_pid <= 0 || !fs::exists("/proc/" + std::to_string(daemon_pid), ec)) {
        status.daemon_running = false;
        status.state = "STOPPED";
        status.compute_percentages();
        return status;
    }

    status.daemon_running = true;
    status.daemon_pid = daemon_pid;

    // Read status file
    std::ifstream sf(status_file_);
    if (!sf.is_open()) {
        status.state = "INITIALIZING";
        status.compute_percentages();
        return status;
    }

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(sf, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    status.mode = kv.count("mode") ? kv["mode"] : "UNKNOWN";
    status.state = kv.count("state") ? kv["state"] : "UNKNOWN";
    
    if (kv.count("bg_cpu_weight")) {
        try { status.bg_cpu_weight = std::stoul(kv["bg_cpu_weight"]); } catch (...) {}
    }
    if (kv.count("prot_cpu_weight")) {
        try { status.prot_cpu_weight = std::stoul(kv["prot_cpu_weight"]); } catch (...) {}
    }
    if (kv.count("hysteresis_remaining_sec")) {
        try { status.hysteresis_remaining_sec = std::stoul(kv["hysteresis_remaining_sec"]); } catch (...) {}
    }

    // Parse protected PIDs
    if (kv.count("protected_pids") && !kv["protected_pids"].empty()) {
        std::stringstream ss(kv["protected_pids"]);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t colon = token.find(':');
            if (colon != std::string::npos) {
                try {
                    pid_t pid = std::stoi(token.substr(0, colon));
                    std::string comm = token.substr(colon + 1);
                    status.protected_procs.push_back({ pid, comm });
                } catch (...) {}
            }
        }
    }

    // Parse background PIDs
    if (kv.count("background_pids") && !kv["background_pids"].empty()) {
        std::stringstream ss(kv["background_pids"]);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t colon = token.find(':');
            if (colon != std::string::npos) {
                try {
                    pid_t pid = std::stoi(token.substr(0, colon));
                    std::string comm = token.substr(colon + 1);
                    status.background_procs.push_back({ pid, comm });
                } catch (...) {}
            }
        }
    }

    status.compute_percentages();
    return status;
}

} // namespace isp
