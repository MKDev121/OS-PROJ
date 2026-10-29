#include "overlay.hpp"
#include "config.hpp"
#include <imgui.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <csignal>

namespace isp {

OverlayRenderer::OverlayRenderer(GLFWwindow* window, StatusReader& reader)
    : window_(window), reader_(reader) {
    init_style();
    apply_window_size();
}

void OverlayRenderer::init_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.10f, 0.14f, config_.window_alpha);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.15f, 0.20f, 0.75f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.12f, 0.17f, 0.96f);
    colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.30f, 0.40f, 0.65f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.19f, 0.25f, 0.80f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.25f, 0.33f, 0.90f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.25f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.10f, 0.13f, 0.18f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.10f, 0.14f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.26f, 0.32f, 0.42f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.42f, 0.55f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.45f, 0.54f, 0.68f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.06f, 0.76f, 0.54f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.06f, 0.76f, 0.54f, 0.85f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.06f, 0.76f, 0.54f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.23f, 0.31f, 0.85f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.25f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.14f, 0.55f, 0.40f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.18f, 0.23f, 0.31f, 0.80f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.25f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.14f, 0.55f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.22f, 0.27f, 0.36f, 0.65f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.32f, 0.40f, 0.52f, 0.80f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.06f, 0.76f, 0.54f, 1.00f);
}

void OverlayRenderer::set_expanded(bool exp) {
    if (config_.expanded != exp) {
        config_.expanded = exp;
        apply_window_size();
    }
}

void OverlayRenderer::apply_window_size() {
    if (!window_) return;
    int w = config_.expanded ? expanded_width_ : collapsed_width_;
    int h = config_.expanded ? expanded_height_ : collapsed_height_;
    glfwSetWindowSize(window_, w, h);
}

void OverlayRenderer::handle_state_transitions(const OverlayStatus& status) {
    if (prev_state_ == "INITIAL") {
        prev_state_ = status.state;
        if (config_.auto_hide_idle) {
            if (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS") {
                glfwShowWindow(window_);
                if (config_.auto_expand_on_call) {
                    set_expanded(true);
                }
            } else {
                glfwHideWindow(window_);
            }
        } else {
            glfwShowWindow(window_);
            if (config_.auto_expand_on_call && (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS")) {
                set_expanded(true);
            }
        }
        return;
    }

    // Auto-turn on / expand overlay when user joins any video call
    if ((prev_state_ == "INACTIVE" || prev_state_ == "STOPPED") && 
        (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS")) {
        
        glfwShowWindow(window_);
        glfwFocusWindow(window_);
        
        if (config_.auto_expand_on_call) {
            set_expanded(true);
        }
    } else if (config_.auto_hide_idle && 
               (prev_state_ == "ACTIVE" || prev_state_ == "IN_HYSTERESIS") &&
               (status.state == "INACTIVE" || status.state == "STOPPED")) {
        glfwHideWindow(window_);
    }

    prev_state_ = status.state;
}

void OverlayRenderer::render(float delta_time) {
    animation_time_ += delta_time;
    if (config_feedback_timer_ > 0.0f) {
        config_feedback_timer_ -= delta_time;
    }
    current_status_ = reader_.poll();

    // Synchronize initial configuration from live status
    if (!config_initialized_ && current_status_.daemon_running) {
        config_.prot_cpu_weight = static_cast<int>(current_status_.prot_cpu_weight);
        config_.bg_cpu_weight = static_cast<int>(current_status_.bg_cpu_weight);
        config_.throttle_games_and_engines = current_status_.throttle_games_and_engines;
        config_.throttle_heavy_background = current_status_.throttle_heavy_background;
        config_.poll_interval_ms = static_cast<int>(current_status_.poll_interval_ms);
        config_.starvation_floor = static_cast<int>(current_status_.starvation_floor_cpu_weight);
        config_.background_cpu_max_percent = static_cast<int>(current_status_.background_cpu_max_percent);
        if (!current_status_.custom_game_patterns.empty()) {
            std::snprintf(config_.custom_game_patterns, sizeof(config_.custom_game_patterns), "%s", current_status_.custom_game_patterns.c_str());
        }
        if (!current_status_.custom_background_patterns.empty()) {
            std::snprintf(config_.custom_background_patterns, sizeof(config_.custom_background_patterns), "%s", current_status_.custom_background_patterns.c_str());
        }
        config_initialized_ = true;
    }

    handle_state_transitions(current_status_);

    // Update window alpha in theme
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = config_.window_alpha;

    // Make the ImGui window cover the entire GLFW window seamlessly
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoCollapse;

    if (!config_.expanded) {
        flags |= ImGuiWindowFlags_NoScrollbar;
    }

    ImGui::Begin("##ISPOverlayMainWindow", nullptr, flags);

    // Seamless window dragging with cursor offset tracking
    static bool is_dragging = false;
    static int drag_start_wx = 0, drag_start_wy = 0;
    static double drag_start_mx = 0, drag_start_my = 0;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && 
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && 
        !ImGui::IsAnyItemHovered()) {
        is_dragging = true;
        glfwGetWindowPos(window_, &drag_start_wx, &drag_start_wy);
        glfwGetCursorPos(window_, &drag_start_mx, &drag_start_my);
    }

    if (is_dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double cur_mx, cur_my;
            glfwGetCursorPos(window_, &cur_mx, &cur_my);
            int dx = static_cast<int>(cur_mx - drag_start_mx);
            int dy = static_cast<int>(cur_my - drag_start_my);
            if (dx != 0 || dy != 0) {
                int cur_wx, cur_wy;
                glfwGetWindowPos(window_, &cur_wx, &cur_wy);
                glfwSetWindowPos(window_, cur_wx + dx, cur_wy + dy);
            }
        } else {
            is_dragging = false;
        }
    }

    if (!config_.expanded) {
        render_collapsed_pill(current_status_);
    } else {
        render_expanded_dashboard(current_status_);
    }

    ImGui::End();
}

