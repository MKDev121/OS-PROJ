#include "bg_workload.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

namespace {
std::atomic<isp::BgWorkloadApp*> g_app_instance{nullptr};

void handle_signal(int sig) {
    (void)sig;
    isp::BgWorkloadApp* app = g_app_instance.load();
    if (app) {
        app->stop();
    }
}
} // namespace

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "A synthetic background workload generator and benchmarking tool designed to create\n"
              << "resource contention (CPU, Disk I/O, Memory) and observe dynamic throttling by the\n"
              << "Interactive Session Protector daemon.\n\n"
              << "Options:\n"
              << "  -m, --mode <mode>        Workload mode: cpu, io, memory, mixed, stress [default: cpu]\n"
              << "      --stress, --hog      Ultra-heavy multi-vector contention: pins all cores, memory bus & NVMe\n"
              << "      --lag-call, --real-lag  Aggressively lag real video calls (Google Meet, Zoom, Discord, WebRTC)\n"
              << "      --matrix <size>      Run dense matrix multiplication with cache-thrashing (e.g. 500)\n"
              << "  -t, --threads <num>      Number of worker threads (0 = auto-detect CPU cores) [default: auto]\n"
              << "  -d, --duration <sec>     Run duration in seconds (0 = run indefinitely) [default: 0]\n"
              << "  -r, --report <sec>       Progress and telemetry reporting interval in seconds [default: 1.0]\n"
              << "  -o, --output <file>      Export time-series metrics to CSV file\n"
              << "      --work-dir <path>    Directory for temporary I/O churn files [default: /tmp]\n"
              << "      --io-size <MB>       File size written per I/O worker in MB [default: 64]\n"
              << "      --mem-size <MB>      Memory buffer allocated per memory worker in MB [default: 128]\n"
              << "  -q, --quiet              Suppress periodic terminal output (only print final summary)\n"
              << "  -h, --help               Show this help message and exit\n\n"
              << "Examples:\n"
              << "  " << prog_name << " --stress                   # Multi-vector contention to lag video calls\n"
              << "  " << prog_name << " --lag-call                 # Dedicated mode for real video call testing\n"
              << "  " << prog_name << " --mode cpu --threads 4 --duration 30\n"
              << "  " << prog_name << " --mode mixed --output workload_metrics.csv\n"
              << "  " << prog_name << " -m io --work-dir /var/tmp --io-size 128\n";
}

int main(int argc, char* argv[]) {
    isp::WorkloadConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--stress" || arg == "--hog" || arg == "--heavy" || 
                   arg == "--lag-call" || arg == "--real-lag" || arg == "--lag-video") {
            config.mode = isp::WorkloadMode::STRESS;
            config.oversubscribe_threads = true;
        } else if (arg == "--matrix") {
            config.mode = isp::WorkloadMode::STRESS;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                try {
                    config.matrix_size = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    config.matrix_size = 512;
                }
            } else {
                config.matrix_size = 512;
            }
        } else if (arg == "-m" || arg == "--mode") {
            if (i + 1 < argc) {
                std::string mode_str = argv[++i];
                if (mode_str == "cpu" || mode_str == "CPU") {
                    config.mode = isp::WorkloadMode::CPU;
                } else if (mode_str == "io" || mode_str == "IO") {
                    config.mode = isp::WorkloadMode::IO;
                } else if (mode_str == "memory" || mode_str == "mem" || mode_str == "MEMORY") {
                    config.mode = isp::WorkloadMode::MEMORY;
                } else if (mode_str == "mixed" || mode_str == "MIXED") {
                    config.mode = isp::WorkloadMode::MIXED;
                } else if (mode_str == "stress" || mode_str == "STRESS") {
                    config.mode = isp::WorkloadMode::STRESS;
                    config.oversubscribe_threads = true;
                } else {
                    std::cerr << "Error: Unknown workload mode '" << mode_str << "'. Expected cpu, io, memory, mixed, or stress.\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: --mode requires an argument\n";
                return 1;
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                try {
                    config.threads = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    std::cerr << "Error: Invalid thread count\n";
                    return 1;
                }
            }
        } else if (arg == "-d" || arg == "--duration") {
            if (i + 1 < argc) {
                try {
                    config.duration_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    std::cerr << "Error: Invalid duration\n";
                    return 1;
                }
            }
        } else if (arg == "-r" || arg == "--report") {
            if (i + 1 < argc) {
                try {
                    config.report_interval_sec = std::stod(argv[++i]);
                } catch (...) {
                    std::cerr << "Error: Invalid report interval\n";
                    return 1;
                }
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.output_csv = argv[++i];
            }
        } else if (arg == "--work-dir") {
            if (i + 1 < argc) {
                config.work_dir = argv[++i];
            }
        } else if (arg == "--io-size") {
            if (i + 1 < argc) {
                try {
                    config.io_size_mb = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    std::cerr << "Error: Invalid IO size\n";
                    return 1;
                }
            }
        } else if (arg == "--mem-size") {
            if (i + 1 < argc) {
                try {
                    config.mem_size_mb = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    std::cerr << "Error: Invalid memory size\n";
                    return 1;
                }
            }
        } else if (arg == "-q" || arg == "--quiet") {
            config.quiet = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    isp::BgWorkloadApp app(config);
    g_app_instance.store(&app);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    app.run();

    return 0;
}
