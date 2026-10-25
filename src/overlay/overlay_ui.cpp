#include "overlay.hpp"
#include <imgui.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <string>
#include <iostream>

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
        } else if (config_.auto_expand_on_call && (status.state == "ACTIVE" || status.state == "IN_HYSTERESIS")) {
            set_expanded(true);
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
    current_status_ = reader_.poll();

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
    if (status.background_procs.empty()) {
        ImGui::TextDisabled("    (No heavy background batch workloads active)");
    } else {
        for (const auto& proc : status.background_procs) {
            ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "  * PID %d: %s", proc.pid, proc.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.12f, 1.0f), "[Clamped to %u shares, ioprio=IDLE]", status.bg_cpu_weight);
        }
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Card 3: USER PREFERENCES & CONTROLS
    // -------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 1.0f), "OVERLAY SETTINGS");

    ImGui::Checkbox("Auto-expand when video call joined", &config_.auto_expand_on_call);
    ImGui::SameLine(290.0f);
    
    if (ImGui::Checkbox("Always on top", &config_.always_on_top)) {
        glfwSetWindowAttrib(window_, GLFW_FLOATING, config_.always_on_top ? GLFW_TRUE : GLFW_FALSE);
    }

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Opacity", &config_.window_alpha, 0.40f, 1.00f, "%.2f");
    
    ImGui::SameLine(290.0f);
    if (reader_.is_mock_mode()) {
        if (ImGui::Button("Cycle State")) {
            if (status.state == "ACTIVE") {
                reader_.set_mock_mode(true, "IN_HYSTERESIS");
            } else if (status.state == "IN_HYSTERESIS") {
                reader_.set_mock_mode(true, "INACTIVE");
            } else {
                reader_.set_mock_mode(true, "ACTIVE");
            }
        }
    } else {
        ImGui::TextDisabled("Daemon PID: %d (%s)", status.daemon_pid, status.mode.c_str());
    }
}

} // namespace isp
