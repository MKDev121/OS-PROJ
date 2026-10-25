#include "test_framework.hpp"
#include "proc_table.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

TEST_CASE(ProcTable_ProtectedProcess) {
    isp::ProcTable table;
    isp::ProcEntry entry;
    entry.pid = 1500;
    entry.ppid = 1;
    entry.comm = "fake_call";
    entry.cmdline = "./fake_call --device /dev/video10";
    entry.holds_video_device = true;

    table.upsert_entry(entry);

    ASSERT_EQ(table.classify_process(1500), isp::ProcessClass::PROTECTED);
    auto protected_list = table.get_by_class(isp::ProcessClass::PROTECTED);
    ASSERT_EQ(protected_list.size(), 1u);
    ASSERT_EQ(protected_list[0].pid, 1500);
}

TEST_CASE(ProcTable_AllowlistedProcess) {
    isp::ProcTable table;
    isp::ProcEntry ssh_entry;
    ssh_entry.pid = 820;
    ssh_entry.ppid = 1;
    ssh_entry.comm = "sshd";
    ssh_entry.cmdline = "/usr/sbin/sshd -D";

    table.upsert_entry(ssh_entry);

    auto entry_opt = table.get_entry(820);
    ASSERT_TRUE(entry_opt.has_value());
    ASSERT_TRUE(entry_opt->allowlisted);
    // Allowlisted processes must always stay NORMAL (never moved to background)
    ASSERT_EQ(table.classify_process(820), isp::ProcessClass::NORMAL);

    // Dynamic addition to allowlist
    isp::ProcEntry custom_proc;
    custom_proc.pid = 900;
    custom_proc.ppid = 1;
    custom_proc.comm = "custom_critical_daemon";
    table.upsert_entry(custom_proc);

    table.add_allowlist_entry("custom_critical_daemon");
    auto custom_opt = table.get_entry(900);
    ASSERT_TRUE(custom_opt.has_value());
    ASSERT_TRUE(custom_opt->allowlisted);
    ASSERT_EQ(table.classify_process(900), isp::ProcessClass::NORMAL);
}

TEST_CASE(ProcTable_BackgroundProcess) {
    isp::ProcTable table;

    // Background root process: stress-ng
    isp::ProcEntry root_stress;
    root_stress.pid = 2000;
    root_stress.ppid = 100;
    root_stress.comm = "stress-ng";
    root_stress.cmdline = "stress-ng --cpu 4";
    table.upsert_entry(root_stress);

    // Child worker: stress-ng-cpu
    isp::ProcEntry child_stress;
    child_stress.pid = 2001;
    child_stress.ppid = 2000;
    child_stress.comm = "stress-ng-cpu";
    child_stress.cmdline = "stress-ng-cpu [worker]";
    table.upsert_entry(child_stress);

    // Additional background workloads: tar and dd
    isp::ProcEntry tar_proc;
    tar_proc.pid = 2050;
    tar_proc.ppid = 100;
    tar_proc.comm = "tar";
    tar_proc.cmdline = "tar -czf backup.tar.gz /data";
    table.upsert_entry(tar_proc);

    ASSERT_EQ(table.classify_process(2000), isp::ProcessClass::BACKGROUND);
    ASSERT_EQ(table.classify_process(2001), isp::ProcessClass::BACKGROUND);
    ASSERT_EQ(table.classify_process(2050), isp::ProcessClass::BACKGROUND);

    auto bg_list = table.get_by_class(isp::ProcessClass::BACKGROUND);
    ASSERT_EQ(bg_list.size(), 3u);
}

TEST_CASE(ProcTable_NormalProcess) {
    isp::ProcTable table;
    isp::ProcEntry editor;
    editor.pid = 3000;
    editor.ppid = 100;
    editor.comm = "nano";
    editor.cmdline = "nano document.txt";
    table.upsert_entry(editor);

    ASSERT_EQ(table.classify_process(3000), isp::ProcessClass::NORMAL);
    auto normal_list = table.get_by_class(isp::ProcessClass::NORMAL);
    ASSERT_EQ(normal_list.size(), 1u);
    ASSERT_EQ(normal_list[0].pid, 3000);
}

