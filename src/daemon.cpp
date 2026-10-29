#include "daemon.hpp"
#include "logger.hpp"
#include "ioprio.hpp"

#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
namespace isp {

static std::atomic<int> g_signal_received{0};
static std::atomic<ProtectorDaemon*> g_daemon_instance{nullptr};

void ProtectorDaemon::handle_signal(int signum) {
    // Pure async-signal-safe atomic store
    g_signal_received.store(signum);
}

int ProtectorDaemon::get_last_signal() {
    return g_signal_received.load();
}

void ProtectorDaemon::reset_signal() {
    g_signal_received.store(0);
}

ProtectorDaemon::ProtectorDaemon(ResourcePolicy policy,
                               std::string pidfile_path,
                               std::shared_ptr<IClock> clock)
    : policy_(std::move(policy)),
      pidfile_path_(std::move(pidfile_path)),
      clock_(std::move(clock)),
      cgroup_mgr_(policy_.cgroup_mount, policy_.dry_run, policy_.proc_mount),
      shaper_(policy_, clock_) {
    if (!clock_) {
        clock_ = std::make_shared<SystemClock>();
    }

    // Configure workload classification and throttling rules
    ClassificationRules rules = proc_table_.get_rules();
    rules.throttle_games_and_engines = policy_.throttle_games_and_engines;
    rules.throttle_heavy_background = policy_.throttle_heavy_background;

    if (!policy_.custom_game_patterns.empty()) {
        std::stringstream ss(policy_.custom_game_patterns);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = Config::trim(token);
            if (!token.empty()) {
                rules.game_patterns.push_back(token);
            }
        }
    }

    if (!policy_.custom_background_patterns.empty()) {
        std::stringstream ss(policy_.custom_background_patterns);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = Config::trim(token);
            if (!token.empty()) {
                rules.heavy_background_patterns.push_back(token);
            }
        }
    }

    proc_table_.set_rules(rules);
}

ProtectorDaemon::~ProtectorDaemon() {
    stop();
}

bool ProtectorDaemon::acquire_pidfile() {
    if (pidfile_path_.empty()) {
        return true;
    }

    pidfile_fd_ = open(pidfile_path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (pidfile_fd_ < 0) {
        ISP_LOG_ERROR("Could not open pidfile: " << pidfile_path_);
        return false;
    }

    if (flock(pidfile_fd_, LOCK_EX | LOCK_NB) < 0) {
        ISP_LOG_ERROR("Another instance of protector daemon is already running (locked " << pidfile_path_ << ")");
        close(pidfile_fd_);
        pidfile_fd_ = -1;
        return false;
    }

    if (ftruncate(pidfile_fd_, 0) == 0) {
        std::string pid_str = std::to_string(getpid()) + "\n";
        [[maybe_unused]] auto written = write(pidfile_fd_, pid_str.c_str(), pid_str.size());
    }

    return true;
}

void ProtectorDaemon::release_pidfile() {
    if (pidfile_fd_ >= 0) {
        flock(pidfile_fd_, LOCK_UN);
        close(pidfile_fd_);
        pidfile_fd_ = -1;
        unlink(pidfile_path_.c_str());
    }
}

bool ProtectorDaemon::start() {
    if (running_.load()) {
        return true;
    }

    if (!acquire_pidfile()) {
        return false;
    }

    // Register signal handlers
    reset_signal();
    g_daemon_instance.store(this);
    std::signal(SIGINT, ProtectorDaemon::handle_signal);
    std::signal(SIGTERM, ProtectorDaemon::handle_signal);
    std::signal(SIGHUP, ProtectorDaemon::handle_signal);

    // Hardening: Clean up any stale cgroup slices or stranded PIDs from past crashes
    ISP_LOG_INFO("Checking for and cleaning up any stale cgroups from prior runs...");
    cgroup_mgr_.cleanup_stale_hierarchy();

    ISP_LOG_INFO("Initializing cgroup hierarchy (mode: "
                 << (policy_.dry_run ? "DRY-RUN" : "ACTIVE") << ")...");
    if (!cgroup_mgr_.init_hierarchy()) {
        ISP_LOG_ERROR("Failed to initialize cgroup hierarchy");
        release_pidfile();
        return false;
    }

    cgroup_mgr_.apply_policy(policy_);

    running_.store(true);
    stop_requested_.store(false);

    // Launch worker threads
    shaper_thread_ = std::thread(&ProtectorDaemon::shaper_loop, this);
    monitor_thread_ = std::thread(&ProtectorDaemon::monitor_loop, this);

    ISP_LOG_INFO("Protector daemon threads started successfully (poll: "
                 << policy_.poll_interval_ms << "ms, hysteresis: "
                 << policy_.hysteresis_delay_sec << "s).");
    return true;
}

void ProtectorDaemon::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    ISP_LOG_INFO("Protector daemon stopping...");
    stop_requested_.store(true);
    cv_.notify_all();

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    if (shaper_thread_.joinable()) {
        shaper_thread_.join();
    }

    // Clean up cgroups and restore processes
    ISP_LOG_INFO("Restoring all processes and cleaning cgroups...");
    cgroup_mgr_.cleanup();

    release_pidfile();
    remove_status_file();
    g_daemon_instance.store(nullptr);
    ISP_LOG_INFO("Protector daemon shutdown complete.");
}

