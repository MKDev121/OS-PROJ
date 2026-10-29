#include "test_framework.hpp"
#include "bg_workload.hpp"
#include "proc_table.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE(BgWorkload_ConfigValidation) {
    isp::WorkloadConfig config;
    config.threads = 0; // should auto-resolve
    config.report_interval_sec = -1.0; // should default to 1.0

    isp::BgWorkloadApp app(config);
    auto resolved = app.get_config();

    ASSERT_TRUE(resolved.threads > 0);
    ASSERT_TRUE(resolved.report_interval_sec > 0.0);
    ASSERT_EQ(resolved.mode, isp::WorkloadMode::CPU);
}

TEST_CASE(BgWorkload_ThrottledDetection) {
    // Normal cgroups
    ASSERT_FALSE(isp::BgWorkloadApp::check_is_throttled("/user.slice/user-1000.slice", isp::IoPriorityClass::BEST_EFFORT));
    ASSERT_FALSE(isp::BgWorkloadApp::check_is_throttled("/", isp::IoPriorityClass::NONE));

    // Throttled in cgroup v2 protector slice
    ASSERT_TRUE(isp::BgWorkloadApp::check_is_throttled("/protector.slice/background", isp::IoPriorityClass::BEST_EFFORT));
    ASSERT_TRUE(isp::BgWorkloadApp::check_is_throttled("/sys/fs/cgroup/protector.slice/background", isp::IoPriorityClass::BEST_EFFORT));

    // Throttled via I/O Priority IDLE class
    ASSERT_TRUE(isp::BgWorkloadApp::check_is_throttled("/user.slice", isp::IoPriorityClass::IDLE));
}

TEST_CASE(BgWorkload_CurrentCgroupParsing) {
    fs::path temp_cgroup_file = fs::temp_directory_path() / "test_proc_self_cgroup.txt";
    std::ofstream of(temp_cgroup_file);
    of << "0::/protector.slice/background\n";
    of.close();

    std::string parsed = isp::BgWorkloadApp::read_current_cgroup(temp_cgroup_file.string());
    ASSERT_EQ(parsed, "/protector.slice/background");

    std::error_code ec;
    fs::remove(temp_cgroup_file, ec);
}

TEST_CASE(BgWorkload_SampleRecordingAndSummary) {
    isp::WorkloadConfig config;
    config.report_interval_sec = 1.0;
    config.mode = isp::WorkloadMode::CPU;

    isp::BgWorkloadApp app(config);

    // Record baseline samples (unthrottled)
    isp::WorkloadSample s1;
    s1.elapsed_sec = 1.0;
    s1.rate_mops = 400.0;
    s1.is_throttled = false;
    s1.cgroup = "/user.slice";
    app.record_sample(s1);

    isp::WorkloadSample s2;
    s2.elapsed_sec = 2.0;
    s2.rate_mops = 420.0;
    s2.is_throttled = false;
    s2.cgroup = "/user.slice";
    app.record_sample(s2);

    // Record throttled samples
    isp::WorkloadSample s3;
    s3.elapsed_sec = 3.0;
    s3.rate_mops = 42.0; // 90% drop
    s3.is_throttled = true;
    s3.cgroup = "/protector.slice/background";
    app.record_sample(s3);

    isp::WorkloadSample s4;
    s4.elapsed_sec = 4.0;
    s4.rate_mops = 40.0;
    s4.is_throttled = true;
    s4.cgroup = "/protector.slice/background";
    app.record_sample(s4);

    auto summary = app.get_summary();
    ASSERT_EQ(summary.total_samples, 4u);
    ASSERT_EQ(summary.throttled_samples, 2u);
    ASSERT_EQ(summary.avg_unthrottled_mops, 410.0);
    ASSERT_EQ(summary.avg_throttled_mops, 41.0);
    // Slowdown: (1 - 41.0 / 410.0) * 100 = 90.0%
    ASSERT_TRUE(summary.slowdown_pct >= 89.9 && summary.slowdown_pct <= 90.1);
    ASSERT_EQ(summary.throttled_time_sec, 2.0);
}