void OverlayRenderer::render_collapsed_pill(const OverlayStatus& status) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    
    // Draw drag handle / status indicator
    float pulse = (std::sin(animation_time_ * 4.0f) + 1.0f) * 0.5f; // 0.0 to 1.0
    ImU32 status_color;
    std::string badge_text;

    if (status.state == "ACTIVE") {
        status_color = IM_COL32(16, 185, 129, 255); // Vibrant Emerald
        badge_text = "CALL PROTECTED";
    } else if (status.state == "IN_HYSTERESIS") {
        status_color = IM_COL32(234, 179, 8, 255); // Warm Gold
        badge_text = "GRACE: " + std::to_string(status.hysteresis_remaining_sec) + "s";
    } else if (status.state == "INACTIVE") {
        status_color = IM_COL32(100, 116, 139, 255); // Slate
        badge_text = "IDLE";
    } else {
        status_color = IM_COL32(239, 68, 68, 255); // Red
        badge_text = "DAEMON OFF";
    }

    // Glowing indicator circle (drawn with OpenGL vector geometry, not fonts)
    float circle_x = p0.x + 12.0f;
    float circle_y = p0.y + 16.0f;
    
    if (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS") {
        float halo_r = 7.0f + pulse * 4.0f;
        ImU32 halo_col = (status_color & 0x00FFFFFF) | (static_cast<ImU32>((1.0f - pulse) * 120.0f) << 24);
        draw_list->AddCircleFilled(ImVec2(circle_x, circle_y), halo_r, halo_col);
    }
    draw_list->AddCircleFilled(ImVec2(circle_x, circle_y), 5.5f, status_color);

    // Left block: Logo + Badge
    ImGui::SetCursorPosX(30.0f);
    ImGui::SetCursorPosY(10.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "[ISP Shield]");
    ImGui::SameLine();
    
    ImVec4 badge_vec = ImGui::ColorConvertU32ToFloat4(status_color);
    ImGui::TextColored(badge_vec, "%s", badge_text.c_str());

    // Middle block: Resource Breakdown summary
    ImGui::SameLine(185.0f);
    if (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS") {
        ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "%.0f%% Call", status.video_call_pct);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "%.0f%% BG", status.background_pct);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.14f, 0.72f, 0.88f, 1.0f), "%.0f%% Sys", status.critical_sys_pct);
    } else {
        ImGui::TextDisabled("Fair Share Allocation");
    }

    // Right block: Clean text button (no unicode question mark)
    ImGui::SameLine(ImGui::GetWindowWidth() - 96.0f);
    ImGui::SetCursorPosY(8.0f);
    if (ImGui::Button("[+] Details", ImVec2(88.0f, 26.0f))) {
        set_expanded(true);
    }

    // Subtle drag hint across bottom
    ImGui::SetCursorPosX(14.0f);
    ImGui::SetCursorPosY(36.0f);
    ImGui::TextDisabled(":: Drag anywhere to reposition HUD");
}

