#include "fake_call.hpp"
#include "logger.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#if defined(__linux__)
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#endif

namespace isp {

FakeCallApp::FakeCallApp(FakeCallConfig config)
    : config_(std::move(config)) {}

FakeCallApp::~FakeCallApp() {
    stop();
}

bool FakeCallApp::init() {
    if (device_fd_ >= 0) {
        return true;
    }

    device_fd_ = open(config_.device.c_str(), O_RDWR | O_NONBLOCK);
    if (device_fd_ < 0) {
        // Fallback to read-only non-blocking
        device_fd_ = open(config_.device.c_str(), O_RDONLY | O_NONBLOCK);
    }
    if (device_fd_ < 0) {
        // Fallback to blocking read-only
        device_fd_ = open(config_.device.c_str(), O_RDONLY);
    }

    if (device_fd_ < 0) {
        ISP_LOG_ERROR("FakeCallApp: Failed to open webcam device " << config_.device
                      << " (" << strerror(errno) << ")");
        return false;
    }

    ISP_LOG_INFO("FakeCallApp: Successfully opened " << config_.device
                 << " (fd: " << device_fd_ << "). Descriptor held open for session.");

#if defined(__linux__)
    struct v4l2_capability cap{};
    if (ioctl(device_fd_, VIDIOC_QUERYCAP, &cap) == 0) {
        ISP_LOG_INFO("FakeCallApp: V4L2 Device info: " << cap.card << " (driver: " << cap.driver
                     << ", bus_info: " << cap.bus_info << ")");
        
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (caps & V4L2_CAP_VIDEO_CAPTURE) {
            ISP_LOG_INFO("FakeCallApp:   V4L2 capability: VIDEO_CAPTURE supported");
        }
        if (caps & V4L2_CAP_READWRITE) {
            ISP_LOG_INFO("FakeCallApp:   V4L2 capability: Direct read() I/O supported");
        }
        if (caps & V4L2_CAP_STREAMING) {
            ISP_LOG_INFO("FakeCallApp:   V4L2 capability: Streaming I/O supported");
        }

        struct v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(device_fd_, VIDIOC_G_FMT, &fmt) == 0) {
            ISP_LOG_INFO("FakeCallApp:   Current format: " << fmt.fmt.pix.width << "x"
                         << fmt.fmt.pix.height << " (" << fmt.fmt.pix.sizeimage << " bytes/frame)");
        }
    } else {
        ISP_LOG_DEBUG("FakeCallApp: VIDIOC_QUERYCAP not supported or file-backed device ("
                      << strerror(errno) << ")");
    }
#endif

