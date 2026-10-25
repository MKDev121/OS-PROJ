#pragma once

#include <string>
#include <vector>
#include <sys/types.h>

namespace isp {

class SessionDetector {
public:
    explicit SessionDetector(std::string proc_root = "/proc",
                            std::string video_device_prefix = "/dev/video");

    // Scans proc_root and returns unique PIDs holding video_device_prefix open
    std::vector<pid_t> find_session_pids() const;

    // Free/static invocation satisfying required signature
    static std::vector<pid_t> findSessionPids(const std::string& procRoot = "/proc",
                                              const std::string& videoDevicePrefix = "/dev/video");

    // Inspection helpers
    static bool is_holding_device(pid_t pid,
                                  const std::string& procRoot = "/proc",
                                  const std::string& devicePrefix = "/dev/video");

    // Audio capture device inspection (/dev/snd/pcm*c)
    static bool is_holding_audio_device(pid_t pid, const std::string& procRoot = "/proc");

    // PipeWire stream detection
    static std::vector<pid_t> find_pipewire_media_pids();
    static std::vector<pid_t> parse_pipewire_dump_pids(const std::string& json_str);

    static std::string get_process_name(pid_t pid, const std::string& procRoot = "/proc");
    static pid_t get_process_ppid(pid_t pid, const std::string& procRoot = "/proc");

    const std::string& get_proc_root() const { return proc_root_; }
    void set_proc_root(const std::string& proc_root) { proc_root_ = proc_root; }

    const std::string& get_device_prefix() const { return video_device_prefix_; }
    void set_device_prefix(const std::string& prefix) { video_device_prefix_ = prefix; }

private:
    std::string proc_root_;
    std::string video_device_prefix_;
};

// Top-level function in isp namespace
inline std::vector<pid_t> findSessionPids(const std::string& procRoot = "/proc",
                                          const std::string& videoDevicePrefix = "/dev/video") {
    return SessionDetector::findSessionPids(procRoot, videoDevicePrefix);
}

} // namespace isp