void OverlayRenderer::render_expanded_dashboard(const OverlayStatus& status) {
    // -------------------------------------------------------------------------
    // Top Bar Header
    // -------------------------------------------------------------------------
    ImGui::SetCursorPosY(10.0f);
    ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[ISP]");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "Session Resource Shield");

    // Daemon state indicator badge
    ImGui::SameLine();
    if (status.state == "ACTIVE") {
        ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[ACTIVE]");
    } else if (status.state == "IN_HYSTERESIS") {
        ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.15f, 1.0f), "[GRACE: %us]", status.hysteresis_remaining_sec);
    } else if (status.state == "INACTIVE") {
        ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.65f, 1.0f), "[STANDBY]");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f), "[STOPPED]");
    }

    // Header buttons (Clean text: Collapse & Close)
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    if (ImGui::Button("[-] Collapse", ImVec2(96.0f, 24.0f))) {
        set_expanded(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("[X]", ImVec2(32.0f, 24.0f))) {
        if (config_.auto_hide_idle) {
            glfwHideWindow(window_);
        } else {
            should_close_ = true;
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Card 1: RESOURCE ALLOCATION BREAKDOWN (Core requirement)
    // -------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 1.0f), "CPU & I/O BANDWIDTH DISTRIBUTION");
    
    // Proportional visual meter bar
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 bar_p0 = ImGui::GetCursorScreenPos();
        float bar_width = ImGui::GetContentRegionAvail().x;
        float bar_height = 14.0f;
        ImVec2 bar_p1 = ImVec2(bar_p0.x + bar_width, bar_p0.y + bar_height);

        // Background
        draw_list->AddRectFilled(bar_p0, bar_p1, IM_COL32(30, 36, 48, 255), 4.0f);

        float w_call = (status.video_call_pct / 100.0f) * bar_width;
        float w_bg = (status.background_pct / 100.0f) * bar_width;
        float w_sys = (status.critical_sys_pct / 100.0f) * bar_width;

        // Video call chunk (Emerald)
        if (w_call > 0.0f) {
            draw_list->AddRectFilled(bar_p0, ImVec2(bar_p0.x + w_call, bar_p1.y),
                                     IM_COL32(16, 185, 129, 255), 4.0f, ImDrawFlags_RoundCornersLeft);
        }
        // Background chunk (Amber)
        if (w_bg > 0.0f) {
            draw_list->AddRectFilled(ImVec2(bar_p0.x + w_call, bar_p0.y),
                                     ImVec2(bar_p0.x + w_call + w_bg, bar_p1.y),
                                     IM_COL32(245, 158, 11, 255));
        }
        // System chunk (Cyan)
        if (w_sys > 0.0f) {
            draw_list->AddRectFilled(ImVec2(bar_p0.x + w_call + w_bg, bar_p0.y),
                                     bar_p1,
                                     IM_COL32(6, 182, 212, 255), 4.0f, ImDrawFlags_RoundCornersRight);
        }

        ImGui::Dummy(ImVec2(bar_width, bar_height + 4.0f));
    }

    // 3 Detail Tiles
    ImGui::Columns(3, "AllocColumns", false);

    // Column 1: Video Call
    ImGui::BeginChild("TileCall", ImVec2(0, 100), true);
    ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "VIDEO CALL");
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "%.1f%%", status.video_call_pct);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Weight: %u", status.prot_cpu_weight);
    ImGui::TextDisabled("I/O: BE-0 (Priority)");
    ImGui::EndChild();
    ImGui::NextColumn();

    // Column 2: Background
    ImGui::BeginChild("TileBG", ImVec2(0, 100), true);
    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "BACKGROUND");
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "%.1f%%", status.background_pct);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Weight: %u (Aged)", status.bg_cpu_weight);
    ImGui::TextDisabled("I/O: IDLE Throttle");
    ImGui::EndChild();
    ImGui::NextColumn();

    // Column 3: Critical / System
    ImGui::BeginChild("TileSys", ImVec2(0, 100), true);
    ImGui::TextColored(ImVec4(0.14f, 0.72f, 0.88f, 1.0f), "CRITICAL / SYS");
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(0.14f, 0.72f, 0.88f, 1.0f), "%.1f%%", status.critical_sys_pct);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Weight: %u", status.norm_cpu_weight);
    ImGui::TextDisabled("OS, Audio & Display");
    ImGui::EndChild();
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Card 2: LIVE PROCESS INSPECTOR (Exclusive to Video Call & Call Helpers)
    // -------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 1.0f), "ACTIVE VIDEO CALL & WORKLOAD TELEMETRY");

    ImGui::BeginChild("ProcessInspector", ImVec2(0, 150), true);
    
    // Protected Calls
    ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[*] Protected Video Call Sessions (%zu)", status.protected_procs.size());
    if (status.protected_procs.empty()) {
        ImGui::TextDisabled("    (No active video call stream detected; monitoring camera/mic)");
    } else {
        for (const auto& proc : status.protected_procs) {
            ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "  * PID %d: %s", proc.pid, proc.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[Guaranteed %u shares]", status.prot_cpu_weight);
        }
    }

    ImGui::Spacing();

    // Throttled Workloads
    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "[-] Throttled Background Workloads (%zu)", status.background_procs.size());
    if (status.throttle_games_and_engines) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[Games/Engines: Throttled]");
    }
    if (status.background_procs.empty()) {
        ImGui::TextDisabled("    (No heavy background batch workloads active)");
    } else {
        for (const auto& proc : status.background_procs) {
            ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "  * PID %d: %s", proc.pid, proc.name.c_str());
            ImGui::SameLine();
            if (status.background_cpu_max_percent > 0) {
                ImGui::TextColored(ImVec4(0.96f, 0.35f, 0.35f, 1.0f), "[Hard Cap: %u%% of 1 core, ioprio=IDLE]", status.background_cpu_max_percent);
            } else {
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "[Clamped to %u shares, ioprio=IDLE]", status.bg_cpu_weight);
            }
        }
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Card 3: LIVE DAEMON & WORKLOAD CONFIGURATION
    // -------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 1.0f), "DYNAMIC QOS & WORKLOAD CONFIGURATION");

    ImGui::BeginChild("ConfigCard", ImVec2(0, 240), true);

    // Throttling Toggles
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "Background Throttling Rules:");
    ImGui::Checkbox("Throttle Games (Steam, Wine, Lutris)", &config_.throttle_games_and_engines);
    ImGui::SameLine(290.0f);
    ImGui::Checkbox("Heavy Tasks (Compilers, Torrents)", &config_.throttle_heavy_background);

    ImGui::Spacing();

    // Shaping Sliders
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Call Shares", &config_.prot_cpu_weight, 100, 1000);
    ImGui::SameLine(290.0f);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("BG Shares", &config_.bg_cpu_weight, 5, 200);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Grace (s)", &config_.hysteresis_delay_sec, 1, 30);
    ImGui::SameLine(290.0f);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Starvation Floor", &config_.starvation_floor, 5, 50);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Poll (ms)", &config_.poll_interval_ms, 100, 2000);
    ImGui::SameLine(290.0f);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Hard CPU Cap", &config_.background_cpu_max_percent, 0, 100, "%d%% (0=Off)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Absolute CFS quota (cpu.max) across ALL background threads.\n"
                          "e.g., 5%% caps background tasks to 5%% of ONE core,\n"
                          "making Unity/games visibly crawl even on 14+ core CPUs.");
    }

    ImGui::Spacing();

    // Custom pattern filters
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Extra Games", config_.custom_game_patterns, sizeof(config_.custom_game_patterns));
    ImGui::SameLine(290.0f);
    ImGui::SetNextItemWidth(170.0f);
    ImGui::InputText("Extra Heavy", config_.custom_background_patterns, sizeof(config_.custom_background_patterns));

    ImGui::Spacing();

    if (ImGui::Button("[Apply Settings to Daemon]", ImVec2(210.0f, 26.0f))) {
        apply_daemon_config();
    }
    ImGui::SameLine();
    if (ImGui::Button("[Reset Defaults]", ImVec2(130.0f, 26.0f))) {
        config_.throttle_games_and_engines = true;
        config_.throttle_heavy_background = true;
        config_.prot_cpu_weight = 800;
        config_.bg_cpu_weight = 20;
        config_.hysteresis_delay_sec = 8;
        config_.poll_interval_ms = 1000;
        config_.starvation_floor = 5;
        config_.background_cpu_max_percent = 5;
        config_.custom_game_patterns[0] = '\0';
        config_.custom_background_patterns[0] = '\0';
        apply_daemon_config();
    }

    if (config_feedback_timer_ > 0.0f) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.06f, 0.76f, 0.54f, 1.0f), "[OK] %s", config_feedback_message_.c_str());
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Card 4: OVERLAY DISPLAY & HUD PREFERENCES
    // -------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 1.0f), "OVERLAY DISPLAY SETTINGS");

    ImGui::Checkbox("Auto-hide when idle", &config_.auto_hide_idle);
    ImGui::SameLine(290.0f);
    ImGui::Checkbox("Auto-expand on call", &config_.auto_expand_on_call);

    if (ImGui::Checkbox("Always on top", &config_.always_on_top)) {
        glfwSetWindowAttrib(window_, GLFW_FLOATING, config_.always_on_top ? GLFW_TRUE : GLFW_FALSE);
    }
    ImGui::SameLine(290.0f);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Opacity", &config_.window_alpha, 0.40f, 1.00f, "%.2f");

    ImGui::Spacing();
    if (reader_.is_mock_mode()) {
        if (ImGui::Button("Cycle State (Mock)")) {
            if (status.state == "ACTIVE") {
                reader_.set_mock_mode(true, "IN_HYSTERESIS");
            } else if (status.state == "IN_HYSTERESIS") {
                reader_.set_mock_mode(true, "INACTIVE");
            } else {
                reader_.set_mock_mode(true, "ACTIVE");
            }
        }
    } else {
        ImGui::TextDisabled("Daemon Status: %s | PID: %d (%s) | Status: /tmp/protector.status",
                            status.state.c_str(), status.daemon_pid, status.mode.c_str());
    }
}