bool ProtectorDaemon::wait(int timeout_sec) {
    auto start_time = std::chrono::steady_clock::now();
    while (running_.load()) {
        int sig = g_signal_received.load();
        if (sig == SIGHUP) {
            ISP_LOG_INFO("Caught SIGHUP signal. Reloading daemon configuration...");
            reset_signal();
            reload_config();
        } else if (sig != 0) {
            ISP_LOG_INFO("Caught termination signal (" << sig << "). Initiating graceful daemon shutdown...");
            stop();
            return false;
        }

        if (timeout_sec > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= timeout_sec) {
                ISP_LOG_INFO("Daemon timeout reached (" << timeout_sec << "s). Stopping...");
                stop();
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    if (shaper_thread_.joinable()) {
        shaper_thread_.join();
    }
    return true;
}

bool ProtectorDaemon::reload_config(const std::string& custom_path) {
    std::string path = custom_path.empty() ? config_file_path_ : custom_path;
    Config config;
    if (!config.load_from_file(path)) {
        ISP_LOG_WARN("ProtectorDaemon::reload_config: could not read " << path);
        return false;
    }
    if (!config.has("dry_run")) {
        config.set("dry_run", policy_.dry_run ? "true" : "false");
    }
    ResourcePolicy new_policy = config.to_resource_policy();
    apply_policy(new_policy);
    ISP_LOG_INFO("ProtectorDaemon: Reloaded configuration from " << path);
    return true;
}

void ProtectorDaemon::apply_policy(const ResourcePolicy& new_policy) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        policy_ = new_policy;
        cgroup_mgr_.apply_policy(policy_);
        shaper_.set_policy(policy_);

        ClassificationRules rules;
        rules.throttle_games_and_engines = policy_.throttle_games_and_engines;
        rules.throttle_heavy_background = policy_.throttle_heavy_background;

        if (!policy_.custom_game_patterns.empty()) {
            std::stringstream ss(policy_.custom_game_patterns);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token = Config::trim(token);
                if (!token.empty()) rules.game_patterns.push_back(token);
            }
        }
        if (!policy_.custom_background_patterns.empty()) {
            std::stringstream ss(policy_.custom_background_patterns);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token = Config::trim(token);
                if (!token.empty()) rules.heavy_background_patterns.push_back(token);
            }
        }
        proc_table_.set_rules(rules);
        state_changed_ = true;
        cv_.notify_all();
    }
    write_status_file();
}

