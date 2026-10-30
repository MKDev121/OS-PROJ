#include "bg_workload.hpp"
#include "logger.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <immintrin.h>

namespace fs = std::filesystem;

namespace isp {

namespace {
// ANSI Terminal Colors
constexpr const char* COLOR_RESET   = "\033[0m";
constexpr const char* COLOR_BOLD    = "\033[1m";
constexpr const char* COLOR_GREEN   = "\033[1;32m";
constexpr const char* COLOR_YELLOW  = "\033[1;33m";
constexpr const char* COLOR_RED     = "\033[1;31m";
constexpr const char* COLOR_CYAN    = "\033[1;36m";
constexpr const char* COLOR_MAGENTA = "\033[1;35m";

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
void stream_write_avx2(uint8_t* ptr, size_t size, uint32_t pattern) {
#if defined(__x86_64__) || defined(_M_X64)
    __m256i val1 = _mm256_set1_epi32(static_cast<int>(pattern));
    __m256i val2 = _mm256_set1_epi32(static_cast<int>(pattern ^ 0xAAAAAAAA));
    for (size_t offset = 0; offset + 64 <= size; offset += 64) {
        _mm256_stream_si256(reinterpret_cast<__m256i*>(ptr + offset), val1);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(ptr + offset + 32), val2);
    }
    _mm_sfence();
#else
    (void)ptr; (void)size; (void)pattern;
#endif
}

void stream_write_portable(uint8_t* ptr, size_t size, uint64_t pattern) {
    for (size_t offset = 0; offset + sizeof(uint64_t) <= size; offset += 64) {
        *reinterpret_cast<volatile uint64_t*>(ptr + offset) = pattern;
    }
}

bool check_cpu_has_avx2() {
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(_M_X64))
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}
} // namespace

BgWorkloadApp::BgWorkloadApp(WorkloadConfig config)
    : config_(std::move(config)) {
    if (config_.threads == 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        if (config_.mode == WorkloadMode::STRESS || config_.oversubscribe_threads) {
            // For STRESS mode or oversubscribed testing, spawn 4x hardware concurrency (min 64 threads)
            // to saturate CFS runqueues and elevate dispatch latency >40ms to lag real video calls.
            config_.threads = (hw > 0) ? std::max(64u, hw * 4) : 64;
        } else {
            // Oversubscribe to 2x hardware concurrency so CFS runqueues contest all CPU cores
            config_.threads = (hw > 0) ? (hw * 2) : 8;
        }
    }
    if (config_.report_interval_sec <= 0.0) {
        config_.report_interval_sec = 1.0;
    }
}

BgWorkloadApp::~BgWorkloadApp() {
    stop();
    // Clean up temporary I/O files
    for (const auto& f : temp_files_) {
        std::error_code ec;
        fs::remove(f, ec);
    }
}

bool BgWorkloadApp::init() {
    std::error_code ec;
    // If work_dir is default /tmp, check if /tmp is a tmpfs (RAM disk).
    // If /var/tmp is on real persistent storage and writable, use /var/tmp for real disk I/O contention.
    if (config_.work_dir == "/tmp") {
        if (fs::exists("/var/tmp", ec) && access("/var/tmp", W_OK) == 0) {
            config_.work_dir = "/var/tmp";
        }
    }
    if (!fs::exists(config_.work_dir, ec)) {
        if (!fs::create_directories(config_.work_dir, ec)) {
            ISP_LOG_ERROR("bg_workload: Failed to create work_dir: " << config_.work_dir);
            return false;
        }
    }
    return true;
}

std::string BgWorkloadApp::read_current_cgroup(const std::string& proc_cgroup_path) {
    std::ifstream cg_file(proc_cgroup_path);
    if (!cg_file.is_open()) {
        return "/";
    }
    std::string line;
    while (std::getline(cg_file, line)) {
        size_t colon_pos = line.rfind(':');
        if (colon_pos != std::string::npos && colon_pos + 1 < line.size()) {
            return line.substr(colon_pos + 1);
        }
    }
    return "/";
}

