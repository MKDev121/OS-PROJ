# Interactive Session Protector: Demonstration & Testing Guide

This guide provides instructions for compiling, running, and demonstrating the **Interactive Session Protector (ISP)**. It features two end-to-end demonstrations that showcase the daemon's ability to throttle heavy background workloads and maintain video call performance.

---

## 1. Quickstart & Compilation

### Step 1.1: Build All Binaries
Ensure you are in the project root directory and build the project using CMake:

```bash
cd /home/meet-kareliya/Meet/Academics/os/project/OS-PROJ
cmake -B build -S .
cmake --build build -j$(nproc)
```

### Step 1.2: Verify Build Artifacts
Verify that all core executables have been generated in `./build`:

| Binary | Description |
| :--- | :--- |
| `build/protector` | The main protector daemon engine with CLI controls. |
| `build/isp_overlay` | The Dear ImGui hardware-accelerated floating HUD overlay. |
| `build/fake_call` | The interactive session benchmark (opens camera & measures frame deadline drops). |
| `build/bg_workload` | Synthetic background workload generator (CPU, memory, I/O, and stress matrix modes). |
| `build/unit_tests` | Comprehensive test suite (75 unit, hardening, and regression tests). |

Run the unit tests to confirm the build:
```bash
./build/unit_tests
```
*(All 75 tests should pass).*

---

## 2. Test 1: Extreme Throttling Demonstration (Making Background Processes Visibly Lag)

### The Problem on Modern Multi-Core Hardware
On modern multi-core laptops and desktops (e.g., 8 to 24 CPU threads):
- Standard cgroup v2 `cpu.weight` is a **work-conserving proportional share**.
- An interactive video call only consumes 1–2 CPU cores.
- If background processes are only shaped with `cpu.weight`, the Linux CFS scheduler allows them to consume **100% capacity on the remaining idle cores**.
- As a result, background tasks (compilation, Unity, games) finish at almost normal speed, making throttling invisible to observers.

### The Solution: Hard CFS Bandwidth Quota (`--hard-throttle`)
By passing `--hard-throttle 5`, the daemon writes `"5000 100000"` to `/sys/fs/cgroup/protector.slice/background/cpu.max`. This enforces an absolute kernel ceiling: all threads of the background workload combined are restricted to **5% of a single CPU core**, causing background tasks to visibly freeze and crawl.

---

### Step-by-Step Execution for Test 1

#### Scenario A: Using the Synthetic Benchmark (`bg_workload`)

1. **Terminal 1 — Launch a heavy matrix & cache-thrashing workload**:
   ```bash
   ./build/bg_workload --matrix 500 --threads 4
   ```
   **Observation**: Terminal 1 prints live unthrottled throughput:
   ```text
   [00:01] [UNTHROTTLED] Compute:  185.4 Mops/s | Cgroup: /user.slice/... | IOPrio: NONE
   [00:02] [UNTHROTTLED] Compute:  192.1 Mops/s | Cgroup: /user.slice/... | IOPrio: NONE
   ```

2. **Terminal 2 — Launch the Protector Daemon with Hard Throttling**:
   ```bash
   sudo ./build/protector -c config/protector.conf --real --throttle-games --hard-throttle 5 --verbose
   ```
   *(Note: The Dear ImGui HUD overlay will automatically appear on your screen).*

3. **Terminal 3 — Trigger an interactive session (or simulate one with fake_call)**:
   ```bash
   ./build/fake_call -d /dev/video0 -t 30
   ```

4. **Observe the Immediate Throttling**:
   - In **Terminal 1**, the throughput will drop by **over 95%**:
     ```text
     [00:05] [THROTTLED] Compute:    2.1 Mops/s (-98.9%) | Cgroup: /protector.slice/background | IOPrio: IDLE
     ```
   - In **Terminal 2** (or by checking `sudo ./build/protector -s`), the daemon reports:
     ```text
     [-] Throttled Background Workloads (1):
       * PID 154333: bg_workload [Hard Cap: 5% of 1 core, ioprio=IDLE]
     ```
   - In the **ImGui Overlay HUD**, the background workload indicator turns red and is capped to 5%.