    if (config_.simulate_cpu_load && config_.load_ms > 0.0) {
        constexpr int WIDTH = 1280;
        constexpr int HEIGHT = 720;
        size_t frame_bytes = WIDTH * HEIGHT;
        if (current_frame_.empty()) {
            current_frame_.resize(frame_bytes, 0x80);
            ref_frame_.resize(frame_bytes, 0x7E);
            residual_.resize(frame_bytes, 0);
            for (size_t i = 0; i < frame_bytes; ++i) {
                current_frame_[i] = static_cast<uint8_t>((i * 17 + 31) & 0xFF);
                ref_frame_[i] = static_cast<uint8_t>((i * 19 + 7) & 0xFF);
            }
        }

        // Measure pass execution time across multiple trials to find unhindered baseline
        double min_pass_ms = 1e9;
        for (int trial = 0; trial < 4; ++trial) {
            auto t0 = std::chrono::steady_clock::now();
            uint64_t dummy = 0;
            for (int y = 0; y < HEIGHT; y += 16) {
                for (int x = 0; x < WIDTH; x += 16) {
                    int sad = 0;
                    for (int dy = 0; dy < 16; ++dy) {
                        int row = (y + dy) * WIDTH;
                        for (int dx = 0; dx < 16; ++dx) {
                            int idx = row + (x + dx);
                            int diff = static_cast<int>(current_frame_[idx]) - static_cast<int>(ref_frame_[idx]);
                            sad += std::abs(diff);
                            residual_[idx] = static_cast<int16_t>(diff);
                        }
                    }
                    dummy += sad;
                    for (int sby = 0; sby < 16; sby += 8) {
                        for (int sbx = 0; sbx < 16; sbx += 8) {
                            int32_t dct_sum = 0;
                            for (int v = 0; v < 8; ++v) {
                                int r_offset = (y + sby + v) * WIDTH + (x + sbx);
                                for (int u = 0; u < 8; ++u) {
                                    int16_t val = residual_[r_offset + u];
                                    dct_sum += (val * (u + 1) * (v + 1)) >> 2;
                                }
                            }
                            dummy += (dct_sum >> 4);
                        }
                    }
                }
            }
            (void)dummy;
            double pass_time = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (pass_time > 0.0001 && pass_time < min_pass_ms) {
                min_pass_ms = pass_time;
            }
        }

        if (min_pass_ms > 0.0001 && min_pass_ms < 1e8) {
            calibrated_iterations_ = std::max(1u, static_cast<uint32_t>(std::round(config_.load_ms / min_pass_ms)));
        } else {
            calibrated_iterations_ = 10;
        }
        ops_per_ms_ = static_cast<uint64_t>(1000.0 / std::max(min_pass_ms, 0.001));
    }

    return true;
}

void FakeCallApp::stop() {
    stop_requested_.store(true);
    if (device_fd_ >= 0) {
        close(device_fd_);
        device_fd_ = -1;
        ISP_LOG_INFO("FakeCallApp: Closed webcam device " << config_.device);
    }
}

void FakeCallApp::read_video_frame() {
    if (device_fd_ < 0) {
        return;
    }

    char frame_buf[4096];
    ssize_t bytes_read = ::read(device_fd_, frame_buf, sizeof(frame_buf));
    if (bytes_read > 0) {
        // If device returns data, update current frame buffer
        size_t to_copy = std::min(static_cast<size_t>(bytes_read), current_frame_.size());
        if (to_copy > 0) {
            std::memcpy(current_frame_.data(), frame_buf, to_copy);
        }
    } else if (bytes_read == 0) {
        // Reached end of file / buffer; rewind to simulate continuous video stream
        ::lseek(device_fd_, 0, SEEK_SET);
    } else {
        // Direct read() not supported or streaming V4L2 device; mutate frame pattern
        if (!current_frame_.empty()) {
            current_frame_[0] = static_cast<uint8_t>(current_frame_[0] + 1);
        }
    }
}

void FakeCallApp::process_simulated_frame() {
    if (!config_.simulate_cpu_load || config_.load_ms <= 0.0 || current_frame_.empty()) {
        return;
    }

    constexpr int WIDTH = 1280;
    constexpr int HEIGHT = 720;
    uint64_t dummy_acc = 0;

    for (uint32_t iter = 0; iter < calibrated_iterations_; ++iter) {
        // Process 16x16 macroblocks across 1280x720 video frame
        for (int y = 0; y < HEIGHT; y += 16) {
            for (int x = 0; x < WIDTH; x += 16) {
                int sad = 0;
                for (int dy = 0; dy < 16; ++dy) {
                    int row = (y + dy) * WIDTH;
                    for (int dx = 0; dx < 16; ++dx) {
                        int idx = row + (x + dx);
                        int diff = static_cast<int>(current_frame_[idx]) - static_cast<int>(ref_frame_[idx]);
                        sad += std::abs(diff);
                        residual_[idx] = static_cast<int16_t>(diff);
                    }
                }
                dummy_acc += sad;

                // 2D 8x8 block transform approximation on the 4 sub-blocks
                for (int sby = 0; sby < 16; sby += 8) {
                    for (int sbx = 0; sbx < 16; sbx += 8) {
                        int32_t dct_sum = 0;
                        for (int v = 0; v < 8; ++v) {
                            int r_offset = (y + sby + v) * WIDTH + (x + sbx);
                            for (int u = 0; u < 8; ++u) {
                                int16_t val = residual_[r_offset + u];
                                dct_sum += (val * (u + 1) * (v + 1)) >> 2;
                            }
                        }
                        dummy_acc += (dct_sum >> 4);
                    }
                }
            }
        }
    }
    (void)dummy_acc;
}