void ProtectorDaemon::check_and_apply_commands() {
    std::string cmd_path = "/tmp/protector.cmd";
    std::error_code ec;
    if (!fs::exists(cmd_path, ec)) {
        return;
    }

    Config cmd_cfg;
    if (cmd_cfg.load_from_file(cmd_path)) {
        ISP_LOG_INFO("ProtectorDaemon: Applying configuration command from " << cmd_path);
        ResourcePolicy updated_policy;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            updated_policy = policy_;
        }
        if (cmd_cfg.has("throttle_games_and_engines")) {
            updated_policy.throttle_games_and_engines = cmd_cfg.get_bool("throttle_games_and_engines", updated_policy.throttle_games_and_engines);
        }
        if (cmd_cfg.has("throttle_heavy_background")) {
            updated_policy.throttle_heavy_background = cmd_cfg.get_bool("throttle_heavy_background", updated_policy.throttle_heavy_background);
        }
        if (cmd_cfg.has("protected_cpu_weight")) {
            updated_policy.protected_cpu_weight = static_cast<uint32_t>(cmd_cfg.get_int("protected_cpu_weight", updated_policy.protected_cpu_weight));
        }
        if (cmd_cfg.has("protected_io_weight")) {
            updated_policy.protected_io_weight = static_cast<uint32_t>(cmd_cfg.get_int("protected_io_weight", updated_policy.protected_io_weight));
        }
        if (cmd_cfg.has("background_cpu_weight")) {
            updated_policy.background_cpu_weight = static_cast<uint32_t>(cmd_cfg.get_int("background_cpu_weight", updated_policy.background_cpu_weight));
        }
        if (cmd_cfg.has("hysteresis_delay_sec")) {
            updated_policy.hysteresis_delay_sec = static_cast<uint32_t>(cmd_cfg.get_int("hysteresis_delay_sec", updated_policy.hysteresis_delay_sec));
        }
        if (cmd_cfg.has("poll_interval_ms")) {
            updated_policy.poll_interval_ms = static_cast<uint32_t>(cmd_cfg.get_int("poll_interval_ms", updated_policy.poll_interval_ms));
        }
        if (cmd_cfg.has("starvation_floor_cpu_weight")) {
            updated_policy.starvation_floor_cpu_weight = static_cast<uint32_t>(cmd_cfg.get_int("starvation_floor_cpu_weight", updated_policy.starvation_floor_cpu_weight));
        }
        if (cmd_cfg.has("custom_game_patterns")) {
            updated_policy.custom_game_patterns = cmd_cfg.get_string("custom_game_patterns", updated_policy.custom_game_patterns);
        }
        if (cmd_cfg.has("custom_background_patterns")) {
            updated_policy.custom_background_patterns = cmd_cfg.get_string("custom_background_patterns", updated_policy.custom_background_patterns);
        }
        if (cmd_cfg.has("background_cpu_max_percent")) {
            updated_policy.background_cpu_max_percent = static_cast<uint32_t>(cmd_cfg.get_int("background_cpu_max_percent", updated_policy.background_cpu_max_percent));
        }
        apply_policy(updated_policy);
    }
    fs::remove(cmd_path, ec);
}

void ProtectorDaemon::monitor_loop() {
    while (!stop_requested_.load()) {
        check_and_apply_commands();

        // Refresh process table and classifications with audio and PipeWire detection
        proc_table_.refresh(policy_.proc_mount, policy_.video_device, policy_.detect_audio, policy_.detect_pipewire);

        auto protected_procs = proc_table_.get_by_class(ProcessClass::PROTECTED);
        auto bg_procs = proc_table_.get_by_class(ProcessClass::BACKGROUND);
        bool has_session = !protected_procs.empty();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            bool was_session = session_detected_;
            session_detected_ = has_session;

            current_protected_pids_.clear();
            for (const auto& p : protected_procs) {
                current_protected_pids_.push_back(p.pid);
            }

            current_background_pids_.clear();
            for (const auto& b : bg_procs) {
                current_background_pids_.push_back(b.pid);
            }

            if (was_session != has_session) {
                state_changed_ = true;
            }
        }

        write_status_file();

        // Notify shaper thread of fresh scan
        cv_.notify_one();

        // Sleep poll interval with early wakeup support
        std::unique_lock<std::mutex> wait_lock(state_mutex_);
        cv_.wait_for(wait_lock, std::chrono::milliseconds(policy_.poll_interval_ms), [this] {
            return stop_requested_.load();
        });
    }
}

