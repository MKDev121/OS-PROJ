# Interactive Session Protector

An Operating Systems daemon designed to protect latency-sensitive interactive sessions (such as video calls, online classes, telemedicine, or live interviews) from resource starvation caused by heavy concurrent background workloads (compilation, compression, batch direct-I/O, CPU stress).

---

## Documentation Quick Links
- **[Architecture & Algorithm Design](docs/ARCHITECTURE_AND_ALGORITHMS.md)**: Surface-level summary, subsystem breakdown, and detailed algorithms (session detection, classification, CFS bandwidth clamping, anti-starvation aging, hysteresis, cgroup restoration).
- **[Demonstration & Testing Guide](docs/DEMO_AND_TESTING_GUIDE.md)**: Step-by-step walkthrough for Test 1 (hard throttling) and Test 2 (video call protection & recovery).
- **[Environment Setup Guide](docs/ENVIRONMENT_SETUP.md)**: System prerequisites, kernel cgroup v2 setup, and dependencies.

---

## Architecture Overview

```
Linux Processes (/proc)
      │
      ▼
Session Detector (monitors open descriptors for /dev/video*)
      │
      ▼
Process Classifier (Tree-walking + Allowlist + Rule-based classification)
      │
      ▼
Cgroup v2 Manager (manages protector.slice hierarchy, migrations, restoration)
      │
      ▼
Resource Shaper (dynamic CPU weight, IO weight, memory.high, IO priority)
      ├── Starvation Floor (prevents total stall of background jobs)
      ├── Dynamic Aging (+10 weight every 15s)
      ├── Aging Ceiling (caps background allocation)
      └── Hysteresis (grace countdown to suppress camera reconnect flapping)
      │
      ▼
Protected Session (Low latency, 0% frame deadline misses)
   + Background Workloads (Smooth progress without starvation)
```

---

## Implementation Status (Phases 0 — 11 Complete)

- [x] **Phase 0: Environment Setup & Verification**
  - Ubuntu Linux environment verified (WSL2 / native Linux)
  - C++17 (`g++` with `-Wall -Wextra -Wpedantic`) & CMake
  - `cgroups v2` unified hierarchy with `cpu`, `io`, and `memory` controllers
  - Video capture test device (`/dev/video10`) & fallback node generator
  - Synthetic stress workloads (`stress-ng`, `tar`, `dd`) & Python analysis suite
- [x] **Phase 1: Build & Foundation (CMake, CLI, Logging)**
  - Clean modular architecture (`include/`, `src/`, `tests/`)
  - Targets: `protector`, `fake_call`, `bg_workload`, `isp_overlay`, `unit_tests`
  - Zero-dependency key=value configuration parser
- [x] **Phase 2: Session Detection (`/dev/video*` procfs scanner)**
  - Injectable `procRoot` support for testing
  - Robust scanning of `/proc/<pid>/fd/*` symlinks
  - Graceful handling of disappeared processes & permission errors
  - Multi-process detection and metadata lookup (`comm`, `status`, `PPid`)
- [x] **Phase 3: Process Classification & Allowlist**
  - Mutex-protected `ProcTable` with safe process lifecycle tracking
  - Parent/child tree walking (`get_ancestors`, `get_descendants`, `get_session_root`)
  - Browser-style multi-process tree protection
  - Three-class taxonomy (`PROTECTED`, `NORMAL`, `BACKGROUND`)
  - Strict allowlist & never-touch policy enforcement
- [x] **Phase 4: Cgroups v2 Manager (Creation, Migration, Restoration)**
  - `protector.slice` hierarchy (`protected`, `background`, `normal`)
  - Dynamic shaping: `cpu.weight`, `io.weight`, `memory.high`
  - PID migration tracking with original cgroup recording
  - Complete restoration of processes to original cgroups upon cleanup
  - Strict `--dry-run` default with transparent action audit logs