TEST_CASE(ProcTable_BrowserLikeProcessTree) {
    isp::ProcTable table;

    // Main browser process
    isp::ProcEntry chrome_main;
    chrome_main.pid = 4000;
    chrome_main.ppid = 1; // Direct child of init/user session
    chrome_main.comm = "chrome";
    chrome_main.cmdline = "/opt/google/chrome/chrome";
    table.upsert_entry(chrome_main);

    // Browser GPU process
    isp::ProcEntry chrome_gpu;
    chrome_gpu.pid = 4001;
    chrome_gpu.ppid = 4000;
    chrome_gpu.comm = "chrome";
    chrome_gpu.cmdline = "/opt/google/chrome/chrome --type=gpu-process";
    table.upsert_entry(chrome_gpu);

    // Browser Renderer process (tab)
    isp::ProcEntry chrome_renderer;
    chrome_renderer.pid = 4002;
    chrome_renderer.ppid = 4000;
    chrome_renderer.comm = "chrome";
    chrome_renderer.cmdline = "/opt/google/chrome/chrome --type=renderer";
    table.upsert_entry(chrome_renderer);

    // Browser Media Utility process that actually holds /dev/video10 open
    isp::ProcEntry chrome_video;
    chrome_video.pid = 4003;
    chrome_video.ppid = 4000;
    chrome_video.comm = "chrome";
    chrome_video.cmdline = "/opt/google/chrome/chrome --type=utility --utility-sub-type=video_capture";
    chrome_video.holds_video_device = true;
    table.upsert_entry(chrome_video);

    // Grandchild worker spawned by renderer
    isp::ProcEntry chrome_worker;
    chrome_worker.pid = 4004;
    chrome_worker.ppid = 4002;
    chrome_worker.comm = "chrome";
    chrome_worker.cmdline = "/opt/google/chrome/chrome --type=worker";
    table.upsert_entry(chrome_worker);

    // Verification:
    // 1. Session root of the media process 4003 should be chrome_main (4000)
    ASSERT_EQ(table.get_session_root(4003), 4000);

    // 2. Descendants of 4000 should include 4001, 4002, 4003, 4004
    auto descendants = table.get_descendants(4000);
    ASSERT_EQ(descendants.size(), 4u);
    ASSERT_TRUE(std::find(descendants.begin(), descendants.end(), 4001) != descendants.end());
    ASSERT_TRUE(std::find(descendants.begin(), descendants.end(), 4002) != descendants.end());
    ASSERT_TRUE(std::find(descendants.begin(), descendants.end(), 4003) != descendants.end());
    ASSERT_TRUE(std::find(descendants.begin(), descendants.end(), 4004) != descendants.end());

    // 3. EVERY process in the browser tree must be PROTECTED
    ASSERT_EQ(table.classify_process(4000), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(4001), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(4002), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(4003), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(4004), isp::ProcessClass::PROTECTED);

    auto protected_list = table.get_by_class(isp::ProcessClass::PROTECTED);
    ASSERT_EQ(protected_list.size(), 5u);
}