bool BgWorkloadApp::check_is_throttled(const std::string& cgroup_path, IoPriorityClass prio_class) {
    if (cgroup_path.find("protector.slice/background") != std::string::npos ||
        cgroup_path.find("/background") != std::string::npos) {
        return true;
    }
    if (prio_class == IoPriorityClass::IDLE) {
        return true;
    }
    return false;
}

void BgWorkloadApp::record_sample(const WorkloadSample& sample) {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    samples_.push_back(sample);
}

void BgWorkloadApp::cpu_worker_thread(uint32_t thread_id) {
    // High-intensity ALU, FPU and cache-line math worker
    constexpr size_t BUF_SIZE = 32 * 1024; // 128 KB buffer (contests L1/L2 caches)
    std::vector<uint32_t> buf(BUF_SIZE, 0x12345678 ^ (thread_id << 16));
    uint64_t state = 0x9e3779b97f4a7c15ULL * (thread_id + 1) + 12345;
    double val = 1.41421356;

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        for (size_t i = 0; i < BUF_SIZE; ++i) {
            state ^= (state << 13);
            state ^= (state >> 7);
            state ^= (state << 17);
            buf[i] ^= static_cast<uint32_t>(state);
            val = std::sqrt(val + static_cast<double>(buf[i] & 0xFFFF) * 1e-6);
            if (val > 1000.0) val = 1.41421356;
        }
        total_ops_.fetch_add(BUF_SIZE, std::memory_order_relaxed);
    }
}

void BgWorkloadApp::io_worker_thread(uint32_t thread_id, const std::string& filepath) {
    // 512 KB buffer chunk
    constexpr size_t CHUNK_SIZE = 512 * 1024;
    std::vector<char> buffer(CHUNK_SIZE, static_cast<char>(0xAA ^ thread_id));

    int fd = open(filepath.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0644);
    if (fd < 0) {
        fd = open(filepath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            ISP_LOG_ERROR("bg_workload: Cannot open IO file: " << filepath);
            return;
        }
    }

    size_t target_bytes = static_cast<size_t>(config_.io_size_mb) * 1024 * 1024;
    if (target_bytes == 0) target_bytes = 64 * 1024 * 1024;

    size_t current_file_size = 0;

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        ssize_t w = write(fd, buffer.data(), CHUNK_SIZE);
        if (w > 0) {
            total_io_bytes_.fetch_add(static_cast<uint64_t>(w), std::memory_order_relaxed);
            current_file_size += static_cast<size_t>(w);
        }

        // Flush to storage device with fdatasync to trigger block-layer contention
        if (current_file_size >= target_bytes || current_file_size % (2 * 1024 * 1024) == 0) {
            fdatasync(fd);
            total_ops_.fetch_add(1, std::memory_order_relaxed);

            if (current_file_size >= target_bytes) {
                // Seek back to overwrite and avoid exhausting disk space
                lseek(fd, 0, SEEK_SET);
                current_file_size = 0;
            }
        }
    }

    close(fd);
}

void BgWorkloadApp::memory_worker_thread(uint32_t thread_id) {
    size_t buf_bytes = static_cast<size_t>(config_.mem_size_mb) * 1024 * 1024;
    if (buf_bytes == 0) buf_bytes = 64 * 1024 * 1024;

    void* raw_ptr = nullptr;
    if (posix_memalign(&raw_ptr, 64, buf_bytes) != 0 || !raw_ptr) {
        ISP_LOG_ERROR("bg_workload: Memory allocation failed for " << config_.mem_size_mb << " MB");
        return;
    }
    uint8_t* ptr = static_cast<uint8_t*>(raw_ptr);
    std::memset(ptr, static_cast<uint8_t>(thread_id & 0xFF), buf_bytes);

    const bool use_avx2 = check_cpu_has_avx2();
    uint32_t pattern = 0x517cc1b7 ^ (thread_id << 16);

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (use_avx2) {
            stream_write_avx2(ptr, buf_bytes, pattern);
        } else {
            stream_write_portable(ptr, buf_bytes, pattern | (static_cast<uint64_t>(pattern) << 32));
        }
        pattern = pattern * 1664525u + 1013904223u;
        total_ops_.fetch_add(buf_bytes / 64, std::memory_order_relaxed);
        total_io_bytes_.fetch_add(buf_bytes, std::memory_order_relaxed);
    }

    free(raw_ptr);
}