void ProtectorDaemon::shaper_loop() {
    SessionState prev_state = SessionState::INACTIVE;

    while (!stop_requested_.load()) {
        bool has_session = false;
        std::vector<pid_t> prot_pids;
        std::vector<pid_t> bg_pids;

        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            // Wait for monitor scan update or 1s timeout for internal ticking
            cv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return stop_requested_.load() || state_changed_;
            });
            state_changed_ = false;

            has_session = session_detected_;
            prot_pids = current_protected_pids_;
            bg_pids = current_background_pids_;
        }

        if (stop_requested_.load()) {
            break;
        }

        // Step the state machine
        shaper_.update(has_session);
        SessionState cur_state = shaper_.get_state();
        write_status_file();

        if (cur_state == SessionState::ACTIVE) {
            // Apply shaped weights
            uint32_t bg_cpu = shaper_.get_background_cpu_weight();
            uint32_t prot_cpu = shaper_.get_protected_cpu_weight();
            cgroup_mgr_.set_cpu_weight(CgroupManager::GROUP_BACKGROUND, bg_cpu);
            cgroup_mgr_.set_cpu_weight(CgroupManager::GROUP_PROTECTED, prot_cpu);

            // Apply hard CPU bandwidth cap if configured
            if (policy_.background_cpu_max_percent > 0) {
                cgroup_mgr_.set_cpu_max(CgroupManager::GROUP_BACKGROUND, policy_.background_cpu_max_percent);
            }

            // Migrate protected processes
            for (pid_t pid : prot_pids) {
                auto entry_opt = proc_table_.get_entry(pid);
                std::string hint = entry_opt ? entry_opt->original_cgroup : "";
                cgroup_mgr_.move_process(pid, CgroupManager::GROUP_PROTECTED, hint);
                set_process_ioprio(pid, IoPriorityClass::BEST_EFFORT, 0, policy_.dry_run);
            }

            // Migrate background processes
            for (pid_t pid : bg_pids) {
                auto entry_opt = proc_table_.get_entry(pid);
                std::string hint = entry_opt ? entry_opt->original_cgroup : "";
                cgroup_mgr_.move_process(pid, CgroupManager::GROUP_BACKGROUND, hint);
                set_process_ioprio(pid, IoPriorityClass::IDLE, 7, policy_.dry_run);
            }

        } else if (cur_state == SessionState::IN_HYSTERESIS) {
            // In grace period: update aged weights if any, but keep migrations active
            uint32_t bg_cpu = shaper_.get_background_cpu_weight();
            cgroup_mgr_.set_cpu_weight(CgroupManager::GROUP_BACKGROUND, bg_cpu);

        } else if (cur_state == SessionState::INACTIVE) {
            // If we just exited protection, restore all processes to original groups
            if (prev_state != SessionState::INACTIVE) {
                ISP_LOG_INFO("Shaper: Restoring all processes to original cgroups and baseline weights...");
                cgroup_mgr_.set_cpu_max(CgroupManager::GROUP_BACKGROUND, 0); // Restore full CPU bandwidth
                cgroup_mgr_.restore_all();
                for (pid_t pid : bg_pids) {
                    set_process_ioprio(pid, IoPriorityClass::BEST_EFFORT, 4, policy_.dry_run);
                }
            }
        }

        prev_state = cur_state;
    }
}

SessionState ProtectorDaemon::get_session_state() const {
    return shaper_.get_state();
}

size_t ProtectorDaemon::get_active_sessions_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return current_protected_pids_.size();
}

size_t ProtectorDaemon::get_background_procs_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return current_background_pids_.size();
}

uint32_t ProtectorDaemon::get_current_bg_cpu_weight() const {
    return shaper_.get_background_cpu_weight();
}

void ProtectorDaemon::set_throttle_games_and_engines(bool enable) {
    policy_.throttle_games_and_engines = enable;
    proc_table_.set_throttle_games_and_engines(enable);
}

