#include "session_detector.hpp"
#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>
#include <cstdlib>
#include <cstdio>

namespace fs = std::filesystem;

namespace isp {

SessionDetector::SessionDetector(std::string proc_root, std::string video_device_prefix)
    : proc_root_(std::move(proc_root)), video_device_prefix_(std::move(video_device_prefix)) {}

std::vector<pid_t> SessionDetector::find_session_pids() const {
    return findSessionPids(proc_root_, video_device_prefix_);
}

std::vector<pid_t> SessionDetector::findSessionPids(const std::string& procRoot,
                                                    const std::string& videoDevicePrefix) {
    std::set<pid_t> session_pids;
    std::error_code ec;

    fs::path root_path(procRoot);
    if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
        ISP_LOG_DEBUG("Procfs root does not exist or is not a directory: " << procRoot);
        return {};
    }

    auto dir_opts = fs::directory_options::skip_permission_denied;
    fs::directory_iterator it(root_path, dir_opts, ec);
    fs::directory_iterator end_it;

    if (ec) {
        ISP_LOG_WARN("Failed to open procfs directory: " << procRoot << " (" << ec.message() << ")");
        return {};
    }

    while (it != end_it) {
        const auto& entry = *it;
        std::string filename = entry.path().filename().string();

        // Check if directory name is numeric PID
        if (!filename.empty() && std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) {
            pid_t pid = 0;
            try {
                pid = static_cast<pid_t>(std::stol(filename));
            } catch (...) {
                pid = 0;
            }

            if (pid > 0) {
                if (is_holding_device(pid, procRoot, videoDevicePrefix)) {
                    session_pids.insert(pid);
                }
            }
        }

        // Safely advance iterator to handle process disappearance or permission changes
        it.increment(ec);
        if (ec) {
            // If an entry disappeared while iterating, clear error and continue
            ec.clear();
        }
    }

    return std::vector<pid_t>(session_pids.begin(), session_pids.end());
}

bool SessionDetector::is_holding_device(pid_t pid,
                                       const std::string& procRoot,
                                       const std::string& devicePrefix) {
    std::error_code ec;
    fs::path fd_dir = fs::path(procRoot) / std::to_string(pid) / "fd";

    if (!fs::exists(fd_dir, ec) || !fs::is_directory(fd_dir, ec) || ec) {
        return false;
    }

    auto dir_opts = fs::directory_options::skip_permission_denied;
    fs::directory_iterator it(fd_dir, dir_opts, ec);
    fs::directory_iterator end_it;

    if (ec) {
        // Permission denied or process exited
        return false;
    }

    std::string prefix = devicePrefix;
    if (!prefix.empty() && prefix.back() == '*') {
        prefix.pop_back();
    }

    while (it != end_it) {
        const auto& fd_entry = *it;
        std::error_code sym_ec;

        if (fd_entry.is_symlink(sym_ec) && !sym_ec) {
            fs::path target = fs::read_symlink(fd_entry.path(), sym_ec);
            if (!sym_ec) {
                std::string target_str = target.string();
                if (target_str.find(prefix) == 0) {
                    return true;
                }
            }
        }

        it.increment(ec);
        if (ec) {
            ec.clear();
        }
    }

    return false;
}

bool SessionDetector::is_holding_audio_device(pid_t pid, const std::string& procRoot) {
    std::error_code ec;
    fs::path fd_dir = fs::path(procRoot) / std::to_string(pid) / "fd";

    if (!fs::exists(fd_dir, ec) || !fs::is_directory(fd_dir, ec) || ec) {
        return false;
    }

    auto dir_opts = fs::directory_options::skip_permission_denied;
    fs::directory_iterator it(fd_dir, dir_opts, ec);
    fs::directory_iterator end_it;

    if (ec) {
        return false;
    }

    while (it != end_it) {
        const auto& fd_entry = *it;
        std::error_code sym_ec;

        if (fd_entry.is_symlink(sym_ec) && !sym_ec) {
            fs::path target = fs::read_symlink(fd_entry.path(), sym_ec);
            if (!sym_ec) {
                std::string target_str = target.string();
                // ALSA capture device pattern: /dev/snd/pcm*c ('c' indicates capture stream)
                if (target_str.find("/dev/snd/pcm") == 0 && target_str.back() == 'c') {
                    return true;
                }
            }
        }

        it.increment(ec);
        if (ec) {
            ec.clear();
        }
    }

    return false;
}

std::vector<pid_t> SessionDetector::find_pipewire_media_pids() {
    std::string runtime_dir;
    const char* env_xdg = std::getenv("XDG_RUNTIME_DIR");
    std::error_code ec;
    if (env_xdg && fs::exists(fs::path(env_xdg) / "pipewire-0", ec)) {
        runtime_dir = env_xdg;
    } else {
        if (fs::exists("/run/user", ec)) {
            for (const auto& entry : fs::directory_iterator("/run/user", ec)) {
                if (fs::exists(entry.path() / "pipewire-0", ec)) {
                    runtime_dir = entry.path().string();
                    break;
                }
            }
        }
    }

    if (runtime_dir.empty()) {
        return {};
    }

    std::string cmd = "XDG_RUNTIME_DIR=" + runtime_dir + " pw-dump 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {};
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    pclose(pipe);

    if (output.empty()) {
        return {};
    }

    return parse_pipewire_dump_pids(output);
}

