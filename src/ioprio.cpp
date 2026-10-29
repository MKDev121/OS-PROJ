#include "ioprio.hpp"
#include "logger.hpp"

#if defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#include <cerrno>

#ifndef SYS_ioprio_set
#define SYS_ioprio_set 251
#endif

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_PRIO_VALUE(class_val, data_val) (((class_val) << IOPRIO_CLASS_SHIFT) | (data_val))
#define IOPRIO_WHO_PROCESS 1
#endif

namespace isp {

bool set_process_ioprio(pid_t pid, IoPriorityClass prio_class, int prio_data, bool dry_run) {
    if (dry_run) {
        ISP_LOG_DEBUG("[DRY-RUN] Would set IOPRIO for PID " << pid << " to " << to_string(prio_class) << " (data: " << prio_data << ")");
        return true;
    }

#if defined(__linux__)
    int ioprio_val = IOPRIO_PRIO_VALUE(static_cast<int>(prio_class), prio_data);
    long rc = syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, pid, ioprio_val);
    if (rc != 0) {
        ISP_LOG_DEBUG("ioprio_set failed for PID " << pid << " (errno: " << errno << ")");
        return false;
    }
    return true;
#else
    (void)pid;
    (void)prio_class;
    (void)prio_data;
    return true;
#endif
}

#ifndef SYS_ioprio_get
#define SYS_ioprio_get 252
#endif

bool get_process_ioprio(pid_t pid, IoPriorityClass& prio_class, int& prio_data) {
#if defined(__linux__)
    long rc = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, pid);
    if (rc < 0) {
        prio_class = IoPriorityClass::NONE;
        prio_data = 0;
        return false;
    }
    prio_class = static_cast<IoPriorityClass>((rc >> IOPRIO_CLASS_SHIFT) & 0x7);
    prio_data = static_cast<int>(rc & 0x1fff);
    return true;
#else
    (void)pid;
    prio_class = IoPriorityClass::NONE;
    prio_data = 0;
    return false;
#endif
}

} // namespace isp
