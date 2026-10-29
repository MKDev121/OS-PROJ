#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <filesystem>

namespace isp {

class CgroupManager {
public:
    static constexpr const char* SLICE_NAME = "protector.slice";
    static constexpr const char* GROUP_PROTECTED = "protected";
    static constexpr const char* GROUP_BACKGROUND = "background";
    static constexpr const char* GROUP_NORMAL = "normal";

    explicit CgroupManager(std::string cgroup_root = "/sys/fs/cgroup",
                           bool dry_run = true,
                           std::string proc_root = "/proc");
    ~CgroupManager();

    // Prevent copies, allow moves
    CgroupManager(const CgroupManager&) = delete;
    CgroupManager& operator=(const CgroupManager&) = delete;
    CgroupManager(CgroupManager&&) noexcept;
    CgroupManager& operator=(CgroupManager&&) noexcept;

    // Initialization & teardown
    bool init_hierarchy();
    bool cleanup();
    bool cleanup_stale_hierarchy();

    // Resource control setters
    bool set_cpu_weight(const std::string& group_name, uint32_t weight);
    bool set_cpu_max(const std::string& group_name, uint32_t max_percent, uint32_t period_us = 100000);
    bool set_io_weight(const std::string& group_name, uint32_t weight);
    bool set_memory_high_bytes(const std::string& group_name, uint64_t bytes);
    bool set_memory_high_percent(const std::string& group_name, uint32_t percent);

    // Apply entire resource policy
    bool apply_policy(const ResourcePolicy& policy);

    // Process migration & restoration (with PID reuse safety)
    bool move_process(pid_t pid,
                      const std::string& target_group,
                      const std::string& hint_original_cgroup = "");
    bool restore_process(pid_t pid);
    void restore_all();

    // PID validation & lifecycle
    static uint64_t get_process_starttime(pid_t pid, const std::string& proc_root = "/proc");
    static bool is_process_alive(pid_t pid, const std::string& proc_root);
    bool is_process_alive(pid_t pid) const;

    // Path resolution
    std::string get_slice_path() const;
    std::string get_group_path(const std::string& group_name) const;

    // Query states
    bool is_dry_run() const { return dry_run_; }
    void set_dry_run(bool dry_run) { dry_run_ = dry_run; }
    const std::string& get_cgroup_root() const { return cgroup_root_; }

    // Action audit log (especially useful for dry-run verification)
    std::vector<std::string> get_action_log() const;
    void clear_action_log();

    // Tracked migrations: pid -> original_cgroup_path
    std::unordered_map<pid_t, std::string> get_tracked_migrations() const;

    // Detect total system memory in bytes
    uint64_t get_total_ram_bytes() const;

    // Read current cgroup for a pid from procfs
    std::string read_process_cgroup(pid_t pid) const;

    struct TrackedProcess {
        std::string original_cgroup;
        uint64_t start_time{0};
    };

private:
    std::string cgroup_root_;
    bool dry_run_{true};
    std::string proc_root_;

    mutable std::mutex mutex_;
    std::unordered_map<pid_t, TrackedProcess> original_cgroups_;
    std::vector<std::string> action_log_;

    // Internal filesystem helpers
    bool write_file(const std::filesystem::path& file_path, const std::string& content);
    bool create_dir(const std::filesystem::path& dir_path);
    bool remove_dir(const std::filesystem::path& dir_path);
    void log_action(const std::string& action);
};

} // namespace isp