- [x] **Phase 5: Resource Shaper State Machine (Fairness & Aging)**
  - Three-state lifecycle (`INACTIVE`, `ACTIVE`, `IN_HYSTERESIS`)
  - Starvation floor enforcement on background workloads
  - Dynamic aging (+10 weight / 15s) with configurable ceiling cap
  - Hysteresis cancellation on transient reconnects
  - Full restoration upon hysteresis expiration
  - Injectable `MockClock` enabling instantaneous, sleep-free unit testing
  - Linux `ioprio_set` support (IDLE vs. Best Effort)
- [x] **Phase 6: Multi-Threaded Daemon Engine (Monitor + Shaper)**
  - Dual-thread daemon (`monitor_thread` + `shaper_thread`)
  - Condition variable thread communication with atomic state synchronization
  - Robust PID file locking (`flock`)
  - POSIX signal handling (`SIGINT`, `SIGTERM`, `SIGHUP`)
  - Graceful lifecycle teardown and total cgroup process restoration
  - Validated end-to-end with concurrent `fake_call` and `stress-ng`
- [x] **Phase 7: Fake Video Call Benchmark App (30 FPS metric collector)**
  - C++17 modular video workload holding `/dev/video10` descriptor open
  - High-precision drift-free 30 FPS pacing using `std::this_thread::sleep_until`
  - V4L2 device capability query and non-blocking read integration
  - Per-frame timestamp recording, interval measurement, and deadline miss detection
  - CSV export with `timestamp,frame_number,frame_interval_ms,deadline_missed,jitter_ms`
  - Validated ~1800 frames over 60 seconds with active session detection
- [x] **Phase 8: Benchmarking & Quantitative Evaluation Suite**
  - Automated experiment runner ([`scripts/run_experiments.py`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/scripts/run_experiments.py)) and shell wrapper ([`scripts/run_experiments.sh`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/scripts/run_experiments.sh))
  - Side-by-side comparison between **MODE A (Baseline - Protector Disabled)** and **MODE B (Protected - Protector Enabled)**
  - Fixed-size concurrent background workloads: `stress-ng` CPU bogo-ops, `tar` dataset compression, `dd` direct I/O
  - Metrics collected: frame drops, interval distributions (P50/P95/P99), jitter, and background job completion times
  - Comprehensive CSV exports: per-frame raw CSVs, per-run summaries, master `results/experiment_summary.csv`, and JSON metadata
  - Automated visualization: summary bar charts and cumulative distribution function (CDF) plots in `results/plots/`
- [x] **Phase 9: Data Analysis & Visualization Suite**
  - Python analysis engine ([`scripts/analyze_experiments.py`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/scripts/analyze_experiments.py)) and shell wrapper ([`scripts/analyze_experiments.sh`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/scripts/analyze_experiments.sh))
  - Metric computation: frame drop rate, mean, P95, P99, max latency spikes, and background job slowdown ratios
  - Automated plot generation:
    - [`results/plots/baseline_vs_protected_frame_drops.png`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/results/plots/baseline_vs_protected_frame_drops.png): Frame-drop rate and total miss count comparison
    - [`results/plots/baseline_vs_protected_p99_latency.png`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/results/plots/baseline_vs_protected_p99_latency.png): P99 latency and peak tail-latency spike suppression
  - Publication-ready summary tables exported to markdown ([`results/analysis_summary_table.md`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/results/analysis_summary_table.md)) and CSV ([`results/analysis_summary_table.csv`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/results/analysis_summary_table.csv))
  - Robust parser handling missing, corrupt, or malformed CSV rows with unit test validation ([`tests/test_data_analysis.py`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/tests/test_data_analysis.py))
