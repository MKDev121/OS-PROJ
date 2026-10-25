#include "test_framework.hpp"
#include "overlay.hpp"

#include <cmath>
#include <fstream>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_CASE(Overlay_ComputePercentages_ActiveCall) {
    isp::OverlayStatus status;
    status.state = "ACTIVE";
    status.prot_cpu_weight = 800;
    status.bg_cpu_weight = 20;
    status.norm_cpu_weight = 100;

    status.compute_percentages();

    // Total = 800 + 20 + 100 = 920
    // Video Call: 800 / 920 * 100 ~= 86.9565%
    // Background: 20 / 920 * 100 ~= 2.1739%
    // Critical:   100 / 920 * 100 ~= 10.8695%
    ASSERT_NEAR(status.video_call_pct, 86.9565f, 0.01f);
    ASSERT_NEAR(status.background_pct, 2.1739f, 0.01f);
    ASSERT_NEAR(status.critical_sys_pct, 10.8695f, 0.01f);

    float sum = status.video_call_pct + status.background_pct + status.critical_sys_pct;
    ASSERT_NEAR(sum, 100.0f, 0.01f);
}

TEST_CASE(Overlay_ComputePercentages_AgingProgression) {
    isp::OverlayStatus status;
    status.state = "ACTIVE";
    status.prot_cpu_weight = 800;
    status.bg_cpu_weight = 100; // Aged up from 20 to prevent starvation
    status.norm_cpu_weight = 100;

    status.compute_percentages();

    // Total = 800 + 100 + 100 = 1000
    // Video Call: 800 / 1000 = 80.0%
    // Background: 100 / 1000 = 10.0%
    // Critical:   100 / 1000 = 10.0%
    ASSERT_NEAR(status.video_call_pct, 80.0f, 0.01f);
    ASSERT_NEAR(status.background_pct, 10.0f, 0.01f);
    ASSERT_NEAR(status.critical_sys_pct, 10.0f, 0.01f);
}

TEST_CASE(Overlay_ComputePercentages_HysteresisGrace) {
    isp::OverlayStatus status;
    status.state = "IN_HYSTERESIS";
    status.prot_cpu_weight = 800;
    status.bg_cpu_weight = 20;
    status.norm_cpu_weight = 100;
    status.hysteresis_remaining_sec = 4;

    status.compute_percentages();

    // Should maintain protection percentages during grace period
    ASSERT_NEAR(status.video_call_pct, 86.9565f, 0.01f);
    ASSERT_NEAR(status.background_pct, 2.1739f, 0.01f);
    ASSERT_NEAR(status.critical_sys_pct, 10.8695f, 0.01f);
}

TEST_CASE(Overlay_ComputePercentages_InactiveState) {
    isp::OverlayStatus status;
    status.state = "INACTIVE";

    status.compute_percentages();

    // In inactive state, video call allocation is 0, normal tasks share resources fairly
    ASSERT_EQ(status.video_call_pct, 0.0f);
    ASSERT_EQ(status.background_pct, 50.0f);
    ASSERT_EQ(status.critical_sys_pct, 50.0f);
}

TEST_CASE(Overlay_ComputePercentages_StoppedDaemon) {
    isp::OverlayStatus status;
    status.state = "STOPPED";

    status.compute_percentages();

    ASSERT_EQ(status.video_call_pct, 0.0f);
    ASSERT_EQ(status.background_pct, 0.0f);
    ASSERT_EQ(status.critical_sys_pct, 100.0f);
}

TEST_CASE(Overlay_StatusReader_ParseStatusFile) {
    std::string test_pidfile = "/tmp/test_isp_overlay.pid";
    std::string test_statusfile = "/tmp/test_isp_overlay.status";

    // Write mock pidfile
    {
        std::ofstream pf(test_pidfile);
        pf << getpid() << "\n";
    }

    // Write mock statusfile
    {
        std::ofstream sf(test_statusfile);
        sf << "daemon_pid=" << getpid() << "\n";
        sf << "mode=REAL\n";
        sf << "state=ACTIVE\n";
        sf << "bg_cpu_weight=30\n";
        sf << "prot_cpu_weight=800\n";
        sf << "hysteresis_remaining_sec=0\n";
        sf << "active_sessions_count=1\n";
        sf << "background_procs_count=2\n";
        sf << "protected_pids=101:chrome,102:pipewire\n";
        sf << "background_pids=201:dd,202:gzip\n";
    }

    isp::StatusReader reader(test_pidfile, test_statusfile);
    isp::OverlayStatus status = reader.poll();

    ASSERT_TRUE(status.daemon_running);
    ASSERT_EQ(status.daemon_pid, getpid());
    ASSERT_EQ(status.mode, "REAL");
    ASSERT_EQ(status.state, "ACTIVE");
    ASSERT_EQ(status.bg_cpu_weight, 30u);
    ASSERT_EQ(status.prot_cpu_weight, 800u);

    ASSERT_EQ(status.protected_procs.size(), 2u);
    ASSERT_EQ(status.protected_procs[0].pid, 101);
    ASSERT_EQ(status.protected_procs[0].name, "chrome");
    ASSERT_EQ(status.protected_procs[1].pid, 102);
    ASSERT_EQ(status.protected_procs[1].name, "pipewire");

    ASSERT_EQ(status.background_procs.size(), 2u);
    ASSERT_EQ(status.background_procs[0].pid, 201);
    ASSERT_EQ(status.background_procs[0].name, "dd");
    ASSERT_EQ(status.background_procs[1].pid, 202);
    ASSERT_EQ(status.background_procs[1].name, "gzip");

    // Percentage: 800 / (800 + 30 + 100) = 800 / 930 ~= 86.02%
    ASSERT_NEAR(status.video_call_pct, 86.0215f, 0.01f);

    // Clean up
    fs::remove(test_pidfile);
    fs::remove(test_statusfile);
}

TEST_CASE(Overlay_StatusReader_MockSimulation) {
    isp::StatusReader reader("/tmp/nonexistent.pid", "/tmp/nonexistent.status");
    reader.set_mock_mode(true, "ACTIVE");

    isp::OverlayStatus status = reader.poll();
    ASSERT_TRUE(status.daemon_running);
    ASSERT_EQ(status.state, "ACTIVE");
    ASSERT_TRUE(status.video_call_pct > 80.0f);
    ASSERT_TRUE(status.protected_procs.size() > 0u);
    ASSERT_TRUE(status.background_procs.size() > 0u);
}