bool ProtectorDaemon::is_throttling_games_and_engines() const {
    return policy_.throttle_games_and_engines;
}

void ProtectorDaemon::set_throttle_heavy_background(bool enable) {
    policy_.throttle_heavy_background = enable;
    proc_table_.set_throttle_heavy_background(enable);
}

bool ProtectorDaemon::is_throttling_heavy_background() const {
    return policy_.throttle_heavy_background;
}

void ProtectorDaemon::write_status_file() {
    std::string status_file;
    bool dry_run;
    uint32_t starvation_floor_cpu_weight;
    uint32_t poll_interval_ms;
    std::string custom_game_patterns;
    std::string custom_background_patterns;
    bool throttle_games_and_engines;
    bool throttle_heavy_background;
    uint32_t background_cpu_max_percent;
    std::vector<pid_t> prot_pids;
    std::vector<pid_t> bg_pids;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (policy_.status_file.empty()) {
            return;
        }
        status_file = policy_.status_file;
        dry_run = policy_.dry_run;
        starvation_floor_cpu_weight = policy_.starvation_floor_cpu_weight;
        poll_interval_ms = policy_.poll_interval_ms;
        custom_game_patterns = policy_.custom_game_patterns;
        custom_background_patterns = policy_.custom_background_patterns;
        throttle_games_and_engines = policy_.throttle_games_and_engines;
        throttle_heavy_background = policy_.throttle_heavy_background;
        background_cpu_max_percent = policy_.background_cpu_max_percent;
        prot_pids = current_protected_pids_;
        bg_pids = current_background_pids_;
    }

    std::string tmp_path = status_file + ".tmp";
    std::ofstream out(tmp_path);
    if (!out.is_open()) {
        return;
    }

    out << "daemon_pid=" << getpid() << "\n";
    out << "mode=" << (dry_run ? "DRY-RUN" : "REAL") << "\n";
    out << "state=" << to_string(shaper_.get_state()) << "\n";
    out << "bg_cpu_weight=" << shaper_.get_background_cpu_weight() << "\n";
    out << "prot_cpu_weight=" << shaper_.get_protected_cpu_weight() << "\n";
    out << "hysteresis_remaining_sec=" << shaper_.get_remaining_hysteresis_seconds() << "\n";
    out << "throttle_games_and_engines=" << (throttle_games_and_engines ? "true" : "false") << "\n";
    out << "throttle_heavy_background=" << (throttle_heavy_background ? "true" : "false") << "\n";
    out << "background_cpu_max_percent=" << background_cpu_max_percent << "\n";
    out << "starvation_floor_cpu_weight=" << starvation_floor_cpu_weight << "\n";
    out << "poll_interval_ms=" << poll_interval_ms << "\n";
    out << "custom_game_patterns=" << custom_game_patterns << "\n";
    out << "custom_background_patterns=" << custom_background_patterns << "\n";

    out << "active_sessions_count=" << prot_pids.size() << "\n";
    out << "background_procs_count=" << bg_pids.size() << "\n";

    out << "protected_pids=";
    for (size_t i = 0; i < prot_pids.size(); ++i) {
        auto e = proc_table_.get_entry(prot_pids[i]);
        out << prot_pids[i] << ":" << (e ? e->comm : "unknown");
        if (i + 1 < prot_pids.size()) out << ",";
    }
    out << "\n";

    out << "background_pids=";
    for (size_t i = 0; i < bg_pids.size(); ++i) {
        auto e = proc_table_.get_entry(bg_pids[i]);
        out << bg_pids[i] << ":" << (e ? e->comm : "unknown");
        if (i + 1 < bg_pids.size()) out << ",";
    }
    out << "\n";

    out.close();
    std::error_code ec;
    fs::rename(tmp_path, status_file, ec);
}

void ProtectorDaemon::remove_status_file() {
    if (!policy_.status_file.empty()) {
        std::error_code ec;
        fs::remove(policy_.status_file, ec);
        fs::remove(policy_.status_file + ".tmp", ec);
    }
}

} // namespace isp
