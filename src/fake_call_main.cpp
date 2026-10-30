#include "fake_call.hpp"
#include "logger.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

namespace {
std::atomic<isp::FakeCallApp*> g_app_instance{nullptr};

void handle_signal(int sig) {
    (void)sig;
    isp::FakeCallApp* app = g_app_instance.load();
    if (app) {
        app->stop();
    }
}
} // namespace

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -d, --device <dev>      Path to webcam device (default: /dev/video10)\n"
              << "  -f, --fps <val>         Target frames per second (default: 30.0)\n"
              << "  -t, --duration <sec>    Run duration in seconds (default: 60)\n"
              << "  -o, --output <file>     Path to output CSV metrics file (default: fake_call_metrics.csv)\n"
              << "      --load-ms <ms>      Target frame encode work in ms [default: 18.0ms]\n"
              << "      --heavy             Realistic heavy video encode workload (22.0ms/frame)\n"
              << "      --tolerance <ratio> Deadline miss tolerance ratio (default: 1.2)\n"
              << "      --no-cpu-load       Disable simulated video encode CPU workload\n"
              << "  -h, --help              Show this help message and exit\n";
}

int main(int argc, char* argv[]) {
    isp::FakeCallConfig config;
    config.device = "/dev/video10";
    config.target_fps = 30.0;
    config.duration_sec = 60;
    config.output_csv = "fake_call_metrics.csv";
    config.deadline_tolerance_ratio = 1.2;
    config.simulate_cpu_load = true;
    config.load_ms = 18.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 < argc) {
                config.device = argv[++i];
            } else {
                std::cerr << "Error: --device requires a device path\n";
                return 1;
            }
        } else if (arg == "-f" || arg == "--fps") {
            if (i + 1 < argc) {
                try {
                    config.target_fps = std::stod(argv[++i]);
                } catch (...) {
                    std::cerr << "Error: Invalid FPS value\n";
                    return 1;
                }
            }
        } else if (arg == "-t" || arg == "--duration") {
            if (i + 1 < argc) {
                try {
                    config.duration_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
                } catch (...) {
                    std::cerr << "Error: Invalid duration value\n";
                    return 1;
                }
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.output_csv = argv[++i];
            }
        } else if (arg == "--tolerance") {
            if (i + 1 < argc) {
                try {
                    config.deadline_tolerance_ratio = std::stod(argv[++i]);
                } catch (...) {
                    std::cerr << "Error: Invalid tolerance ratio\n";
                    return 1;
                }
            }
        } else if (arg == "--load-ms") {
            if (i + 1 < argc) {
                try {
                    config.load_ms = std::stod(argv[++i]);
                } catch (...) {
                    std::cerr << "Error: Invalid load-ms value\n";
                    return 1;
                }
            }
        } else if (arg == "--heavy") {
            config.load_ms = 22.0;
        } else if (arg == "--no-cpu-load") {
            config.simulate_cpu_load = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    isp::FakeCallApp app(config);
    g_app_instance.store(&app);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    app.run();
    app.print_summary();

    g_app_instance.store(nullptr);
    return 0;
}