void OverlayRenderer::apply_daemon_config() {
    // 1. Write runtime command file for instant daemon polling
    std::ofstream cmd_file("/tmp/protector.cmd");
    if (cmd_file.is_open()) {
        cmd_file << "throttle_games_and_engines=" << (config_.throttle_games_and_engines ? "true" : "false") << "\n";
        cmd_file << "throttle_heavy_background=" << (config_.throttle_heavy_background ? "true" : "false") << "\n";
        cmd_file << "protected_cpu_weight=" << config_.prot_cpu_weight << "\n";
        cmd_file << "background_cpu_weight=" << config_.bg_cpu_weight << "\n";
        cmd_file << "hysteresis_delay_sec=" << config_.hysteresis_delay_sec << "\n";
        cmd_file << "starvation_floor_cpu_weight=" << config_.starvation_floor << "\n";
        cmd_file << "poll_interval_ms=" << config_.poll_interval_ms << "\n";
        cmd_file << "background_cpu_max_percent=" << config_.background_cpu_max_percent << "\n";
        cmd_file << "custom_game_patterns=" << config_.custom_game_patterns << "\n";
        cmd_file << "custom_background_patterns=" << config_.custom_background_patterns << "\n";
        cmd_file.close();
    }

    // 2. Persist to configuration file if exists
    std::vector<std::string> conf_paths = { "config/protector.conf", "/etc/isp/protector.conf" };
    for (const auto& path : conf_paths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            Config cfg;
            if (cfg.load_from_file(path)) {
                cfg.set("throttle_games_and_engines", config_.throttle_games_and_engines ? "true" : "false");
                cfg.set("throttle_heavy_background", config_.throttle_heavy_background ? "true" : "false");
                cfg.set("protected_cpu_weight", std::to_string(config_.prot_cpu_weight));
                cfg.set("background_cpu_weight", std::to_string(config_.bg_cpu_weight));
                cfg.set("hysteresis_delay_sec", std::to_string(config_.hysteresis_delay_sec));
                cfg.set("starvation_floor_cpu_weight", std::to_string(config_.starvation_floor));
                cfg.set("poll_interval_ms", std::to_string(config_.poll_interval_ms));
                cfg.set("background_cpu_max_percent", std::to_string(config_.background_cpu_max_percent));
                cfg.set("custom_game_patterns", config_.custom_game_patterns);
                cfg.set("custom_background_patterns", config_.custom_background_patterns);

                std::ofstream out(path);
                if (out.is_open()) {
                    for (const auto& [k, v] : cfg.get_all()) {
                        out << k << " = " << v << "\n";
                    }
                }
            }
        }
    }

    // 3. Send SIGHUP to running daemon for instant zero-latency reload
    if (current_status_.daemon_running && current_status_.daemon_pid > 0) {
        kill(current_status_.daemon_pid, SIGHUP);
        config_feedback_message_ = "Settings applied to daemon (PID " + std::to_string(current_status_.daemon_pid) + ")!";
    } else {
        config_feedback_message_ = "Settings saved to config file successfully.";
    }

    if (reader_.is_mock_mode()) {
        current_status_.prot_cpu_weight = static_cast<uint32_t>(config_.prot_cpu_weight);
        current_status_.bg_cpu_weight = static_cast<uint32_t>(config_.bg_cpu_weight);
        current_status_.throttle_games_and_engines = config_.throttle_games_and_engines;
        current_status_.throttle_heavy_background = config_.throttle_heavy_background;
        current_status_.starvation_floor_cpu_weight = static_cast<uint32_t>(config_.starvation_floor);
        current_status_.poll_interval_ms = static_cast<uint32_t>(config_.poll_interval_ms);
        current_status_.background_cpu_max_percent = static_cast<uint32_t>(config_.background_cpu_max_percent);
        current_status_.custom_game_patterns = config_.custom_game_patterns;
        current_status_.custom_background_patterns = config_.custom_background_patterns;
        current_status_.compute_percentages();
        config_feedback_message_ = "Settings applied in mock simulation mode!";
    }

    config_feedback_timer_ = 3.5f;
}

} // namespace isp
