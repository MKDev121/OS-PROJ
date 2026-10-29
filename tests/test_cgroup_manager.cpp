#include "test_framework.hpp"
#include "cgroup_manager.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

class FakeCgroupFixture {
public:
    FakeCgroupFixture(const std::string& name) {
        root_path_ = fs::temp_directory_path() / ("isp_cg_test_" + name);
        std::error_code ec;
        fs::remove_all(root_path_, ec);
        fs::create_directories(root_path_, ec);
    }

    ~FakeCgroupFixture() {
        std::error_code ec;
        fs::remove_all(root_path_, ec);
    }

    const fs::path& path() const { return root_path_; }
    std::string str() const { return root_path_.string(); }

private:
    fs::path root_path_;
};

TEST_CASE(CgroupManager_DryRunAuditing) {
    isp::CgroupManager mgr("/sys/fs/cgroup", /*dry_run=*/true);

    ASSERT_TRUE(mgr.init_hierarchy());

    isp::ResourcePolicy pol;
    pol.protected_cpu_weight = 800;
    pol.protected_io_weight = 800;
    pol.background_cpu_weight = 20;
    pol.background_memory_high_percent = 60;
    ASSERT_TRUE(mgr.apply_policy(pol));

    ASSERT_TRUE(mgr.move_process(1234, "protected", "/user.slice/app.scope"));
    ASSERT_TRUE(mgr.move_process(5678, "background", "/user.slice/worker.scope"));

    // Verify migration tracking
    auto tracked = mgr.get_tracked_migrations();
    ASSERT_EQ(tracked.size(), 2u);
    ASSERT_EQ(tracked[1234], "/user.slice/app.scope");
    ASSERT_EQ(tracked[5678], "/user.slice/worker.scope");

    // Check action log to verify every single proposed file/folder mutation is recorded
    auto log = mgr.get_action_log();
    ASSERT_TRUE(!log.empty());

    bool saw_create_slice = false;
    bool saw_cpu_weight = false;
    bool saw_move_1234 = false;
    bool saw_move_5678 = false;

    for (const auto& entry : log) {
        if (entry.find("Create directory:") != std::string::npos &&
            entry.find("protector.slice") != std::string::npos) {
            saw_create_slice = true;
        }
        if (entry.find("Write \"800\" to") != std::string::npos &&
            entry.find("cpu.weight") != std::string::npos) {
            saw_cpu_weight = true;
        }
        if (entry.find("Move PID 1234 to") != std::string::npos) {
            saw_move_1234 = true;
        }
        if (entry.find("Move PID 5678 to") != std::string::npos) {
            saw_move_5678 = true;
        }
    }

    ASSERT_TRUE(saw_create_slice);
    ASSERT_TRUE(saw_cpu_weight);
    ASSERT_TRUE(saw_move_1234);
    ASSERT_TRUE(saw_move_5678);

    // Verify cleanup in dry-run
    ASSERT_TRUE(mgr.cleanup());
    ASSERT_EQ(mgr.get_tracked_migrations().size(), 0u);
}

TEST_CASE(CgroupManager_RealFilesystemMockOperations) {
    FakeCgroupFixture fixture("real_mock");

    // Run in REAL filesystem mode (dry_run = false) inside mock directory
    isp::CgroupManager mgr(fixture.str(), /*dry_run=*/false);

    // 1. Initialize hierarchy
    ASSERT_TRUE(mgr.init_hierarchy());

    fs::path slice_dir = fixture.path() / "protector.slice";
    fs::path prot_dir = slice_dir / "protected";
    fs::path bg_dir = slice_dir / "background";
    fs::path norm_dir = slice_dir / "normal";

    ASSERT_TRUE(fs::exists(slice_dir));
    ASSERT_TRUE(fs::exists(prot_dir));
    ASSERT_TRUE(fs::exists(bg_dir));
    ASSERT_TRUE(fs::exists(norm_dir));

    // Verify subtree control file
    fs::path subtree_ctl = slice_dir / "cgroup.subtree_control";
    ASSERT_TRUE(fs::exists(subtree_ctl));
    {
        std::ifstream in(subtree_ctl);
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "+cpu +io +memory +pids");
    }

    // 2. Set weights
    ASSERT_TRUE(mgr.set_cpu_weight("protected", 800));
    {
        std::ifstream in(prot_dir / "cpu.weight");
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "800");
    }

    ASSERT_TRUE(mgr.set_io_weight("protected", 750));
    {
        std::ifstream in(prot_dir / "io.weight");
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "750");
    }

    ASSERT_TRUE(mgr.set_cpu_weight("background", 20));
    {
        std::ifstream in(bg_dir / "cpu.weight");
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "20");
    }

    ASSERT_TRUE(mgr.set_memory_high_bytes("background", 536870912ULL)); // 512 MB
    {
        std::ifstream in(bg_dir / "memory.high");
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "536870912");
    }

    // 3. Process migration tracking
    ASSERT_TRUE(mgr.move_process(4321, "protected", "/user.slice/session-1.scope"));
    auto tracked = mgr.get_tracked_migrations();
    ASSERT_EQ(tracked.size(), 1u);
    ASSERT_EQ(tracked[4321], "/user.slice/session-1.scope");

    // Content of cgroup.procs in protected
    {
        std::ifstream in(prot_dir / "cgroup.procs");
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "4321");
    }

    // 4. Process restoration
    ASSERT_TRUE(mgr.restore_process(4321));
    ASSERT_EQ(mgr.get_tracked_migrations().size(), 0u);

    // 5. Cleanup removes child groups and slice
    ASSERT_TRUE(mgr.cleanup());
    ASSERT_FALSE(fs::exists(prot_dir));
    ASSERT_FALSE(fs::exists(bg_dir));
    ASSERT_FALSE(fs::exists(norm_dir));
    ASSERT_FALSE(fs::exists(slice_dir));
}

TEST_CASE(CgroupManager_SafetyAndGracefulErrors) {
    FakeCgroupFixture fixture("errors_safe");
    isp::CgroupManager mgr(fixture.str(), /*dry_run=*/false);

    // Moving to a non-existent group without init_hierarchy should fail gracefully without crashing
    ASSERT_FALSE(mgr.move_process(9999, "non_existent_group"));

    // Restoring an untracked PID should return true (noop)
    ASSERT_TRUE(mgr.restore_process(8888));
}

TEST_CASE(CgroupManager_SetCpuMaxQuota) {
    FakeCgroupFixture fixture("cpu_max_test");
    isp::CgroupManager mgr(fixture.str(), /*dry_run=*/false);
    ASSERT_TRUE(mgr.init_hierarchy());

    fs::path bg_dir = fixture.path() / "protector.slice" / "background";
    fs::path cpu_max_file = bg_dir / "cpu.max";

    // Test capping to 5% (5000us per 100000us period)
    ASSERT_TRUE(mgr.set_cpu_max("background", 5, 100000));
    {
        std::ifstream in(cpu_max_file);
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "5000 100000");
    }

    // Test resetting to unlimited (0% -> "max 100000")
    ASSERT_TRUE(mgr.set_cpu_max("background", 0, 100000));
    {
        std::ifstream in(cpu_max_file);
        std::string content;
        std::getline(in, content);
        ASSERT_EQ(content, "max 100000");
    }

    ASSERT_TRUE(mgr.cleanup());
}