double FakeCallApp::get_deadline_miss_rate() const {
    if (metrics_.empty()) {
        return 0.0;
    }
    return (static_cast<double>(deadline_misses_) / static_cast<double>(metrics_.size())) * 100.0;
}

void FakeCallApp::record_frame(double interval_ms, uint64_t elapsed_us) {
    uint32_t frame_count = static_cast<uint32_t>(metrics_.size() + 1);
    double nominal_interval_ms = 1000.0 / config_.target_fps;
    double deadline_threshold_ms = nominal_interval_ms * config_.deadline_tolerance_ratio;

    bool missed = (frame_count > 1) && (interval_ms > deadline_threshold_ms);
    if (missed) {
        ++deadline_misses_;
    }

    double jitter = std::abs(interval_ms - nominal_interval_ms);
    double timestamp_sec = static_cast<double>(elapsed_us) / 1000000.0;

    metrics_.push_back({
        timestamp_sec,
        elapsed_us,
        frame_count,
        interval_ms,
        missed,
        jitter
    });
}

void FakeCallApp::run() {
    if (device_fd_ < 0) {
        if (!init()) {
            ISP_LOG_ERROR("FakeCallApp: Failed to initialize device. Aborting session.");
            return;
        }
    }

    double nominal_interval_ms = 1000.0 / config_.target_fps;
    double deadline_threshold_ms = nominal_interval_ms * config_.deadline_tolerance_ratio;

    uint32_t expected_total_frames = static_cast<uint32_t>(std::round(config_.target_fps * config_.duration_sec));
    metrics_.clear();
    metrics_.reserve(expected_total_frames + 60);
    deadline_misses_ = 0;
    stop_requested_.store(false);
    is_running_.store(true);

    ISP_LOG_INFO("FakeCallApp: Starting session (" << config_.duration_sec << "s @ "
                 << config_.target_fps << " FPS). Target frame interval: "
                 << nominal_interval_ms << "ms (deadline limit: "
                 << deadline_threshold_ms << "ms, expected frames: "
                 << expected_total_frames << ")...");

    auto session_start = std::chrono::steady_clock::now();
    auto session_end = session_start + std::chrono::seconds(config_.duration_sec);

    auto prev_frame_time = session_start;
    uint32_t frame_count = 0;

    while (!stop_requested_.load() &&
           frame_count < expected_total_frames &&
           std::chrono::steady_clock::now() < session_end) {
        auto frame_start = std::chrono::steady_clock::now();
        ++frame_count;

        // Calculate inter-frame interval
        double interval_ms = (frame_count == 1)
            ? nominal_interval_ms
            : std::chrono::duration<double, std::milli>(frame_start - prev_frame_time).count();

        // Check if deadline was missed
        bool missed = (frame_count > 1) && (interval_ms > deadline_threshold_ms);
        if (missed) {
            ++deadline_misses_;
        }

        double jitter = std::abs(interval_ms - nominal_interval_ms);
        uint64_t timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            frame_start - session_start).count();
        double timestamp_sec = static_cast<double>(timestamp_us) / 1000000.0;

        metrics_.push_back({
            timestamp_sec,
            timestamp_us,
            frame_count,
            interval_ms,
            missed,
            jitter
        });

        // Read video frame from device and simulate processing load
        read_video_frame();
        process_simulated_frame();

        prev_frame_time = frame_start;

        // Compute next target frame time using cumulative reference to prevent drift
        auto target_next_frame = session_start +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(frame_count * nominal_interval_ms));

        // Pacing sleep until next frame boundary
        if (target_next_frame > std::chrono::steady_clock::now()) {
            std::this_thread::sleep_until(target_next_frame);
        }

        // Live progress telemetry every ~1 second (target_fps frames)
        uint32_t interval_frames = static_cast<uint32_t>(config_.target_fps);
        if (interval_frames == 0) interval_frames = 30;
        if (frame_count % interval_frames == 0 || frame_count == 1) {
            double current_miss_rate = get_deadline_miss_rate();
            int secs = static_cast<int>(timestamp_sec);
            int mins = secs / 60;
            secs %= 60;

            std::cout << "  [" << std::setfill('0') << std::setw(2) << mins << ":"
                      << std::setfill('0') << std::setw(2) << secs << "] "
                      << "Frame #" << std::setw(4) << frame_count
                      << " | Interval: " << std::fixed << std::setprecision(1) << interval_ms << "ms"
                      << " | Drops: " << deadline_misses_ << " (" << std::setprecision(1) << current_miss_rate << "%)";

            if (missed || current_miss_rate > 10.0) {
                std::cout << " \033[1;31m[LAG / STUTTERING]\033[0m\n";
            } else {
                std::cout << " \033[1;32m[SMOOTH / PROTECTED]\033[0m\n";
            }
            std::cout.flush();
        }
    }

    is_running_.store(false);

    ISP_LOG_INFO("FakeCallApp: Session completed. Recorded " << metrics_.size()
                 << " frames with " << deadline_misses_ << " deadline misses ("
                 << get_deadline_miss_rate() << "%).");

    if (!config_.output_csv.empty()) {
        export_csv(config_.output_csv);
    }
}

