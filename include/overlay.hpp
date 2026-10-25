#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <utility>
#include <cstdint>

struct GLFWwindow;

namespace isp {

struct ProcessItem {
    pid_t pid = 0;
    std::string name;
};

struct OverlayStatus {
    bool daemon_running = false;
    pid_t daemon_pid = 0;
    std::string mode = "UNKNOWN";   // "REAL" or "DRY-RUN"
    std::string state = "STOPPED";  // "ACTIVE", "IN_HYSTERESIS", "INACTIVE", "STOPPED"
    
    uint32_t bg_cpu_weight = 20;
    uint32_t prot_cpu_weight = 800;
    uint32_t norm_cpu_weight = 100; // Standard cgroup v2 root/system default
    uint32_t hysteresis_remaining_sec = 0;
    
    std::vector<ProcessItem> protected_procs;
    std::vector<ProcessItem> background_procs;

    // Computed allocation percentages
    float video_call_pct = 0.0f;
    float background_pct = 0.0f;
    float critical_sys_pct = 0.0f;

    void compute_percentages();
};

class StatusReader {
public:
    StatusReader(std::string pidfile = "/tmp/protector.pid",
                 std::string status_file = "/tmp/protector.status");

    // Read status from files or mock data
    OverlayStatus poll();

    void set_mock_mode(bool enable, const std::string& mock_state = "ACTIVE");
    bool is_mock_mode() const { return mock_mode_; }

private:
    std::string pidfile_;
    std::string status_file_;
    bool mock_mode_ = false;
    std::string mock_state_ = "ACTIVE";
    std::chrono::steady_clock::time_point mock_start_time_;
};

struct OverlayConfig {
    bool expanded = false;
    bool auto_expand_on_call = true;
    bool auto_hide_idle = true;
    bool always_on_top = true;
    float window_alpha = 0.94f;
    int pos_x = -1;
    int pos_y = -1;
};

class OverlayRenderer {
public:
    OverlayRenderer(GLFWwindow* window, StatusReader& reader);

    void init_style();
    void render(float delta_time);

    bool should_close() const { return should_close_; }
    bool is_expanded() const { return config_.expanded; }
    void set_expanded(bool exp);
    void set_auto_hide_idle(bool hide) { config_.auto_hide_idle = hide; }
    bool is_auto_hide_idle() const { return config_.auto_hide_idle; }

private:
    void render_collapsed_pill(const OverlayStatus& status);
    void render_expanded_dashboard(const OverlayStatus& status);
    void handle_state_transitions(const OverlayStatus& status);
    void apply_window_size();

    GLFWwindow* window_ = nullptr;
    StatusReader& reader_;
    OverlayConfig config_;
    OverlayStatus current_status_;
    std::string prev_state_ = "INITIAL";
    bool should_close_ = false;
    float animation_time_ = 0.0f;

    const int collapsed_width_ = 490;
    const int collapsed_height_ = 62;
    const int expanded_width_ = 540;
    const int expanded_height_ = 520;
};

} // namespace isp