void BgWorkloadApp::mem_stream_worker_thread(uint32_t thread_id) {
    // Memory bus saturator: Allocates 64MB buffer aligned to 64 bytes and issues non-temporal
    // streaming writes directly to RAM, bypassing CPU cache hierarchy. Floods DDR5 bus controllers
    // with un-cached write transactions, starving GPU texture uploads & video buffers.
    size_t size = static_cast<size_t>(config_.mem_size_mb > 0 ? config_.mem_size_mb : 64) * 1024 * 1024;
    if (size > 128 * 1024 * 1024) size = 128 * 1024 * 1024;

    void* raw_ptr = nullptr;
    if (posix_memalign(&raw_ptr, 64, size) != 0 || !raw_ptr) {
        ISP_LOG_ERROR("bg_workload: Failed to allocate aligned memory for mem_stream worker");
        return;
    }
    uint8_t* ptr = static_cast<uint8_t*>(raw_ptr);
    std::memset(ptr, 0x55 ^ (thread_id & 0xFF), size);

    const bool use_avx2 = check_cpu_has_avx2();
    uint32_t pattern = 0x12345678 ^ (thread_id << 16);

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (use_avx2) {
            stream_write_avx2(ptr, size, pattern);
        } else {
            stream_write_portable(ptr, size, pattern | (static_cast<uint64_t>(pattern) << 32));
        }
        pattern = pattern * 1664525u + 1013904223u;
        total_ops_.fetch_add(size / 64, std::memory_order_relaxed);
        total_io_bytes_.fetch_add(size, std::memory_order_relaxed);
    }

    free(raw_ptr);
}

void BgWorkloadApp::stress_worker_thread(uint32_t thread_id) {
    // Dense Matrix Multiply + Cache Thrashing
    uint32_t N = config_.matrix_size > 0 ? std::min(config_.matrix_size, 512u) : 256u;
    if (N < 64) N = 64;

    size_t mat_size = static_cast<size_t>(N) * N;
    std::vector<double> A(mat_size, 1.01 + 0.01 * (thread_id % 16));
    std::vector<double> B(mat_size, 0.99 - 0.01 * (thread_id % 16));
    std::vector<double> C(mat_size, 0.0);

    // Large 16MB cache-buster buffer for severe memory bus and L3 cache contention
    constexpr size_t CACHE_BUSTER_SIZE = 16 * 1024 * 1024;
    std::vector<uint8_t> cache_buster(CACHE_BUSTER_SIZE, static_cast<uint8_t>(thread_id & 0xFF));

    uint64_t seed = 0x123456789ABCDEF0ULL ^ (static_cast<uint64_t>(thread_id) << 32);

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        // Block matrix multiply (high computational and execution-port intensity)
        for (uint32_t i = 0; i < N; i += 16) {
            for (uint32_t j = 0; j < N; j += 16) {
                for (uint32_t k = 0; k < N; ++k) {
                    for (uint32_t ii = 0; ii < 16 && (i + ii) < N; ++ii) {
                        for (uint32_t jj = 0; jj < 16 && (j + jj) < N; ++jj) {
                            C[(i + ii) * N + (j + jj)] += A[(i + ii) * N + k] * B[k * N + (j + jj)];
                        }
                    }
                }
            }
            if (stop_requested_.load(std::memory_order_relaxed)) break;
        }

        // Cache thrashing: sweep across 16MB memory with 64-byte cache lines to evict L1/L2/L3 caches
        for (size_t p = 0; p + sizeof(uint64_t) <= CACHE_BUSTER_SIZE; p += 64) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            uint64_t* ptr = reinterpret_cast<uint64_t*>(&cache_buster[p]);
            *ptr ^= seed;
        }

        total_ops_.fetch_add(static_cast<uint64_t>(N) * N * 2 + (CACHE_BUSTER_SIZE / 64), std::memory_order_relaxed);
        total_io_bytes_.fetch_add(mat_size * sizeof(double) + CACHE_BUSTER_SIZE, std::memory_order_relaxed);

        // Normalize C occasionally to avoid floating-point overflow
        if (C[0] > 1e12 || std::isnan(C[0])) {
            std::fill(C.begin(), C.end(), 1.0);
        }
    }
}