- [x] **Phase 10: Hardening and Safety**
  - **AddressSanitizer (ASan) & UBSan**: Clean build with zero leaks or invalid memory accesses across all 40 tests
  - **ThreadSanitizer (TSan)**: Clean build verifying concurrent state is completely race-safe
  - **PID Reuse Protection**: Tracks start time (jiffies since boot from `/proc/<pid>/stat`) to reject recycled PIDs
  - **Stale Cgroup Cleanup**: Startup routine evacuates stranded PIDs to root cgroup and removes leftover slices
  - **Crash-and-Restart Recovery**: Verified daemon recovers from sudden `kill -9` without hanging on stale locks
  - **Async-Signal-Safe Handling**: Signal handler stores to atomic integer; teardown and I/O occur in normal thread context
  - **Verification Script**: Automated audit script [`scripts/verify_hardening.sh`](file:///c:/Users/agrbr/OneDrive/Desktop/OS%20PROJ/scripts/verify_hardening.sh)
- [x] **Phase 11: Final Integration & Demonstration Workflow**
  - 10-step end-to-end demo script ([`scripts/demo.sh`](file:///home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/scripts/demo.sh))
  - Concise color-coded terminal output detailing every lifecycle phase
  - Verified in both `--real` (active cgroup v2) and `--dry-run` modes
- [x] **Phase 12: Production Readiness & Real Video Meeting Support**
  - **Hardware Webcam Discovery**: Dynamic wildcard `/dev/video*` matching supporting native laptop and USB cameras (`/dev/video0`, `/dev/video1`).
  - **PipeWire & WirePlumber Stream Detection**: Zero-dependency parser detecting client capture streams (`Stream/Input/Audio`, `Stream/Input/Video`) across Google Meet (Chrome, Firefox), Zoom, Teams, Discord, and Slack.
  - **Microphone & "Camera-Off" Protection**: Monitors ALSA capture nodes (`/dev/snd/pcm*c`) and PipeWire audio streams, ensuring meetings remain fully protected even when video is muted or screen-sharing.
  - **False-Positive Elimination**: Idle browsers browsing web pages remain in `NORMAL` unthrottled state; protection activates strictly upon verified active media I/O.
  - **Kernel Thread Protection**: Automatically protects `kthreadd`, `kworker`, and system daemons from background classification via PPID filtering and token-boundary pattern matching.
  - **Instantaneous CLI Dashboard (`protector --status`)**: Live colorized status reporting of daemon state, active meeting PIDs, throttled workloads, and hysteresis countdowns.
  - **Systemd Service & Automated Packaging**: Production systemd service unit ([`scripts/isp.service`](file:///home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/scripts/isp.service)), installer ([`scripts/install.sh`](file:///home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/scripts/install.sh)), and uninstaller ([`scripts/uninstall.sh`](file:///home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/scripts/uninstall.sh)).
- [x] **Phase 13: Dear ImGui Interactive Session Overlay HUD (`isp_overlay`)**
  - **Auto-Activation on Call Start**: Automatically detects active meetings and pops up an elegant floating translucent HUD.
  - **Collapsible Floating Pill**: Unobtrusive compact badge (`Call: 87% | BG: 2% | Sys: 11%`) with animated glowing pulsing indicator.
  - **Expanded Resource Allocation Dashboard**:
    - **Video Call Slice**: High-priority guaranteed bandwidth (e.g. 87.0% CPU shares, `cpu.weight=800`, `ioprio=BE-0`).
    - **Background Processes**: Throttled allocation (e.g. 2.2% CPU shares, `cpu.weight=20`, `ioprio=IDLE`, dynamic aging tracking).
    - **Critical / Normal Processes**: Core OS, kernel workers, display compositor, and audio daemon (e.g. 10.8% CPU shares, `cpu.weight=100`).
  - **Live Process Telemetry**: Lists active protected meeting applications (PIDs and process names) and clamped background tasks in real time.
  - **Draggable & Configurable**: Draggable across monitors, opacity slider, always-on-top toggle, and headless mock simulation mode (`isp_overlay --mock`).
- [x] **Phase 14: Game Engine, Game, and Heavy Background Throttling**
  - **Game Engine Throttling**: Automatically classifies and throttles game engines (Unity, Unreal Engine, Godot, CryEngine) and their child worker / shader compiler processes (`UnityHelper`, `ShaderCompileWorker`) during active calls.
  - **Game and Launcher Throttling**: Intercepts gaming platforms (Steam, Lutris, Wine, Proton, Gamescope) and their launched game binaries, clamping them to background priority.
  - **Heavy Task Throttling**: Throttles compilers/build systems (`clang`, `gcc`, `cargo`, `rustc`, `mvn`), 3D renderers (`blender`), archivers (`7z`, `zstd`), video transcoders (`handbrake`), and background compute/torrents (`qbittorrent`, `xmrig`).
  - **Configurable Controls**: Managed via `config/protector.conf` (`throttle_games_and_engines`, `throttle_heavy_background`, custom pattern lists) or CLI flags (`--throttle-games` / `--no-throttle-games`).
  - **HUD and CLI Telemetry**: Live policy and throttled task reporting in `protector --status` and `isp_overlay`.

---

## Interactive Dear ImGui Overlay HUD

Launch the floating overlay:

```bash
# Launch overlay HUD (connects to running protector daemon)
./build/isp_overlay &

# Or test in standalone simulation/mock mode
./build/isp_overlay --mock --expanded
```

### Key UI Features:
- **Collapsed Pill View**: Stays neatly in the corner of your screen showing `[ ISP Shield: CALL PROTECTED | 87% Call · 2% BG · 11% Sys | Details ▼ ]`.
- **Expanded Dashboard**: Click `Details ▼` to expand into full diagnostics:
  - Visual color-coded proportional resource distribution bar.
  - Interactive metric tiles for Video Call, Background, and Critical System processes.
  - Live PID and application inspection for protected calls and throttled background jobs.
  - Grace countdown timer during hysteresis camera reconnects.
  - Opacity adjustment and always-on-top controls.

---

## Synthetic Background Workload Generator (`bg_workload`)

A high-intensity, multi-threaded synthetic workload generator and benchmark tool specifically designed to create real system contention (CPU, Disk I/O, Memory) and observe dynamic throttling in real time.

```bash
# High-intensity CPU contention (matrix math, hashing, ALU/FPU saturation)
./build/bg_workload --mode cpu --threads 4 --duration 30

# High-intensity Disk Direct/Buffered I/O churn with fdatasync
./build/bg_workload --mode io --io-size 128 --duration 30

# Memory churn and cache thrashing (tests cgroup memory.high)
./build/bg_workload --mode memory --mem-size 256 --duration 30

# Mixed mode: concurrent CPU + I/O + Memory contention with CSV export
./build/bg_workload --mode mixed --duration 60 --output workload_metrics.csv
```

### Self-Monitoring Live Telemetry:
`bg_workload` continuously inspects its own `/proc/self/cgroup` and Linux I/O scheduling class:
```
[00:03] [UNTHROTTLED] Compute: 421.3 Mops/s | Cgroup: /user.slice/... | IOPrio: BEST_EFFORT
...
[00:08] [THROTTLED]   Compute:  21.3 Mops/s (-94.9%) | Cgroup: protector.slice/background | IOPrio: IDLE
```
Upon completion, it outputs a full statistical breakdown comparing unthrottled baseline throughput vs. throttled throughput, total slowdown ratio, and time spent in the throttled state.

---

## Quick Start & 10-Step Demo Workflow

To run the complete 10-step demonstration workflow:

```bash
# Run the demo script (auto-detects real cgroups if root, or dry-run simulation)
./scripts/demo.sh
```

### Demonstration Steps Shown:
1. **Protector Startup**: Daemon acquires `/tmp/isp_demo.pid`, sweeps stale cgroups, and initializes `protector.slice`.
2. **Background Workloads Starting**: High-contention background CPU worker is spawned.
3. **Fake Call Starting**: `fake_call` begins 30 FPS stream holding `/dev/video10`.
4. **Session Detection**: Procfs scan detects active video descriptor; state machine transitions to `ACTIVE`.
5. **Classification**: Tree walking classifies `fake_call` as `PROTECTED`, worker as `BACKGROUND`, shell as `NORMAL`.
6. **Cgroup Resource Shaping**: Background `cpu.weight` throttled to `20` + `IDLE` IO priority; `fake_call` boosted to `800`.
7. **Frame Statistics**: `fake_call` logs 150 frames with 0 deadline misses (< 40ms deadline).
8. **Session Termination**: Video descriptor closes; state machine enters `IN_HYSTERESIS`.
9. **Hysteresis Period**: 4-second grace countdown ensures protection remains active against intermittent reconnects.
10. **Restoration of Normal Resource Allocation**: Baseline weights (100) restored, processes returned to original cgroups, daemon shuts down with zero lingering state.

---

## Interactive Dear ImGui Overlay HUD (`isp_overlay`)

The interactive overlay HUD provides a hardware-accelerated, translucent floating dashboard for real-time QoS monitoring and live daemon configuration.

```bash
# Standalone execution (opens immediately on launch)
./build/isp_overlay

# Standalone interactive simulation & mock mode
./build/isp_overlay --mock

# Run daemon with auto-launched overlay HUD (default when $DISPLAY/$WAYLAND_DISPLAY is set)
./build/protector -c config/protector.conf --real
# or explicitly disable overlay if running headless:
./build/protector -c config/protector.conf --no-overlay
```

### Features & Live Configuration Controls:
- **Instant Launch Visibility**: Opens immediately on launch (`auto_hide_idle = false` by default) so users always have instant access to monitoring and controls.
- **Dynamic QoS & Workload Configuration Panel**:
  - **Throttling Toggles**: Checkboxes to toggle throttling of games (Steam, Wine, Lutris), game engines (Unity, Unreal, Godot), and heavy background workloads (compilers, archivers, torrents).
  - **Interactive Sliders**: Fine-tune Call CPU Shares (100–1000), Background CPU Shares (5–200), Grace Delay (1–30s), Starvation Floor (5–50), and Polling Interval (100–2000ms).
  - **Custom Pattern Filters**: Input fields for dynamically adding comma-separated game/engine and background task names.
  - **Zero-Latency Daemon Synchronization**: Clicking `[Apply Settings to Daemon]` writes to `/tmp/protector.cmd`, persists updates to `config/protector.conf`, and signals the running daemon with `SIGHUP` for instant zero-restart reconfiguration.
  - **One-Click Reset**: `[Reset Defaults]` restores recommended QoS values instantly.
- **Visual Status Pill & Expanded Telemetry Dashboard**:
  - Live resource allocation percentages (e.g. `87% Call | 2% BG | 11% Sys`).
  - Active video call processes list with allocated shares.
  - Throttled background processes list with clamped shares and `ioprio=IDLE`.
  - Floating translucent window with opacity slider and always-on-top toggle.

---

## Build and Test Suite

### 1. Standard Build

```bash
cmake -B build -S .
cmake --build build

# Run all 65 unit tests
./build/unit_tests
```

### 2. AddressSanitizer & UndefinedBehaviorSanitizer (ASan/UBSan)

```bash
cmake -B build_asan -S . -DENABLE_ASAN=ON
cmake --build build_asan
./build_asan/unit_tests
```

### 3. ThreadSanitizer (TSan)

```bash
cmake -B build_tsan -S . -DENABLE_TSAN=ON
cmake --build build_tsan

# Run with ASLR entropy adjustment (required on newer Linux / WSL2 kernels)
setarch x86_64 -R ./build_tsan/unit_tests
```

### 4. Run Hardening & Safety Audit

```bash
./scripts/verify_hardening.sh
```

---

## Running Experiments & Data Analysis

To reproduce the quantitative benchmark comparing **Mode A (Baseline)** vs **Mode B (Protected)**:

```bash
# Run full benchmark harness (fixed-size stress-ng, tar compression, dd I/O)
./scripts/run_experiments.sh --duration 60 --repetitions 3 --dry-run

# Run data analysis and generate comparison plots & tables
./scripts/analyze_experiments.sh
```

Results are generated in:
- `results/plots/baseline_vs_protected_frame_drops.png`
- `results/plots/baseline_vs_protected_p99_latency.png`
- `results/analysis_summary_table.md`
- `results/analysis_summary_table.csv`

---

## Troubleshooting Guide

### 1. cgroup v2 Unified Hierarchy & Permissions
- **Symptom**: `Failed to initialize cgroup hierarchy` or permission denied writing to `/sys/fs/cgroup`.
- **Cause**: Linux requires root privileges (or delegated cgroup permissions) to create sub-slices and write `cpu.weight` / `cgroup.subtree_control`.
- **Remedy**:
  - For full active enforcement: Run with `sudo ./build/protector --real`
  - For unprivileged environments: Use `--dry-run` (the safe default). In dry-run mode, the daemon executes all detection, classification, state machine transitions, and logs every cgroup write without modifying root system files.

### 2. Missing Webcam Device (`/dev/video10`)
- **Symptom**: `Failed to open webcam device /dev/video10`.
- **Cause**: No physical webcam is attached or `v4l2loopback` is not loaded in WSL2.
- **Remedy**:
  - Load the loopback kernel module: `sudo modprobe v4l2loopback video_nr=10 card_label="VirtualWebcam"`
  - Or use a dummy test file: `touch /tmp/dummy_video && chmod 0666 /tmp/dummy_video` and set `video_device = /tmp/dummy_video` in `config/protector.conf`. The demo script ([`scripts/demo.sh`](scripts/demo.sh)) and experiment runner ([`scripts/run_experiments.py`](scripts/run_experiments.py)) automate this fallback transparently.

### 3. ThreadSanitizer: `FATAL: unexpected memory mapping`
- **Symptom**: When running `./build_tsan/unit_tests`, TSan outputs `FATAL: ThreadSanitizer: unexpected memory mapping 0x5f...`.
- **Cause**: Recent Linux kernels and Ubuntu 24.04 glibc employ Address Space Layout Randomization (ASLR) with high entropy (32-bit/28-bit VMA randomization), placing PIE binaries outside TSan's fixed shadow memory table.
- **Remedy**:
  - Run the TSan binary with ASLR disabled: `setarch x86_64 -R ./build_tsan/unit_tests`
  - Or temporarily adjust the system entropy: `sudo sysctl vm.mmap_rnd_bits=28`

### 4. Stale Cgroups After Daemon Crash or SIGKILL
- **Symptom**: Cgroup hierarchy cannot be removed or error `Device or resource busy` on `rmdir`.
- **Cause**: The previous daemon was abruptly terminated (`kill -9`) while processes were inside `protector.slice/background` or `protector.slice/protected`. In Linux cgroups v2, directories cannot be deleted if PIDs remain inside them.
- **Remedy**:
  - The protector daemon implements automatic **Stale Cgroup Cleanup** (`cleanup_stale_hierarchy()`) upon startup. It reads all `/sys/fs/cgroup/protector.slice/*/cgroup.procs`, writes each stranded PID back to `/sys/fs/cgroup/cgroup.procs`, and wipes the slice.
  - Manual evacuation command if needed:
    ```bash
    for p in $(cat /sys/fs/cgroup/protector.slice/* /cgroup.procs 2>/dev/null); do
        echo "$p" | sudo tee /sys/fs/cgroup/cgroup.procs >/dev/null
    done
    sudo rmdir /sys/fs/cgroup/protector.slice/* 2>/dev/null || true
    sudo rmdir /sys/fs/cgroup/protector.slice 2>/dev/null || true
    ```

### 5. PID Reuse Detection
- **Symptom**: Concern that a dead process PID might be reassigned by the kernel to a new unrelated process, causing erroneous throttling.
- **Resolution**: Both `ProcTable` and `CgroupManager` record the process `start_time` (jiffies since system boot parsed from `/proc/<pid>/stat` field 22). When a PID is refreshed or restored, the current start time is compared against the recorded start time. If the start time differs, the entry is invalidated, preventing any misdirected cgroup operations.

### 6. Pidfile Lock Contention (`/tmp/protector.pid`)
- **Symptom**: `Another instance of protector daemon is already running (locked /tmp/protector.pid)`.
- **Cause**: An active daemon is already holding the `flock` lock, or an orphaned process is still running.
- **Remedy**:
  - Verify if an instance is running: `ps aux | grep protector`
  - Stop the running instance gracefully: `kill -15 $(cat /tmp/protector.pid)`
  - If the previous instance crashed, `flock` locks are automatically released by the Linux kernel upon process death. A new daemon will seamlessly acquire the file and overwrite the PID.
