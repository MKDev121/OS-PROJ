#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Daemon Integration Test
# Validates session start/stop cycle with concurrent fake_call and stress-ng
# ==============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
LOG_FILE="/tmp/protector_integration.log"
PIDFILE="/tmp/protector_test_runner.pid"

rm -f "$LOG_FILE" "$PIDFILE"

echo "=== Starting Daemon Integration Test ==="

# 1. Start background workload (stress-ng)
echo "[1/5] Launching background stress-ng workload..."
stress-ng --cpu 1 --timeout 30s >/dev/null 2>&1 &
STRESS_PID=$!
echo "      stress-ng running as PID $STRESS_PID"

# 2. Start protector daemon in dry-run mode for 18 seconds
echo "[2/5] Launching protector daemon..."
"$BUILD_DIR/protector" \
    --dry-run \
    --pidfile "$PIDFILE" \
    --timeout 18 > "$LOG_FILE" 2>&1 &
DAEMON_PID=$!
echo "      protector daemon running as PID $DAEMON_PID"

sleep 2

# 3. Launch fake video call app holding /dev/video10
echo "[3/5] Launching fake_call app holding /dev/video10 for 5 seconds..."
"$BUILD_DIR/fake_call" --device /dev/video10 --duration 5 --fps 30 >/dev/null 2>&1 &
CALL_PID=$!
echo "      fake_call running as PID $CALL_PID"

# Wait for session detection and resource shaping
sleep 3
echo "[4/5] Inspecting active protection state..."
if grep -q "Entered ACTIVE session" "$LOG_FILE"; then
    echo "      [PASS] Daemon successfully detected session and entered ACTIVE state!"
else
    echo "      [FAIL] Daemon did not enter ACTIVE state!"
fi

if grep -q "Throttled background CPU weight" "$LOG_FILE"; then
    echo "      [PASS] Background workloads throttled!"
else
    echo "      [FAIL] Background workloads were not throttled!"
fi

# Wait for fake call to finish and hysteresis to expire
echo "[5/5] Waiting for fake_call termination, hysteresis grace period, and restoration..."
wait $CALL_PID 2>/dev/null || true
echo "      fake_call terminated. Waiting for hysteresis (4s)..."
wait $DAEMON_PID 2>/dev/null || true

# Wait for stress-ng to wrap up
wait $STRESS_PID 2>/dev/null || true

echo -e "\n=== Integration Log Summary ==="
cat "$LOG_FILE"

echo -e "\n=== Verifying Assertions ==="
PASS=0
FAIL=0

check_log() {
    local pattern="$1"
    local desc="$2"
    if grep -q "$pattern" "$LOG_FILE"; then
        echo -e "  \033[0;32m[PASS]\033[0m $desc"
        PASS=$((PASS + 1))
    else
        echo -e "  \033[0;31m[FAIL]\033[0m $desc"
        FAIL=$((FAIL + 1))
    fi
}

check_log "Entered ACTIVE session" "1. Transitioned to ACTIVE state on session start"
check_log "Throttled background CPU weight to 20" "2. Throttled background CPU weight"
check_log "boosted protected CPU weight to 800" "3. Boosted protected CPU weight"
check_log "Entered IN_HYSTERESIS grace period" "4. Entered IN_HYSTERESIS on session exit"
check_log "Transitioned to INACTIVE. Restored baseline weights" "5. Restored normal weights upon hysteresis expiration"
check_log "Protector daemon shutdown complete" "6. Clean daemon shutdown and cgroup cleanup"

echo -e "\nTotal Checks: $((PASS + FAIL)) | Passed: $PASS | Failed: $FAIL"

if [[ $FAIL -eq 0 ]]; then
    echo -e "\033[1;32m[SUCCESS] Phase 6 Integration Test Passed Perfectly!\033[0m\n"
    exit 0
else
    echo -e "\033[1;31m[FAILURE] Phase 6 Integration Test Failed!\033[0m\n"
    exit 1
fi
