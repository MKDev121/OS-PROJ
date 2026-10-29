#pragma once

#include <sys/types.h>
#include <string>

namespace isp {

enum class IoPriorityClass {
    NONE = 0,
    REAL_TIME = 1,
    BEST_EFFORT = 2,
    IDLE = 3
};

inline const char* to_string(IoPriorityClass prio) {
    switch (prio) {
        case IoPriorityClass::NONE:        return "NONE";
        case IoPriorityClass::REAL_TIME:   return "REAL_TIME";
        case IoPriorityClass::BEST_EFFORT: return "BEST_EFFORT";
        case IoPriorityClass::IDLE:        return "IDLE";
    }
    return "UNKNOWN";
}

// Wrapper for Linux SYS_ioprio_set syscall
bool set_process_ioprio(pid_t pid, IoPriorityClass prio_class, int prio_data = 0, bool dry_run = true);

// Wrapper for Linux SYS_ioprio_get syscall
bool get_process_ioprio(pid_t pid, IoPriorityClass& prio_class, int& prio_data);

} // namespace isp