5. **Stop Terminal 3 (`Ctrl+C`)**:
   - After the 8-second hysteresis grace countdown, the daemon automatically evacuates `bg_workload` back to normal priority.
   - Terminal 1 instantly resumes full speed (~190 Mops/s).

---

#### Scenario B: Throttling Real Applications (Unity, Steam, Compilers)

1. Launch your Unity editor, heavy game (via Steam/Wine/Lutris), or a multi-threaded compile job (`make -j$(nproc)`).
2. Start the daemon with hard throttling enabled:
   ```bash
   sudo ./build/protector -c config/protector.conf --real --throttle-games --hard-throttle 5
   ```
3. Join any video call (Google Meet, Zoom, Discord, Microsoft Teams) or run `./build/fake_call`.
4. **Result**: The game/Unity FPS will drop to single digits or freeze completely until the call concludes.

---

#### Scenario C: Live Tuning via the Dear ImGui HUD Slider

1. Look at the floating **Interactive Session Protector** HUD overlay on your desktop.
2. Click **`[+] Details`** to expand the full dashboard.
3. Scroll to **Card 3: DYNAMIC QOS & WORKLOAD CONFIGURATION**.
4. Drag the **Hard CPU Cap** slider (e.g. from `5%` down to `1%` for near-lockup, or up to `25%` for moderate throttling).
5. Click **`[Apply Settings to Daemon]`**.
6. The daemon instantly applies the new `cpu.max` limit via zero-latency IPC (`/tmp/protector.cmd`) without restarting.

---

## 3. Test 2: Video Call Protection Demonstration (Rescuing a Degraded Video Call)

### Concept
This test demonstrates the core value proposition of the project:
1. First, an unthrottled background workload consumes system resources, causing an active video call to **lag, stutter, and drop frames**.
2. Then, the protector daemon is activated, which **instantly rescues the video call**, restoring it to a smooth 30.0 FPS with 0% frame drops.

---

### Step-by-Step Execution for Test 2

#### Phase 2.1: Observe the Lag WITHOUT the Protector Daemon

1. **Terminal 1 — Launch the ultra-heavy stress workload**:
   ```bash
   ./build/bg_workload --stress
   # Or explicitly:
   ./build/bg_workload --lag-call
   ```
   **What this does to lag video calls**:
   - Spawns **64 worker threads** (4x oversubscription on 16 logical cores), congesting the Linux CFS runqueues so scheduler wake-up latency for browser video decode and compositor threads jumps past the 33.3ms frame deadline.
   - Streams **25+ GB/s of non-temporal AVX2 stores** (`_mm256_stream_si256`) bypassing CPU cache directly to DDR5 RAM, starving browser frame rendering and GPU texture uploads of memory bus bandwidth.
   - Concurrently executes **synchronous NVMe I/O flushes** (`fdatasync` in `/var/tmp`) to trigger block-layer contention and kernel writeback stalls.

2. **Terminal 2 — Start the video call benchmark (WITHOUT the daemon running)**:
   ```bash
   ./build/fake_call -d /dev/video0 -t 30 --heavy
   ```
   *(Note: The `--heavy` flag simulates realistic video frame compression work of ~22ms per frame).*

3. **Observe the Severe Degradation in Terminal 2**:
   Because the background stress workload is consuming all CPU cores and memory bus bandwidth:
   ```text
   [00:01] Frame #0030 | Interval: 58.7ms | Drops: 6 (20.0%) [LAG / STUTTERING]
   [00:02] Frame #0060 | Interval: 72.4ms | Drops: 18 (30.0%) [LAG / STUTTERING]
   [00:03] Frame #0090 | Interval: 66.1ms | Drops: 31 (34.4%) [LAG / STUTTERING]
   ```
   Notice:
   - High inter-frame intervals (spiking up to **100ms–300ms+**).
   - Significant deadline misses (**30% to 80%+ frame drop rate**).
   - Status marked in bright red: **`[LAG / STUTTERING]`**.