TEST_CASE(ProcTable_ProcfsRefreshIntegration) {
    fs::path fake_proc = fs::temp_directory_path() / "isp_proctable_refresh_test";
    std::error_code ec;
    fs::remove_all(fake_proc, ec);
    fs::create_directories(fake_proc, ec);

    // Create process 5001: Video call app
    fs::create_directories(fake_proc / "5001" / "fd", ec);
    {
        std::ofstream comm(fake_proc / "5001" / "comm"); comm << "fake_call\n";
        std::ofstream status(fake_proc / "5001" / "status"); status << "Name:\tfake_call\nPPid:\t1\n";
        std::ofstream cg(fake_proc / "5001" / "cgroup"); cg << "0::/user.slice/app.scope\n";
        fs::create_symlink("/dev/video10", fake_proc / "5001" / "fd" / "3", ec);
    }

    // Create process 5002: Background compiler
    fs::create_directories(fake_proc / "5002" / "fd", ec);
    {
        std::ofstream comm(fake_proc / "5002" / "comm"); comm << "cc1plus\n";
        std::ofstream status(fake_proc / "5002" / "status"); status << "Name:\tcc1plus\nPPid:\t1\n";
        std::ofstream cg(fake_proc / "5002" / "cgroup"); cg << "0::/user.slice/build.scope\n";
        fs::create_symlink("/dev/null", fake_proc / "5002" / "fd" / "0", ec);
    }

    isp::ProcTable table;
    ASSERT_TRUE(table.refresh(fake_proc.string(), "/dev/video"));

    ASSERT_EQ(table.size(), 2u);
    ASSERT_EQ(table.classify_process(5001), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(5002), isp::ProcessClass::BACKGROUND);

    auto e1 = table.get_entry(5001);
    ASSERT_TRUE(e1.has_value());
    ASSERT_EQ(e1->current_cgroup, "/user.slice/app.scope");

    // Clean up
    fs::remove_all(fake_proc, ec);
}

TEST_CASE(ProcTable_IdleBrowserIsNotProtected) {
    isp::ProcTable table;

    // Main browser process
    isp::ProcEntry chrome_main;
    chrome_main.pid = 9000;
    chrome_main.ppid = 1;
    chrome_main.comm = "chrome";
    chrome_main.cmdline = "/opt/google/chrome/chrome";
    chrome_main.holds_video_device = false;
    chrome_main.holds_audio_device = false;
    chrome_main.has_pipewire_stream = false;
    table.upsert_entry(chrome_main);

    // Browser tab (reading Wikipedia)
    isp::ProcEntry chrome_renderer;
    chrome_renderer.pid = 9001;
    chrome_renderer.ppid = 9000;
    chrome_renderer.comm = "chrome";
    chrome_renderer.cmdline = "/opt/google/chrome/chrome --type=renderer";
    chrome_renderer.holds_video_device = false;
    chrome_renderer.holds_audio_device = false;
    chrome_renderer.has_pipewire_stream = false;
    table.upsert_entry(chrome_renderer);

    // Idle browser MUST remain NORMAL (must NOT trigger active session protection!)
    ASSERT_EQ(table.classify_process(9000), isp::ProcessClass::NORMAL);
    ASSERT_EQ(table.classify_process(9001), isp::ProcessClass::NORMAL);

    auto protected_list = table.get_by_class(isp::ProcessClass::PROTECTED);
    ASSERT_EQ(protected_list.size(), 0u);
}

TEST_CASE(ProcTable_AudioCaptureCallProtection) {
    isp::ProcTable table;

    // Main browser process
    isp::ProcEntry chrome_main;
    chrome_main.pid = 9100;
    chrome_main.ppid = 1;
    chrome_main.comm = "chrome";
    chrome_main.cmdline = "/opt/google/chrome/chrome";
    table.upsert_entry(chrome_main);

    // Browser audio recording process (e.g. Google Meet with camera off, mic on)
    isp::ProcEntry chrome_audio;
    chrome_audio.pid = 9102;
    chrome_audio.ppid = 9100;
    chrome_audio.comm = "chrome";
    chrome_audio.cmdline = "/opt/google/chrome/chrome --type=utility --utility-sub-type=audio";
    chrome_audio.holds_audio_device = true; // Mic capture active!
    table.upsert_entry(chrome_audio);

    // Entire browser tree MUST be PROTECTED
    ASSERT_EQ(table.classify_process(9100), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(9102), isp::ProcessClass::PROTECTED);

    auto protected_list = table.get_by_class(isp::ProcessClass::PROTECTED);
    ASSERT_EQ(protected_list.size(), 2u);
}

