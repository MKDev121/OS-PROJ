#pragma once

#include "ioprio.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <ostream>

namespace isp {

enum class WorkloadMode {
    CPU,      // Multi-threaded arithmetic, hashing, and transcendental math contention
    IO,       // Direct/buffered disk churn with fdatasync for I/O scheduling contention
    MEMORY,   // Memory allocation, cache thrashing, and memory.high limit stress
    MIXED,    // Combined concurrent CPU + I/O + Memory contention
    STRESS    // Ultra-heavy multi-core cache thrashing, matrix multiplication & contention
};

inline const char* to_string(WorkloadMode mode) {
    switch (mode) {
        case WorkloadMode::CPU:    return "CPU";
        case WorkloadMode::IO:     return "IO";
        case WorkloadMode::MEMORY: return "MEMORY";
        case WorkloadMode::MIXED:  return "MIXED";
        case WorkloadMode::STRESS: return "STRESS";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, WorkloadMode mode) {
    return os << to_string(mode);
}

struct WorkloadConfig {
    WorkloadMode mode{WorkloadMode::CPU};
    uint32_t threads{0};                   // 0 = auto-detect hardware concurrency
    uint32_t duration_sec{0};              // 0 = indefinite until SIGINT/SIGTERM
    double report_interval_sec{1.0};       // Live progress telemetry frequency
    std::string work_dir{"/tmp"};          // Temp directory for I/O churn files
    uint32_t io_size_mb{64};               // File size per I/O worker
    uint32_t mem_size_mb{128};             // Buffer size per memory worker
    uint32_t matrix_size{512};             // Dimension for dense matrix operations
    bool oversubscribe_threads{false};     // If true, spawn 2x cores for total saturation
    std::string output_csv{""};            // Optional path for time-series metrics CSV
    bool quiet{false};                     // Suppress interactive progress printing
};

struct WorkloadSample {
    double timestamp{0.0};                 // Epoch time in seconds
    double elapsed_sec{0.0};               // Seconds from workload start
    uint64_t ops_delta{0};                 // Operations in this sample interval
    double rate_mops{0.0};                 // Million operations per second
    uint64_t io_bytes_delta{0};            // Bytes I/O in this sample interval
    double io_rate_mb_s{0.0};              // MB/sec I/O throughput
    std::string cgroup{"/"};               // Current cgroup path from /proc/self/cgroup
    bool is_throttled{false};              // True if cgroup is in protector.slice/background or IDLE prio
    IoPriorityClass ioprio_class{IoPriorityClass::NONE};
    int ioprio_data{0};
};

struct WorkloadSummary {
    double total_duration_sec{0.0};
    uint64_t total_ops{0};
    uint64_t total_io_bytes{0};
    double avg_unthrottled_mops{0.0};
    double avg_throttled_mops{0.0};
    double avg_unthrottled_io_mb_s{0.0};
    double avg_throttled_io_mb_s{0.0};
    double slowdown_pct{0.0};              // (1 - throttled_rate / unthrottled_rate) * 100
    double throttled_time_sec{0.0};
    double throttled_time_pct{0.0};
    size_t total_samples{0};
    size_t throttled_samples{0};
};

class BgWorkloadApp {
public:
    explicit BgWorkloadApp(WorkloadConfig config = WorkloadConfig{});
    ~BgWorkloadApp();

    BgWorkloadApp(const BgWorkloadApp&) = delete;
    BgWorkloadApp& operator=(const BgWorkloadApp&) = delete;

    bool init();
    void run();
    void stop();

    bool export_csv(const std::string& path) const;
    void print_summary() const;

    WorkloadSummary get_summary() const;
    const std::vector<WorkloadSample>& get_samples() const;
    bool is_running() const { return is_running_.load(); }
    const WorkloadConfig& get_config() const { return config_; }

    // Helpers for telemetry and testing
    static std::string read_current_cgroup(const std::string& proc_cgroup_path = "/proc/self/cgroup");
    static bool check_is_throttled(const std::string& cgroup_path, IoPriorityClass prio_class);

    // Testing hook: manually push a sample for unit testing
    void record_sample(const WorkloadSample& sample);

private:
    void cpu_worker_thread(uint32_t thread_id);
    void io_worker_thread(uint32_t thread_id, const std::string& filepath);
    void memory_worker_thread(uint32_t thread_id);
    void stress_worker_thread(uint32_t thread_id);
    void mem_stream_worker_thread(uint32_t thread_id);
    void reporter_loop();

    WorkloadConfig config_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> is_running_{false};

    // Performance metrics counters
    std::atomic<uint64_t> total_ops_{0};
    std::atomic<uint64_t> total_io_bytes_{0};

    std::vector<std::thread> workers_;
    std::thread reporter_thread_;

    mutable std::mutex samples_mutex_;
    std::vector<WorkloadSample> samples_;
    std::vector<std::string> temp_files_;
};

} // namespace isp