void BgWorkloadApp::reporter_loop() {
    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    uint64_t last_ops = 0;
    uint64_t last_io_bytes = 0;

    double baseline_rate = 0.0;
    bool has_baseline = false;

    if (!config_.quiet) {
        std::cout << "\n" << COLOR_BOLD << COLOR_CYAN
                  << "================================================================================\n"
                  << "  INTERACTIVE SESSION PROTECTOR — BACKGROUND WORKLOAD BENCHMARK TOOL            \n"
                  << "================================================================================\n"
                  << COLOR_RESET
                  << "  " << COLOR_BOLD << "PID:" << COLOR_RESET << "               " << getpid() << "\n"
                  << "  " << COLOR_BOLD << "Mode:" << COLOR_RESET << "              " << to_string(config_.mode) << "\n"
                  << "  " << COLOR_BOLD << "Worker Threads:" << COLOR_RESET << "    " << config_.threads << "\n"
                  << "  " << COLOR_BOLD << "Target Duration:" << COLOR_RESET << "   " 
                  << (config_.duration_sec > 0 ? (std::to_string(config_.duration_sec) + "s") : "Indefinite (Ctrl+C to stop)") << "\n"
                  << "  " << COLOR_BOLD << "Report Interval:" << COLOR_RESET << "   " << config_.report_interval_sec << "s\n"
                  << "  " << COLOR_BOLD << "Throttling Policy:" << COLOR_RESET << " Automatically tracked via cgroup v2 & ioprio\n"
                  << COLOR_CYAN << "--------------------------------------------------------------------------------\n"
                  << COLOR_RESET;
    }

    while (!stop_requested_.load()) {
        auto sleep_dur = std::chrono::duration<double>(config_.report_interval_sec);
        std::this_thread::sleep_for(sleep_dur);

        if (stop_requested_.load()) break;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        double delta_t = std::chrono::duration<double>(now - last_time).count();
        if (delta_t <= 0.0) delta_t = 0.001;

        uint64_t cur_ops = total_ops_.load(std::memory_order_relaxed);
        uint64_t cur_io = total_io_bytes_.load(std::memory_order_relaxed);

        uint64_t ops_delta = (cur_ops >= last_ops) ? (cur_ops - last_ops) : 0;
        uint64_t io_delta = (cur_io >= last_io_bytes) ? (cur_io - last_io_bytes) : 0;

        double rate_mops = (static_cast<double>(ops_delta) / delta_t) / 1e6;
        double io_rate_mb = (static_cast<double>(io_delta) / delta_t) / (1024.0 * 1024.0);

        last_time = now;
        last_ops = cur_ops;
        last_io_bytes = cur_io;

        // Query system cgroup and I/O priority state
        std::string cgroup = read_current_cgroup();
        IoPriorityClass prio_class = IoPriorityClass::NONE;
        int prio_data = 0;
        get_process_ioprio(0, prio_class, prio_data);

        bool throttled = check_is_throttled(cgroup, prio_class);

        if (!throttled && rate_mops > baseline_rate) {
            baseline_rate = rate_mops;
            has_baseline = true;
        }

        WorkloadSample sample;
        sample.timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        sample.elapsed_sec = elapsed;
        sample.ops_delta = ops_delta;
        sample.rate_mops = rate_mops;
        sample.io_bytes_delta = io_delta;
        sample.io_rate_mb_s = io_rate_mb;
        sample.cgroup = cgroup;
        sample.is_throttled = throttled;
        sample.ioprio_class = prio_class;
        sample.ioprio_data = prio_data;

        record_sample(sample);

        if (!config_.quiet) {
            int mins = static_cast<int>(elapsed) / 60;
            int secs = static_cast<int>(elapsed) % 60;

            std::ostringstream time_ss;
            time_ss << std::setfill('0') << std::setw(2) << mins << ":"
                    << std::setfill('0') << std::setw(2) << secs;

            std::cout << "  [" << time_ss.str() << "] ";

            if (throttled) {
                double drop = (has_baseline && baseline_rate > 0.0) 
                    ? std::max(0.0, (1.0 - (rate_mops / baseline_rate)) * 100.0) : 0.0;
                std::cout << COLOR_RED << COLOR_BOLD << "[THROTTLED] " << COLOR_RESET;
                std::cout << std::fixed << std::setprecision(1)
                          << "Compute: " << COLOR_YELLOW << std::setw(6) << rate_mops << " Mops/s" << COLOR_RESET;
                if (drop > 0.0) {
                    std::cout << " (" << COLOR_RED << "-" << drop << "%" << COLOR_RESET << ")";
                }
            } else {
                std::cout << COLOR_GREEN << COLOR_BOLD << "[UNTHROTTLED]" << COLOR_RESET;
                std::cout << std::fixed << std::setprecision(1)
                          << " Compute: " << COLOR_GREEN << std::setw(6) << rate_mops << " Mops/s" << COLOR_RESET;
            }

            if (config_.mode == WorkloadMode::IO || config_.mode == WorkloadMode::MIXED || io_rate_mb > 0.0) {
                std::cout << " | IO: " << std::fixed << std::setprecision(1) << std::setw(5) << io_rate_mb << " MB/s";
            }

            std::cout << " | Cgroup: " << (throttled ? COLOR_RED : COLOR_CYAN) << cgroup << COLOR_RESET;
            std::cout << " | IOPrio: " << to_string(prio_class) << "\n";
            std::cout.flush();
        }

        if (config_.duration_sec > 0 && elapsed >= config_.duration_sec) {
            stop();
            break;
        }
    }
}

