#include "config.hpp"
#include "logger.hpp"
#include "types.hpp"
#include "daemon.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config <file>     Path to configuration file\n"
              << "  -d, --dry-run           Run in dry-run mode [default]\n"
              << "  --real                  Enable real cgroup writes and process migration\n"
              << "  -s, --status            Show daemon runtime status and active sessions\n"
              << "  -p, --pidfile <file>    Path to PID lockfile (default: /tmp/protector.pid)\n"
              << "  -t, --timeout <sec>     Run for specified seconds then exit (for testing)\n"
              << "  -v, --verbose           Enable debug-level logging\n"
              << "  -h, --help              Show this help message and exit\n";
}

int show_status(const std::string& pidfile, const std::string& status_file) {
    std::ifstream pf(pidfile);
    if (!pf.is_open()) {
        std::cout << "\033[1;33mInteractive Session Protector Status\033[0m\n"
                  << "==================================================\n"
                  << "Daemon Status:  \033[1;31mSTOPPED\033[0m (PID lockfile " << pidfile << " not found)\n"
                  << "==================================================\n";
        return 0;
    }

    pid_t daemon_pid = 0;
    pf >> daemon_pid;
    pf.close();

    std::error_code ec;
    if (daemon_pid <= 0 || !std::filesystem::exists("/proc/" + std::to_string(daemon_pid), ec)) {
        std::cout << "\033[1;33mInteractive Session Protector Status\033[0m\n"
                  << "==================================================\n"
                  << "Daemon Status:  \033[1;31mSTOPPED\033[0m (stale PID file with PID " << daemon_pid << ")\n"
                  << "==================================================\n";
        return 0;
    }

    std::ifstream sf(status_file);
    std::unordered_map<std::string, std::string> kv;
    if (sf.is_open()) {
        std::string line;
        while (std::getline(sf, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                kv[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }

    std::string mode = kv.count("mode") ? kv["mode"] : "UNKNOWN";
    std::string state = kv.count("state") ? kv["state"] : "UNKNOWN";
    std::string bg_cpu = kv.count("bg_cpu_weight") ? kv["bg_cpu_weight"] : "20";
    std::string prot_cpu = kv.count("prot_cpu_weight") ? kv["prot_cpu_weight"] : "800";
    std::string hyst = kv.count("hysteresis_remaining_sec") ? kv["hysteresis_remaining_sec"] : "0";
    std::string prot_pids = kv.count("protected_pids") ? kv["protected_pids"] : "";
    std::string bg_pids = kv.count("background_pids") ? kv["background_pids"] : "";

    std::string state_color = (state == "ACTIVE") ? "\033[1;32m" : ((state == "IN_HYSTERESIS") ? "\033[1;33m" : "\033[1;34m");

    std::cout << "\033[1;36m==================================================\033[0m\n"
              << "\033[1;37m        Interactive Session Protector Status      \033[0m\n"
              << "\033[1;36m==================================================\033[0m\n"
              << "Daemon Status:        \033[1;32mRUNNING\033[0m (PID: " << daemon_pid << ", Mode: " << mode << ")\n"
              << "Session State:        " << state_color << state << "\033[0m"
              << (state == "IN_HYSTERESIS" ? (" (grace countdown: " + hyst + "s remaining)") : "") << "\n"
              << "Shaped Weights:       Protected: " << prot_cpu << " | Background: " << bg_cpu << "\n"
              << "--------------------------------------------------\n";

    std::cout << "Protected Sessions:   " << (prot_pids.empty() ? "None (no active call)" : "") << "\n";
    if (!prot_pids.empty()) {
        std::stringstream ss(prot_pids);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                std::cout << "  \033[1;32m●\033[0m [PID] " << item << "\n";
            }
        }
    }

    std::cout << "Throttled Workloads:  " << (bg_pids.empty() ? "None active" : "") << "\n";
    if (!bg_pids.empty()) {
        std::stringstream ss(bg_pids);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                std::cout << "  \033[1;31m■\033[0m [PID] " << item << " (cpu.weight=" << bg_cpu << ", ioprio=IDLE)\n";
            }
        }
    }
    std::cout << "\033[1;36m==================================================\033[0m\n";
    return 0;
}

int main(int argc, char* argv[]) {
    std::string config_file = "config/protector.conf";
    std::string pidfile = "/tmp/protector.pid";
    bool dry_run = true; // Default to true for safety
    bool verbose = false;
    bool check_status = false;
    int timeout_sec = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--status") {
            check_status = true;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                std::cerr << "Error: --config requires a file argument\n";
                return 1;
            }
        } else if (arg == "-p" || arg == "--pidfile") {
            if (i + 1 < argc) {
                pidfile = argv[++i];
            }
        } else if (arg == "-d" || arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--real") {
            dry_run = false;
        } else if (arg == "-t" || arg == "--timeout") {
            if (i + 1 < argc) {
                timeout_sec = std::stoi(argv[++i]);
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (check_status) {
        std::string status_file = "/tmp/protector.status";
        isp::Config config;
        if (config.load_from_file(config_file)) {
            status_file = config.get_string("status_file", status_file);
        }
        return show_status(pidfile, status_file);
    }

    if (verbose) {
        isp::Logger::instance().set_level(isp::LogLevel::DEBUG);
    }

    ISP_LOG_INFO("Interactive Session Protector daemon initializing...");

    isp::Config config;
    if (config.load_from_file(config_file)) {
        ISP_LOG_INFO("Loaded configuration from: " << config_file);
    } else {
        ISP_LOG_WARN("Could not read configuration from " << config_file << ", using defaults");
    }

    config.set("dry_run", dry_run ? "true" : "false");
    isp::ResourcePolicy policy = config.to_resource_policy();

    ISP_LOG_INFO("Configuration summary:");
    ISP_LOG_INFO("  video_device: " << policy.video_device);
    ISP_LOG_INFO("  cgroup_mount: " << policy.cgroup_mount);
    ISP_LOG_INFO("  proc_mount:   " << policy.proc_mount);
    ISP_LOG_INFO("  poll_ms:      " << policy.poll_interval_ms);
    ISP_LOG_INFO("  hysteresis:   " << policy.hysteresis_delay_sec << "s");
    ISP_LOG_INFO("  mode:         " << (policy.dry_run ? "DRY-RUN (SIMULATION)" : "REAL (ACTIVE)"));

    isp::ProtectorDaemon daemon(policy, pidfile);

    if (!daemon.start()) {
        ISP_LOG_ERROR("Daemon failed to start");
        return 1;
    }

    if (timeout_sec > 0) {
        ISP_LOG_INFO("Running in timed mode for " << timeout_sec << " seconds...");
        daemon.wait(timeout_sec);
    } else {
        // Run indefinitely until signal
        daemon.wait();
    }

    return 0;
}