TEST_CASE(ProcTable_ForceProtectedProcess) {
    isp::ProcTable table;

    isp::ProcEntry custom_app;
    custom_app.pid = 9200;
    custom_app.ppid = 1;
    custom_app.comm = "custom_meeting_agent";
    custom_app.cmdline = "./custom_meeting_agent";
    table.upsert_entry(custom_app);

    // Before force protection -> NORMAL
    ASSERT_EQ(table.classify_process(9200), isp::ProcessClass::NORMAL);

    // Add to force-protected patterns
    table.add_force_protected_pattern("custom_meeting_agent");

    // After force protection -> PROTECTED
    ASSERT_EQ(table.classify_process(9200), isp::ProcessClass::PROTECTED);
}

TEST_CASE(ProcTable_ExcludesNonVideoCallAppsLikeUnityHub) {
    isp::ProcTable table;

    // Simulate Unity Hub processes
    isp::ProcEntry unityhub_main;
    unityhub_main.pid = 54046;
    unityhub_main.ppid = 5124; // child of desktop shell
    unityhub_main.comm = "unityhub-bin";
    unityhub_main.cmdline = "/usr/lib/unityhub/unityhub-bin";
    table.upsert_entry(unityhub_main);

    isp::ProcEntry unityhub_crashpad;
    unityhub_crashpad.pid = 54088;
    unityhub_crashpad.ppid = 54046;
    unityhub_crashpad.comm = "crashpad_handler";
    unityhub_crashpad.cmdline = "/usr/lib/unityhub/chrome_crashpad_handler --monitor-self-annotation=ptype=crashpad-handler";
    table.upsert_entry(unityhub_crashpad);

    isp::ProcEntry unityhub_renderer;
    unityhub_renderer.pid = 54126;
    unityhub_renderer.ppid = 54046;
    unityhub_renderer.comm = "unityhub-bin";
    unityhub_renderer.cmdline = "/usr/lib/unityhub/unityhub-bin --type=renderer";
    table.upsert_entry(unityhub_renderer);

    // Also add an active video call process (e.g. Zoom)
    isp::ProcEntry zoom_call;
    zoom_call.pid = 60000;
    zoom_call.ppid = 5124;
    zoom_call.comm = "zoom";
    zoom_call.cmdline = "/opt/zoom/zoom";
    zoom_call.holds_video_device = true;
    table.upsert_entry(zoom_call);

    // Zoom must be PROTECTED
    ASSERT_EQ(table.classify_process(60000), isp::ProcessClass::PROTECTED);

    // All UnityHub processes MUST remain NORMAL (never protected)
    ASSERT_EQ(table.classify_process(54046), isp::ProcessClass::NORMAL);
    ASSERT_EQ(table.classify_process(54088), isp::ProcessClass::NORMAL);
    ASSERT_EQ(table.classify_process(54126), isp::ProcessClass::NORMAL);

    auto protected_list = table.get_by_class(isp::ProcessClass::PROTECTED);
    ASSERT_EQ(protected_list.size(), 1u);
    ASSERT_EQ(protected_list[0].pid, 60000);
}

TEST_CASE(ProcTable_StopsSessionClimbAtApplicationBoundary) {
    isp::ProcTable table;

    // Desktop shell (e.g. gnome-shell)
    isp::ProcEntry desktop_shell;
    desktop_shell.pid = 5124;
    desktop_shell.ppid = 1;
    desktop_shell.comm = "gnome-shell";
    desktop_shell.cmdline = "/usr/bin/gnome-shell";
    table.upsert_entry(desktop_shell);

    // Chrome browser main
    isp::ProcEntry chrome_main;
    chrome_main.pid = 70000;
    chrome_main.ppid = 5124; // child of gnome-shell
    chrome_main.comm = "chrome";
    chrome_main.cmdline = "/opt/google/chrome/chrome";
    table.upsert_entry(chrome_main);

    // Chrome video capture service (holds camera)
    isp::ProcEntry chrome_capture;
    chrome_capture.pid = 70001;
    chrome_capture.ppid = 70000; // child of chrome_main
    chrome_capture.comm = "chrome";
    chrome_capture.cmdline = "/opt/google/chrome/chrome --type=utility --utility-sub-type=video_capture";
    chrome_capture.holds_video_device = true;
    table.upsert_entry(chrome_capture);

    // Another app launched by gnome-shell (e.g. unityhub or text editor)
    isp::ProcEntry other_app;
    other_app.pid = 70002;
    other_app.ppid = 5124; // child of gnome-shell
    other_app.comm = "unityhub";
    other_app.cmdline = "/usr/lib/unityhub/unityhub";
    table.upsert_entry(other_app);

    // Session root of chrome_capture must be chrome_main (70000), NEVER gnome-shell (5124)
    ASSERT_EQ(table.get_session_root(70001), 70000);

    // Chrome processes are PROTECTED
    ASSERT_EQ(table.classify_process(70000), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(70001), isp::ProcessClass::PROTECTED);

    // gnome-shell and other_app must NOT be protected
    ASSERT_EQ(table.classify_process(5124), isp::ProcessClass::NORMAL);
    ASSERT_EQ(table.classify_process(70002), isp::ProcessClass::NORMAL);
}