bool FakeCallApp::export_csv(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        ISP_LOG_ERROR("FakeCallApp: Could not open CSV file for writing: " << path);
        return false;
    }

    out << "timestamp,frame_number,frame_interval_ms,deadline_missed,jitter_ms\n";
    for (const auto& m : metrics_) {
        out << std::fixed << std::setprecision(6) << m.timestamp << ","
            << m.frame_number << ","
            << std::setprecision(3) << m.frame_interval_ms << ","
            << (m.deadline_missed ? 1 : 0) << ","
            << m.jitter_ms << "\n";
    }

    ISP_LOG_INFO("FakeCallApp: Exported " << metrics_.size() << " frame metrics to " << path);
    return true;
}

void FakeCallApp::print_summary() const {
    if (metrics_.empty()) {
        std::cout << "No frame metrics collected.\n";
        return;
    }

    std::vector<double> intervals;
    intervals.reserve(metrics_.size());
    for (const auto& m : metrics_) {
        intervals.push_back(m.frame_interval_ms);
    }

    std::sort(intervals.begin(), intervals.end());

    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    double mean = sum / intervals.size();
    double min_val = intervals.front();
    double max_val = intervals.back();
    double p95 = intervals[static_cast<size_t>(intervals.size() * 0.95)];
    double p99 = intervals[static_cast<size_t>(intervals.size() * 0.99)];

    std::cout << "\n=======================================================\n"
              << "       Fake Video Call Metrics Summary\n"
              << "=======================================================\n"
              << "  Total Frames Recorded  : " << metrics_.size() << "\n"
              << "  Target FPS             : " << config_.target_fps << "\n"
              << "  Target Interval        : " << (1000.0 / config_.target_fps) << " ms\n"
              << "  Deadline Misses (drops): " << deadline_misses_
              << " (" << get_deadline_miss_rate() << "%)\n"
              << "  Mean Frame Interval    : " << mean << " ms\n"
              << "  Min Frame Interval     : " << min_val << " ms\n"
              << "  Max Frame Interval     : " << max_val << " ms\n"
              << "  P95 Frame Interval     : " << p95 << " ms\n"
              << "  P99 Frame Interval     : " << p99 << " ms\n"
              << "=======================================================\n\n";
}

} // namespace isp
