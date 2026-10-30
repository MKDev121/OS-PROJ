#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>

namespace isp {

struct FrameMetric {
    double timestamp{0.0};           // Seconds since session start (microsecond precision)
    uint64_t timestamp_us{0};        // Microseconds since session start
    uint32_t frame_number{0};        // 1-indexed frame sequence number
    double frame_interval_ms{0.0};   // Time since previous frame in ms
    bool deadline_missed{false};     // True if interval exceeded deadline threshold
    double jitter_ms{0.0};           // Absolute deviation from target interval
};

struct FakeCallConfig {
    std::string device{"/dev/video10"};
    double target_fps{30.0};
    uint32_t duration_sec{60};
    std::string output_csv{"fake_call_metrics.csv"};
    double deadline_tolerance_ratio{1.2}; // 1.2 * 33.33ms = 40.0ms deadline limit
    bool simulate_cpu_load{true};        // Simulate frame encoding CPU workload
    double load_ms{14.0};                // Workload duration per frame in ms (simulates realistic 720p/1080p encode)
};

class FakeCallApp {
public:
    explicit FakeCallApp(FakeCallConfig config = FakeCallConfig{});
    ~FakeCallApp();

    FakeCallApp(const FakeCallApp&) = delete;
    FakeCallApp& operator=(const FakeCallApp&) = delete;

    bool init();
    void run();
    void stop();

    bool export_csv(const std::string& path) const;
    void print_summary() const;

    const std::vector<FrameMetric>& get_metrics() const { return metrics_; }
    size_t get_total_frames() const { return metrics_.size(); }
    size_t get_deadline_miss_count() const { return deadline_misses_; }
    double get_deadline_miss_rate() const;
    int get_device_fd() const { return device_fd_; }
    bool is_running() const { return is_running_.load(); }
    const FakeCallConfig& get_config() const { return config_; }

    // Helper for programmatic/unit testing of metric recording
    void record_frame(double interval_ms, uint64_t elapsed_us = 0);

private:
    FakeCallConfig config_;
    int device_fd_{-1};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> is_running_{false};
    std::vector<FrameMetric> metrics_;
    size_t deadline_misses_{0};
    uint64_t ops_per_ms_{0};

    std::vector<uint8_t> current_frame_;
    std::vector<uint8_t> ref_frame_;
    std::vector<int16_t> residual_;
    uint32_t calibrated_iterations_{48};

    void read_video_frame();
    void process_simulated_frame();
};

} // namespace isp
