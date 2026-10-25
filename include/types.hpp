#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <ostream>

namespace isp {

// Classification of processes
enum class ProcessClass {
    PROTECTED,  // Latency-sensitive interactive session processes
    NORMAL,     // Standard system / user processes (unmanaged)
    BACKGROUND  // Heavy batch / competing workloads
};

inline const char* to_string(ProcessClass pc) {
    switch (pc) {
        case ProcessClass::PROTECTED:  return "PROTECTED";
        case ProcessClass::NORMAL:     return "NORMAL";
        case ProcessClass::BACKGROUND: return "BACKGROUND";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, ProcessClass pc) {
    return os << to_string(pc);
}

// State machine for session protector
enum class SessionState {
    INACTIVE,       // No interactive session detected
    ACTIVE,         // Interactive session is actively holding device
    IN_HYSTERESIS   // Session recently disappeared; waiting out hysteresis timer
};

inline const char* to_string(SessionState state) {
    switch (state) {
        case SessionState::INACTIVE:      return "INACTIVE";
        case SessionState::ACTIVE:        return "ACTIVE";
        case SessionState::IN_HYSTERESIS: return "IN_HYSTERESIS";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, SessionState state) {
    return os << to_string(state);
}

// Resource shaping policy configuration
struct ResourcePolicy {
    uint32_t protected_cpu_weight{800};
    uint32_t protected_io_weight{800};
    uint32_t background_cpu_weight{20};
    uint32_t starvation_floor_cpu_weight{5};
    uint32_t aging_increment{10};
    uint32_t aging_interval_sec{15};
    uint32_t aging_ceiling_cpu_weight{100};
    uint32_t background_memory_high_percent{60};
    uint32_t poll_interval_ms{1000};
    uint32_t hysteresis_delay_sec{8};
    std::string video_device{"/dev/video"};
    bool detect_audio{true};
    bool detect_pipewire{true};
    std::string status_file{"/tmp/protector.status"};
    std::string cgroup_mount{"/sys/fs/cgroup"};
    std::string proc_mount{"/proc"};
    bool dry_run{false};
};

// Process snapshot representation
struct ProcessInfo {
    pid_t pid{0};
    pid_t ppid{0};
    std::string name;
    std::string cmdline;
    std::string original_cgroup;
    std::string current_cgroup;
    ProcessClass classification{ProcessClass::NORMAL};
};

} // namespace isp
