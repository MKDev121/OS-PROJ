#pragma once

#include "types.hpp"
#include "config.hpp"
#include "session_detector.hpp"
#include "proc_table.hpp"
#include "cgroup_manager.hpp"
#include "resource_shaper.hpp"
#include "clock.hpp"

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace isp {

class ProtectorDaemon {
public:
    explicit ProtectorDaemon(ResourcePolicy policy = ResourcePolicy{},
                           std::string pidfile_path = "/tmp/protector.pid",
                           std::shared_ptr<IClock> clock = std::make_shared<SystemClock>());
    ~ProtectorDaemon();

    ProtectorDaemon(const ProtectorDaemon&) = delete;
    ProtectorDaemon& operator=(const ProtectorDaemon&) = delete;

    // Lifecycle
    bool start();
    void stop();
    bool wait(int timeout_sec = 0);

    // Signal handling
    static void handle_signal(int signum);
    static int get_last_signal();
    static void reset_signal();

    // Status queries
    bool is_running() const { return running_.load(); }
    SessionState get_session_state() const;
    size_t get_active_sessions_count() const;
    size_t get_background_procs_count() const;
    uint32_t get_current_bg_cpu_weight() const;

    // Component accessors (useful for testing and monitoring)
    ProcTable& get_proc_table() { return proc_table_; }
    CgroupManager& get_cgroup_manager() { return cgroup_mgr_; }
    ResourceShaper& get_resource_shaper() { return shaper_; }

private:
    ResourcePolicy policy_;
    std::string pidfile_path_;
    int pidfile_fd_{-1};
    std::shared_ptr<IClock> clock_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    // Subsystems
    ProcTable proc_table_;
    CgroupManager cgroup_mgr_;
    ResourceShaper shaper_;

    // Threading & synchronization
    std::thread monitor_thread_;
    std::thread shaper_thread_;
    std::mutex state_mutex_;
    std::condition_variable cv_;

    // Shared state between monitor and shaper
    bool session_detected_{false};
    bool state_changed_{false};
    std::vector<pid_t> current_protected_pids_;
    std::vector<pid_t> current_background_pids_;

    // Worker loops
    void monitor_loop();
    void shaper_loop();

    // PID and status file management
    bool acquire_pidfile();
    void release_pidfile();
    void write_status_file();
    void remove_status_file();
};

} // namespace isp