4. Stop Terminal 2 (`Ctrl+C`).

---

#### Phase 2.2: Activate Protector Daemon & Demonstrate the Rescue

1. **Leave Terminal 1 running** (`bg_workload --stress` still running).
2. **Terminal 3 — Start the Protector Daemon**:
   ```bash
   sudo ./build/protector -c config/protector.conf --real --throttle-games --hard-throttle 5
   ```
3. **Terminal 2 — Re-run the video call**:
   ```bash
   ./build/fake_call -d /dev/video0 -t 30 --heavy
   ```
4. **Observe the Immediate Recovery in Terminal 2**:
   ```text
   [00:01] Frame #0030 | Interval: 33.3ms | Drops: 0 (0.0%) [SMOOTH / PROTECTED]
   [00:02] Frame #0060 | Interval: 33.4ms | Drops: 0 (0.0%) [SMOOTH / PROTECTED]
   [00:03] Frame #0090 | Interval: 33.3ms | Drops: 0 (0.0%) [SMOOTH / PROTECTED]
   ```
   **What occurred behind the scenes**:
   - The daemon detected `fake_call` opening `/dev/video0`.
   - `fake_call` was moved into `protector.slice/protected` with **800 CPU shares**.
   - `bg_workload` was detected, moved to `protector.slice/background`, choked to **5% of 1 core**, and downgraded to **IOPRIO_CLASS_IDLE**.
   - Frame drops dropped to **0.0%**, frame intervals stabilized at **33.3ms**, and status shifted to green **`[SMOOTH / PROTECTED]`**.

---

#### Phase 2.3: Verification with Real Video Calls (Google Meet / Zoom / Discord)

You can run this exact test with your real webcam and web browser:
1. Join an active video call in **Firefox** or **Chrome** (Google Meet, Discord, Zoom, Microsoft Teams).
2. Start the workload in a terminal:
   ```bash
   ./build/bg_workload --stress
   # Or:
   ./build/bg_workload --lag-call
   ```
   - *Without the daemon*: Notice immediate video frame stutter, dropped frames, delayed/jittery audio, and video resolution downscaling.
3. In another terminal, activate the protector daemon:
   ```bash
   sudo ./build/protector -c config/protector.conf --real --throttle-games --hard-throttle 5
   ```
4. *With the daemon*: The daemon automatically detects the browser media session and places `bg_workload` into `/sys/fs/cgroup/protector.slice/background` with a 5% CPU cap and idle I/O priority. The video call immediately clears up and returns to smooth 30 FPS playback.

---

## 4. Verification & Status Inspection Commands

While tests are active, open an extra terminal to inspect the underlying kernel state:

### 1. Check Live Daemon Status
```bash
./build/protector -s
```
Sample output:
```text
State:                   ACTIVE (Interactive call protected)
Protected CPU Weight:    800 shares
Background CPU Weight:   1 shares
Hard CPU Quota:          CAPPED TO 5% OF 1 CORE (cpu.max = 5000 100000)
Throttled Background Workloads:
  * PID 154894: bg_workload [Hard Cap: 5% of 1 core, ioprio=IDLE]
```

### 2. Verify Raw Kernel Cgroup v2 Writes
```bash
# Check hard CFS bandwidth limit
cat /sys/fs/cgroup/protector.slice/background/cpu.max
# Expected output: 5000 100000

# Check proportional CPU weight
cat /sys/fs/cgroup/protector.slice/background/cpu.weight
# Expected output: 1 (or 20)

# Check PIDs trapped in the background throttled group
cat /sys/fs/cgroup/protector.slice/background/cgroup.procs
```

### 3. Graceful Daemon Teardown
Press `Ctrl+C` in the daemon terminal, or run:
```bash
sudo kill -15 $(cat /tmp/protector.pid)
```
The daemon safely evacuates all processes back to their original scopes, resets `cpu.max`, and removes `protector.slice` cleanly without leaving orphaned state.
