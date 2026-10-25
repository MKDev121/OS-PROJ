#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <chrono>

namespace isp {

struct ProcEntry {
    pid_t pid{0};
    pid_t ppid{0};
    std::string comm;
    std::string cmdline;
    std::string original_cgroup{"/"};
    std::string current_cgroup{"/"};
    ProcessClass classification{ProcessClass::NORMAL};
    bool holds_video_device{false};
    bool holds_audio_device{false};
    bool has_pipewire_stream{false};
    bool allowlisted{false};
    uint64_t start_time{0};
    std::vector<pid_t> children;
    std::chrono::steady_clock::time_point last_seen{std::chrono::steady_clock::now()};
};

struct ClassificationRules {
    std::vector<std::string> allowlist{
        "systemd", "sshd", "init", "systemd-journal", "Xorg", "wayland",
        "pipewire", "wireplumber", "pulseaudio", "dbus-daemon", "bash", "zsh", "protector", "systemd-udevd",
        "gnome-shell", "mutter", "plasma-workspace", "kwin", "kwin_x11", "kwin_wayland",
        "sway", "hyprland", "xfce4-session", "xfwm4", "gnome-terminal-server", "konsole"
    };
    std::vector<std::string> background_patterns{
        "stress-ng", "tar", "dd", "gzip", "bzip2", "xz", "cc1", "cc1plus",
        "make", "ninja", "rustc", "ffmpeg"
    };
    std::vector<std::string> protected_patterns{
        "fake_call", "zoom", "teams", "skype", "slack", "discord", "webex", "jitsi", "meet",
        "chrome", "google-chrome", "chromium", "firefox", "brave", "msedge", "opera", "vivaldi"
    };
    std::vector<std::string> force_protected_patterns;
    std::vector<std::string> excluded_patterns{
        "unity", "unityhub", "unityhub-bin", "UnityHelper", "Unity.Licensing.Client",
        "unreal", "godot", "blender", "code", "cursor", "pycharm", "idea", "clion",
        "steam", "lutris", "wine", "crashpad_handler"
    };
};

class ProcTable {
public:
    ProcTable();
    explicit ProcTable(ClassificationRules rules);

    // Safely refresh table by scanning procfs (injectable procRoot)
    bool refresh(const std::string& procRoot = "/proc",
                 const std::string& videoDevicePrefix = "/dev/video",
                 bool detectAudio = true,
                 bool detectPipewire = true);

    // Thread-safe accessors
    std::optional<ProcEntry> get_entry(pid_t pid) const;
    std::vector<ProcEntry> get_all_entries() const;
    std::vector<ProcEntry> get_by_class(ProcessClass pc) const;
    size_t size() const;

    // Process tree walking
    std::vector<pid_t> get_descendants(pid_t pid) const;
    std::vector<pid_t> get_ancestors(pid_t pid) const;
    pid_t get_session_root(pid_t pid) const;

    // Classification trigger
    void classify_all();
    ProcessClass classify_process(pid_t pid) const;

    // Rule management
    void set_rules(ClassificationRules rules);
    ClassificationRules get_rules() const;
    void add_allowlist_entry(const std::string& name);
    void add_background_pattern(const std::string& pattern);
    void add_protected_pattern(const std::string& pattern);
    void add_force_protected_pattern(const std::string& pattern);
    void add_excluded_pattern(const std::string& pattern);

    // Direct manual insertion for testbeds
    void upsert_entry(const ProcEntry& entry);
    bool remove_entry(pid_t pid);
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<pid_t, ProcEntry> entries_;
    ClassificationRules rules_;

    // Internal methods (assumes mutex_ is held)
    void rebuild_tree_locked();
    void classify_all_locked();
    bool matches_pattern(const std::string& text, const std::string& pattern) const;
    bool is_allowlisted_locked(const ProcEntry& entry) const;
    bool is_background_locked(const ProcEntry& entry) const;
    bool is_video_call_app_locked(const ProcEntry& entry) const;
    bool is_excluded_from_protection_locked(const ProcEntry& entry) const;
    bool is_same_app_family_locked(const ProcEntry& a, const ProcEntry& b) const;
    bool is_protected_candidate_locked(const ProcEntry& entry) const;
    bool is_force_protected_locked(const ProcEntry& entry) const;
    bool has_active_media_locked(const ProcEntry& entry) const;
    std::vector<pid_t> get_descendants_locked(pid_t pid) const;
    std::vector<pid_t> get_ancestors_locked(pid_t pid) const;
    pid_t get_session_root_locked(pid_t pid) const;
};

} // namespace isp