void BgWorkloadApp::run() {
    if (!init()) {
        return;
    }

    is_running_.store(true);
    stop_requested_.store(false);

    pid_t pid = getpid();
    temp_files_.clear();

    // Spawn workers depending on mode
    if (config_.mode == WorkloadMode::CPU) {
        for (uint32_t i = 0; i < config_.threads; ++i) {
            workers_.emplace_back(&BgWorkloadApp::cpu_worker_thread, this, i);
        }
    } else if (config_.mode == WorkloadMode::IO) {
        for (uint32_t i = 0; i < config_.threads; ++i) {
            std::string tmp_path = config_.work_dir + "/bg_workload_" + std::to_string(pid) + "_" + std::to_string(i) + ".tmp";
            temp_files_.push_back(tmp_path);
            workers_.emplace_back(&BgWorkloadApp::io_worker_thread, this, i, tmp_path);
        }
    } else if (config_.mode == WorkloadMode::MEMORY) {
        for (uint32_t i = 0; i < config_.threads; ++i) {
            workers_.emplace_back(&BgWorkloadApp::memory_worker_thread, this, i);
        }
    } else if (config_.mode == WorkloadMode::MIXED) {
        uint32_t cpu_threads = std::max(1u, config_.threads / 2);
        uint32_t io_threads = std::max(1u, (config_.threads - cpu_threads) / 2);
        uint32_t mem_threads = std::max(1u, config_.threads - cpu_threads - io_threads);

        uint32_t tid = 0;
        for (uint32_t i = 0; i < cpu_threads; ++i, ++tid) {
            workers_.emplace_back(&BgWorkloadApp::cpu_worker_thread, this, tid);
        }
        for (uint32_t i = 0; i < io_threads; ++i, ++tid) {
            std::string tmp_path = config_.work_dir + "/bg_workload_" + std::to_string(pid) + "_" + std::to_string(tid) + ".tmp";
            temp_files_.push_back(tmp_path);
            workers_.emplace_back(&BgWorkloadApp::io_worker_thread, this, tid, tmp_path);
        }
        for (uint32_t i = 0; i < mem_threads; ++i, ++tid) {
            workers_.emplace_back(&BgWorkloadApp::memory_worker_thread, this, tid);
        }
    } else if (config_.mode == WorkloadMode::STRESS) {
        // Multi-vector contention architecture:
        // 1) Memory bus saturators (AVX2 streaming stores direct to DRAM, bypassing CPU cache)
        // 2) Synchronous NVMe I/O workers (fdatasync and block-layer write queues)
        // 3) CPU runqueue & ALU/FPU saturators (dense matrix multiplication + cache thrashing)
        uint32_t mem_threads = std::max(1u, config_.threads / 4);
        uint32_t io_threads = (config_.threads > 4) ? std::max(1u, config_.threads / 8) : 0;
        uint32_t cpu_threads = config_.threads - mem_threads - io_threads;

        uint32_t tid = 0;
        for (uint32_t i = 0; i < mem_threads; ++i, ++tid) {
            workers_.emplace_back(&BgWorkloadApp::mem_stream_worker_thread, this, tid);
        }
        for (uint32_t i = 0; i < io_threads; ++i, ++tid) {
            std::string tmp_path = config_.work_dir + "/bg_workload_" + std::to_string(pid) + "_" + std::to_string(tid) + ".tmp";
            temp_files_.push_back(tmp_path);
            workers_.emplace_back(&BgWorkloadApp::io_worker_thread, this, tid, tmp_path);
        }
        for (uint32_t i = 0; i < cpu_threads; ++i, ++tid) {
            workers_.emplace_back(&BgWorkloadApp::stress_worker_thread, this, tid);
        }
    }

    // Launch reporter thread
    reporter_thread_ = std::thread(&BgWorkloadApp::reporter_loop, this);

    // Wait for reporter thread to conclude (either duration reached or stop requested)
    if (reporter_thread_.joinable()) {
        reporter_thread_.join();
    }

    // Clean up worker threads
    stop_requested_.store(true);
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();

    is_running_.store(false);

    if (!config_.output_csv.empty()) {
        export_csv(config_.output_csv);
    }

    if (!config_.quiet) {
        print_summary();
    }
}