TEST_CASE(BgWorkload_CsvExport) {
    fs::path temp_csv = fs::temp_directory_path() / "test_workload_metrics.csv";

    isp::WorkloadConfig config;
    config.quiet = true;
    isp::BgWorkloadApp app(config);

    isp::WorkloadSample s;
    s.timestamp = 1700000000.0;
    s.elapsed_sec = 1.0;
    s.ops_delta = 500000;
    s.rate_mops = 0.5;
    s.io_bytes_delta = 1048576;
    s.io_rate_mb_s = 1.0;
    s.cgroup = "/protector.slice/background";
    s.is_throttled = true;
    s.ioprio_class = isp::IoPriorityClass::IDLE;
    app.record_sample(s);

    ASSERT_TRUE(app.export_csv(temp_csv.string()));
    ASSERT_TRUE(fs::exists(temp_csv));

    std::ifstream in(temp_csv);
    std::string header, line;
    std::getline(in, header);
    ASSERT_TRUE(header.find("timestamp,elapsed_sec") != std::string::npos);

    std::getline(in, line);
    ASSERT_TRUE(line.find("/protector.slice/background") != std::string::npos);
    ASSERT_TRUE(line.find("IDLE") != std::string::npos);

    std::error_code ec;
    fs::remove(temp_csv, ec);
}

TEST_CASE(BgWorkload_ProcTableClassification) {
    isp::ProcTable table;
    isp::ProcEntry entry;
    entry.pid = 4321;
    entry.ppid = 1;
    entry.comm = "bg_workload";
    entry.cmdline = "./bg_workload --mode cpu --threads 2";

    table.upsert_entry(entry);

    // Verify it is classified as BACKGROUND by Interactive Session Protector
    ASSERT_EQ(table.classify_process(4321), isp::ProcessClass::BACKGROUND);
}

TEST_CASE(BgWorkload_ExecutionLifecycle_CPU) {
    isp::WorkloadConfig config;
    config.mode = isp::WorkloadMode::CPU;
    config.threads = 2;
    config.duration_sec = 1; // 1 second run
    config.report_interval_sec = 0.5;
    config.quiet = true;

    isp::BgWorkloadApp app(config);
    app.run();

    auto summary = app.get_summary();
    ASSERT_TRUE(summary.total_ops > 0);
    ASSERT_TRUE(summary.total_samples >= 1);
}

TEST_CASE(BgWorkload_ExecutionLifecycle_IO) {
    fs::path temp_dir = fs::temp_directory_path() / "isp_workload_io_test";
    std::error_code ec;
    fs::create_directories(temp_dir, ec);

    isp::WorkloadConfig config;
    config.mode = isp::WorkloadMode::IO;
    config.threads = 1;
    config.duration_sec = 1;
    config.report_interval_sec = 0.5;
    config.work_dir = temp_dir.string();
    config.io_size_mb = 4;
    config.quiet = true;

    isp::BgWorkloadApp app(config);
    app.run();

    auto summary = app.get_summary();
    ASSERT_TRUE(summary.total_io_bytes > 0);

    fs::remove_all(temp_dir, ec);
}

TEST_CASE(BgWorkload_ExecutionLifecycle_Stress) {
    isp::WorkloadConfig config;
    config.mode = isp::WorkloadMode::STRESS;
    config.threads = 2;
    config.matrix_size = 64;
    config.duration_sec = 1;
    config.report_interval_sec = 0.5;
    config.quiet = true;

    isp::BgWorkloadApp app(config);
    app.run();

    auto summary = app.get_summary();
    ASSERT_TRUE(summary.total_ops > 0);
    ASSERT_TRUE(summary.total_samples >= 1);
}