std::vector<pid_t> SessionDetector::parse_pipewire_dump_pids(const std::string& json_str) {
    std::unordered_map<int64_t, pid_t> client_map;
    struct NodeInfo {
        int64_t client_id{-1};
        pid_t direct_pid{0};
        std::string media_class;
        std::string media_role;
    };
    std::vector<NodeInfo> nodes;

    size_t pos = 0;
    while (pos < json_str.size()) {
        size_t obj_start = json_str.find('{', pos);
        if (obj_start == std::string::npos) break;

        int brace_depth = 0;
        size_t obj_end = std::string::npos;
        for (size_t i = obj_start; i < json_str.size(); ++i) {
            if (json_str[i] == '{') {
                brace_depth++;
            } else if (json_str[i] == '}') {
                brace_depth--;
                if (brace_depth == 0) {
                    obj_end = i;
                    break;
                }
            }
        }
        if (obj_end == std::string::npos) break;

        std::string obj = json_str.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        auto extract_string = [&](const std::string& key) -> std::string {
            size_t kpos = obj.find("\"" + key + "\"");
            if (kpos == std::string::npos) return "";
            size_t colon = obj.find(':', kpos);
            if (colon == std::string::npos) return "";
            size_t q1 = obj.find('"', colon);
            if (q1 == std::string::npos) return "";
            size_t q2 = obj.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return obj.substr(q1 + 1, q2 - q1 - 1);
        };

        auto extract_int = [&](const std::string& key) -> int64_t {
            size_t kpos = obj.find("\"" + key + "\"");
            if (kpos == std::string::npos) return -1;
            size_t colon = obj.find(':', kpos);
            if (colon == std::string::npos) return -1;
            size_t num_start = colon + 1;
            while (num_start < obj.size() && (std::isspace(obj[num_start]) || obj[num_start] == '"')) num_start++;
            size_t num_end = num_start;
            while (num_end < obj.size() && (std::isdigit(obj[num_end]) || obj[num_end] == '-')) num_end++;
            if (num_start < num_end) {
                try {
                    return std::stoll(obj.substr(num_start, num_end - num_start));
                } catch (...) {
                    return -1;
                }
            }
            return -1;
        };

        std::string type = extract_string("type");
        int64_t id = extract_int("id");

        if (type == "PipeWire:Interface:Client") {
            int64_t app_pid = extract_int("application.process.id");
            if (app_pid <= 0) {
                app_pid = extract_int("pipewire.sec.pid");
            }
            if (id >= 0 && app_pid > 0) {
                client_map[id] = static_cast<pid_t>(app_pid);
            }
        } else if (type == "PipeWire:Interface:Node") {
            NodeInfo node;
            node.client_id = extract_int("client.id");
            int64_t direct = extract_int("application.process.id");
            if (direct > 0) {
                node.direct_pid = static_cast<pid_t>(direct);
            }
            node.media_class = extract_string("media.class");
            node.media_role = extract_string("media.role");
            nodes.push_back(node);
        }
    }

    std::set<pid_t> result;
    for (const auto& node : nodes) {
        bool is_capture = false;
        // Match client capture streams: Stream/Input/Audio or Stream/Input/Video
        if (node.media_class.find("Stream/Input/") == 0) {
            is_capture = true;
        } else if (node.media_role == "Communication" || node.media_role == "Phone") {
            // VoIP and WebRTC meeting streams (Google Meet, Zoom, Teams)
            is_capture = true;
        }

        if (is_capture) {
            if (node.direct_pid > 0) {
                result.insert(node.direct_pid);
            }
            if (node.client_id >= 0) {
                auto it = client_map.find(node.client_id);
                if (it != client_map.end() && it->second > 0) {
                    result.insert(it->second);
                }
            }
        }
    }

    return std::vector<pid_t>(result.begin(), result.end());
}


std::string SessionDetector::get_process_name(pid_t pid, const std::string& procRoot) {
    fs::path comm_path = fs::path(procRoot) / std::to_string(pid) / "comm";
    std::ifstream comm_file(comm_path);
    if (comm_file.is_open()) {
        std::string comm;
        if (std::getline(comm_file, comm)) {
            while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) {
                comm.pop_back();
            }
            if (!comm.empty()) {
                return comm;
            }
        }
    }

    // Fallback: /proc/<pid>/cmdline
    fs::path cmdline_path = fs::path(procRoot) / std::to_string(pid) / "cmdline";
    std::ifstream cmd_file(cmdline_path);
    if (cmd_file.is_open()) {
        std::string cmd;
        if (std::getline(cmd_file, cmd, '\0')) {
            auto slash_pos = cmd.find_last_of('/');
            if (slash_pos != std::string::npos) {
                return cmd.substr(slash_pos + 1);
            }
            return cmd;
        }
    }

    return "";
}

pid_t SessionDetector::get_process_ppid(pid_t pid, const std::string& procRoot) {
    fs::path status_path = fs::path(procRoot) / std::to_string(pid) / "status";
    std::ifstream status_file(status_path);
    if (!status_file.is_open()) {
        return 0;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("PPid:", 0) == 0) {
            size_t tab = line.find_first_of(" \t");
            if (tab != std::string::npos) {
                try {
                    return static_cast<pid_t>(std::stol(line.substr(tab)));
                } catch (...) {
                    return 0;
                }
            }
        }
    }

    return 0;
}

} // namespace isp