void BgWorkloadApp::stop() {
    stop_requested_.store(true);
}

WorkloadSummary BgWorkloadApp::get_summary() const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    WorkloadSummary sum;

    sum.total_ops = total_ops_.load();
    sum.total_io_bytes = total_io_bytes_.load();
    sum.total_samples = samples_.size();

    if (samples_.empty()) {
        return sum;
    }

    sum.total_duration_sec = samples_.back().elapsed_sec;

    double sum_unthrottled_mops = 0.0;
    size_t count_unthrottled = 0;
    double sum_throttled_mops = 0.0;
    size_t count_throttled = 0;

    double sum_unthrottled_io = 0.0;
    double sum_throttled_io = 0.0;

    for (const auto& s : samples_) {
        if (s.is_throttled) {
            count_throttled++;
            sum_throttled_mops += s.rate_mops;
            sum_throttled_io += s.io_rate_mb_s;
        } else {
            count_unthrottled++;
            sum_unthrottled_mops += s.rate_mops;
            sum_unthrottled_io += s.io_rate_mb_s;
        }
    }

    sum.throttled_samples = count_throttled;
    sum.throttled_time_sec = static_cast<double>(count_throttled) * config_.report_interval_sec;
    if (sum.total_duration_sec > 0.0) {
        sum.throttled_time_pct = (sum.throttled_time_sec / sum.total_duration_sec) * 100.0;
    }

    if (count_unthrottled > 0) {
        sum.avg_unthrottled_mops = sum_unthrottled_mops / static_cast<double>(count_unthrottled);
        sum.avg_unthrottled_io_mb_s = sum_unthrottled_io / static_cast<double>(count_unthrottled);
    }
    if (count_throttled > 0) {
        sum.avg_throttled_mops = sum_throttled_mops / static_cast<double>(count_throttled);
        sum.avg_throttled_io_mb_s = sum_throttled_io / static_cast<double>(count_throttled);
    }

    if (sum.avg_unthrottled_mops > 0.0 && sum.avg_throttled_mops >= 0.0) {
        sum.slowdown_pct = std::max(0.0, (1.0 - (sum.avg_throttled_mops / sum.avg_unthrottled_mops)) * 100.0);
    }

    return sum;
}

