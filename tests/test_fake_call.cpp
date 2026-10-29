#include "test_framework.hpp"
#include "fake_call.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_CASE(FakeCall_DefaultConfig) {
    isp::FakeCallConfig cfg;
    ASSERT_EQ(cfg.device, "/dev/video10");
    ASSERT_NEAR(cfg.target_fps, 30.0, 0.001);
    ASSERT_EQ(cfg.duration_sec, 60u);
    ASSERT_EQ(cfg.output_csv, "fake_call_metrics.csv");
    ASSERT_NEAR(cfg.deadline_tolerance_ratio, 1.2, 0.001);
    ASSERT_TRUE(cfg.simulate_cpu_load);
    ASSERT_NEAR(cfg.load_ms, 14.0, 0.001);
}

TEST_CASE(FakeCall_CustomConfig) {
    isp::FakeCallConfig cfg;
    cfg.device = "/dev/video2";
    cfg.target_fps = 60.0;
    cfg.duration_sec = 5;
    cfg.output_csv = "custom.csv";
    cfg.deadline_tolerance_ratio = 1.1;
    cfg.simulate_cpu_load = false;

    isp::FakeCallApp app(cfg);
    ASSERT_EQ(app.get_config().device, "/dev/video2");
    ASSERT_NEAR(app.get_config().target_fps, 60.0, 0.001);
    ASSERT_EQ(app.get_config().duration_sec, 5u);
    ASSERT_EQ(app.get_total_frames(), 0u);
    ASSERT_EQ(app.get_deadline_miss_count(), 0u);
}

TEST_CASE(FakeCall_MetricCalculationsAndDeadlines) {
    isp::FakeCallConfig cfg;
    cfg.target_fps = 30.0; // nominal: 33.333ms, deadline threshold: 33.333 * 1.2 = 40.0ms
    cfg.deadline_tolerance_ratio = 1.2;

    isp::FakeCallApp app(cfg);

    // Frame 1: Initial frame (elapsed 0us) -> never considered missed
    app.record_frame(33.333, 0);

    // Frame 2: on time (33.0ms <= 40.0ms)
    app.record_frame(33.0, 33333);

    // Frame 3: missed deadline (48.5ms > 40.0ms)
    app.record_frame(48.5, 81833);

    // Frame 4: on time (34.0ms <= 40.0ms)
    app.record_frame(34.0, 115833);

    ASSERT_EQ(app.get_total_frames(), 4u);
    ASSERT_EQ(app.get_deadline_miss_count(), 1u);
    ASSERT_NEAR(app.get_deadline_miss_rate(), 25.0, 0.01);

    const auto& metrics = app.get_metrics();
    ASSERT_EQ(metrics[0].frame_number, 1u);
    ASSERT_FALSE(metrics[0].deadline_missed);
    ASSERT_NEAR(metrics[0].timestamp, 0.0, 0.0001);

    ASSERT_EQ(metrics[1].frame_number, 2u);
    ASSERT_FALSE(metrics[1].deadline_missed);
    ASSERT_NEAR(metrics[1].frame_interval_ms, 33.0, 0.001);

    ASSERT_EQ(metrics[2].frame_number, 3u);
    ASSERT_TRUE(metrics[2].deadline_missed);
    ASSERT_NEAR(metrics[2].frame_interval_ms, 48.5, 0.001);

    ASSERT_EQ(metrics[3].frame_number, 4u);
    ASSERT_FALSE(metrics[3].deadline_missed);
}

TEST_CASE(FakeCall_CsvExportFormat) {
    fs::path temp_csv = fs::temp_directory_path() / "test_fake_call_metrics_output.csv";
    std::error_code ec;
    fs::remove(temp_csv, ec);

    isp::FakeCallConfig cfg;
    cfg.target_fps = 30.0;
    isp::FakeCallApp app(cfg);

    app.record_frame(33.333, 0);
    app.record_frame(33.250, 33333);
    app.record_frame(52.100, 85433);

    bool export_ok = app.export_csv(temp_csv.string());
    ASSERT_TRUE(export_ok);
    ASSERT_TRUE(fs::exists(temp_csv));

    std::ifstream in(temp_csv);
    ASSERT_TRUE(in.is_open());

    std::string header;
    ASSERT_TRUE(std::getline(in, header));
    ASSERT_EQ(header, "timestamp,frame_number,frame_interval_ms,deadline_missed,jitter_ms");

    std::string line1, line2, line3;
    ASSERT_TRUE(std::getline(in, line1));
    ASSERT_TRUE(std::getline(in, line2));
    ASSERT_TRUE(std::getline(in, line3));

    // Verify row 3 has deadline_missed = 1
    std::stringstream ss3(line3);
    std::string ts, fn, interval, missed, jitter;
    std::getline(ss3, ts, ',');
    std::getline(ss3, fn, ',');
    std::getline(ss3, interval, ',');
    std::getline(ss3, missed, ',');
    std::getline(ss3, jitter, ',');

    ASSERT_EQ(fn, "3");
    ASSERT_EQ(missed, "1");

    in.close();
    fs::remove(temp_csv, ec);
}

TEST_CASE(FakeCall_DeviceLifecycleAndShortRun) {
    fs::path temp_dev = fs::temp_directory_path() / "dummy_fake_video.bin";
    {
        std::ofstream dummy(temp_dev, std::ios::binary);
        std::vector<char> data(8192, 0x42);
        dummy.write(data.data(), data.size());
    }

    isp::FakeCallConfig cfg;
    cfg.device = temp_dev.string();
    cfg.target_fps = 30.0;
    cfg.duration_sec = 1;
    cfg.output_csv = (fs::temp_directory_path() / "dummy_run_metrics.csv").string();
    cfg.simulate_cpu_load = false;

    isp::FakeCallApp app(cfg);

    // 1. Initialize and check device opened
    ASSERT_TRUE(app.init());
    int fd = app.get_device_fd();
    ASSERT_TRUE(fd >= 0);
    ASSERT_TRUE(fcntl(fd, F_GETFD) != -1); // Descriptor is open and valid

    // 2. Run 1-second benchmark
    app.run();

    // In 1 second at 30 FPS, expect 30 frames
    ASSERT_TRUE(app.get_total_frames() >= 29u);
    ASSERT_TRUE(app.get_total_frames() <= 31u);

    // 3. Stop should close descriptor
    app.stop();
    ASSERT_EQ(app.get_device_fd(), -1);

    // Verify CSV was created
    std::error_code ec;
    ASSERT_TRUE(fs::exists(cfg.output_csv));

    fs::remove(temp_dev, ec);
    fs::remove(cfg.output_csv, ec);
}