TEST_CASE(ProcTable_ProtectsFullFirefoxProcessTree) {
    isp::ProcTable table;

    // Desktop shell (gnome-shell)
    isp::ProcEntry shell;
    shell.pid = 5124;
    shell.ppid = 1;
    shell.comm = "gnome-shell";
    shell.cmdline = "/usr/bin/gnome-shell";
    table.upsert_entry(shell);

    // Firefox main UI process
    isp::ProcEntry ff_main;
    ff_main.pid = 6708;
    ff_main.ppid = 5124;
    ff_main.comm = "firefox";
    ff_main.cmdline = "/snap/firefox/9036/usr/lib/firefox/firefox";
    table.upsert_entry(ff_main);

    // Firefox forkserver
    isp::ProcEntry ff_forkserver;
    ff_forkserver.pid = 6918;
    ff_forkserver.ppid = 6708;
    ff_forkserver.comm = "forkserver";
    ff_forkserver.cmdline = "/snap/firefox/9036/usr/lib/firefox/firefox -contentproc -parentPid 6708 forkserver";
    table.upsert_entry(ff_forkserver);

    // Firefox WebRTC video call tab (holds camera or PipeWire stream)
    isp::ProcEntry ff_tab;
    ff_tab.pid = 61585;
    ff_tab.ppid = 6918;
    ff_tab.comm = "Isolated Web Co";
    ff_tab.cmdline = "/snap/firefox/9036/usr/lib/firefox/firefox -contentproc -parentPid 6708 tab";
    ff_tab.has_pipewire_stream = true;
    table.upsert_entry(ff_tab);

    // Unrelated application (VS Code)
    isp::ProcEntry vscode;
    vscode.pid = 80000;
    vscode.ppid = 5124;
    vscode.comm = "code";
    vscode.cmdline = "/usr/share/code/code";
    table.upsert_entry(vscode);

    // Session root of the call tab must resolve to Firefox main (6708)
    ASSERT_EQ(table.get_session_root(61585), 6708);

    // The entire Firefox application family must be PROTECTED
    ASSERT_EQ(table.classify_process(6708), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(6918), isp::ProcessClass::PROTECTED);
    ASSERT_EQ(table.classify_process(61585), isp::ProcessClass::PROTECTED);

    // Shell and unrelated apps must NOT be protected
    ASSERT_EQ(table.classify_process(5124), isp::ProcessClass::NORMAL);
    ASSERT_EQ(table.classify_process(80000), isp::ProcessClass::NORMAL);
}

TEST_CASE(ProcTable_DoesNotMatchPathSubstrings) {
    isp::ProcTable table;

    // App located in /home/meet-kareliya/... (must not trigger video pattern 'meet')
    isp::ProcEntry local_app;
    local_app.pid = 88888;
    local_app.ppid = 1;
    local_app.comm = "my_worker";
    local_app.cmdline = "/home/meet-kareliya/projects/my_worker --active";
    local_app.holds_video_device = true; // Even if it holds video, binary name is not a video call app
    table.upsert_entry(local_app);

    // Must NOT be classified as PROTECTED because 'my_worker' is not a video call app
    ASSERT_EQ(table.classify_process(88888), isp::ProcessClass::NORMAL);
}