const std::vector<WorkloadSample>& BgWorkloadApp::get_samples() const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    return samples_;
}

bool BgWorkloadApp::export_csv(const std::string& path) const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    std::ofstream of(path);
    if (!of.is_open()) {
        ISP_LOG_ERROR("bg_workload: Cannot open CSV for writing: " << path);
        return false;
    }

    of << "timestamp,elapsed_sec,ops_delta,rate_mops,io_bytes_delta,io_rate_mb_s,cgroup,is_throttled,ioprio_class\n";
    for (const auto& s : samples_) {
        of << std::fixed << std::setprecision(3)
           << s.timestamp << ","
           << s.elapsed_sec << ","
           << s.ops_delta << ","
           << s.rate_mops << ","
           << s.io_bytes_delta << ","
           << s.io_rate_mb_s << ",\""
           << s.cgroup << "\","
           << (s.is_throttled ? 1 : 0) << ","
           << to_string(s.ioprio_class) << "\n";
    }

    if (!config_.quiet) {
        std::cout << "  " << COLOR_GREEN << "✓" << COLOR_RESET << " Metrics exported to CSV: " << path << "\n";
    }
    return true;
}

void BgWorkloadApp::print_summary() const {
    WorkloadSummary sum = get_summary();

    std::cout << "\n" << COLOR_BOLD << COLOR_CYAN
              << "================================================================================\n"
              << "                   BACKGROUND WORKLOAD EXECUTION SUMMARY                        \n"
              << "================================================================================\n"
              << COLOR_RESET
              << "  Total Duration:            " << std::fixed << std::setprecision(2) << sum.total_duration_sec << "s\n"
              << "  Workload Mode:             " << to_string(config_.mode) << " (" << config_.threads << " threads)\n"
              << "  Total Operations:          " << sum.total_ops << " ops\n";

    if (sum.total_io_bytes > 0) {
        double total_mb = static_cast<double>(sum.total_io_bytes) / (1024.0 * 1024.0);
        std::cout << "  Total I/O Transferred:     " << std::fixed << std::setprecision(1) << total_mb << " MB\n";
    }

    std::cout << "  ------------------------------------------------------------------------------\n"
              << "  Baseline Throughput:       " << COLOR_GREEN << std::fixed << std::setprecision(2) 
              << sum.avg_unthrottled_mops << " Mops/s" << COLOR_RESET << "\n";

    if (sum.throttled_samples > 0) {
        std::cout << "  Throttled Throughput:      " << COLOR_RED << std::fixed << std::setprecision(2)
                  << sum.avg_throttled_mops << " Mops/s" << COLOR_RESET << "\n"
                  << "  Throughput Suppression:    " << COLOR_BOLD << COLOR_RED << std::fixed << std::setprecision(1)
                  << sum.slowdown_pct << "% slowdown under protector" << COLOR_RESET << "\n"
                  << "  Throttled Duration:        " << std::fixed << std::setprecision(1)
                  << sum.throttled_time_sec << "s / " << sum.total_duration_sec << "s ("
                  << sum.throttled_time_pct << "% of total runtime)\n";
    } else {
        std::cout << "  Throttling Status:         " << COLOR_YELLOW 
                  << "No active protector throttling detected during this run\n"
                  << "                             (Interactive session protector was inactive or in dry-run)" << COLOR_RESET << "\n";
    }

    std::cout << COLOR_CYAN
              << "================================================================================\n"
              << COLOR_RESET << std::endl;
}

} // namespace isp
