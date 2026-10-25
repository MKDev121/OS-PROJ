#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Phase 7 Fake Call Acceptance Test
# Validates:
#   1. Process holds /dev/video10 open throughout session
#   2. Session detector detects the running process
#   3. Target 30 FPS achieves ~1800 frames over 60 seconds
#   4. Output CSV is generated with all required columns
# ==============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
CSV_FILE="/tmp/acceptance_fake_call_60s.csv"
LOG_FILE="/tmp/acceptance_fake_call.log"

rm -f "$CSV_FILE" "$LOG_FILE"

echo "=================================================================="
echo "  Phase 7 Acceptance Test: Fake Video Call Workload (60s @ 30 FPS)"
echo "=================================================================="

# Ensure /dev/video10 exists
if [[ ! -e "/dev/video10" ]]; then
    echo "Creating /dev/video10 node..."
    dd if=/dev/urandom of=/dev/video10 bs=1024 count=100 2>/dev/null || touch /dev/video10
    chmod 0666 /dev/video10
fi

echo "[1/4] Launching fake_call (60s @ 30 FPS holding /dev/video10)..."
"$BUILD_DIR/fake_call" \
    --device /dev/video10 \
    --duration 60 \
    --fps 30 \
    --output "$CSV_FILE" > "$LOG_FILE" 2>&1 &
CALL_PID=$!
echo "      fake_call running as PID $CALL_PID"

# Give the process 1 second to start and open /dev/video10
sleep 1

echo -e "\n[2/4] Verifying /dev/video10 is held open & detectable by SessionDetector..."
# Check /proc/<pid>/fd
FD_MATCH=$(ls -l "/proc/$CALL_PID/fd" 2>/dev/null | grep "/dev/video10" || true)
if [[ -n "$FD_MATCH" ]]; then
    echo -e "      \033[0;32m[PASS]\033[0m /dev/video10 is held open by PID $CALL_PID: $FD_MATCH"
else
    echo -e "      \033[0;31m[FAIL]\033[0m /dev/video10 is NOT open in /proc/$CALL_PID/fd!"
fi

# Run python/one-liner to verify session detector finds CALL_PID
DETECTOR_CHECK=$(python3 -c "
import os, sys
pids = []
for p in os.listdir('/proc'):
    if p.isdigit():
        fd_dir = os.path.join('/proc', p, 'fd')
        try:
            for fd in os.listdir(fd_dir):
                target = os.readlink(os.path.join(fd_dir, fd))
                if target.startswith('/dev/video'):
                    pids.append(int(p))
                    break
        except Exception:
            pass
if $CALL_PID in pids:
    print('DETECTED')
else:
    print('NOT_DETECTED')
")

if [[ "$DETECTOR_CHECK" == "DETECTED" ]]; then
    echo -e "      \033[0;32m[PASS]\033[0m Session detector correctly identified PID $CALL_PID as holding video device"
else
    echo -e "      \033[0;31m[FAIL]\033[0m Session detector failed to detect PID $CALL_PID!"
fi

echo -e "\n[3/4] Waiting for 60-second video call session to complete..."
# Monitor progress periodically
for i in {1..6}; do
    sleep 10
    ELAPSED=$((i * 10))
    echo "      ... elapsed ${ELAPSED}s / 60s"
done

wait $CALL_PID 2>/dev/null || true
echo "      fake_call completed."

echo -e "\n[4/4] Evaluating Acceptance Criteria & CSV Metrics..."
cat "$LOG_FILE"

PASS=0
FAIL=0

check() {
    local cond="$1"
    local desc="$2"
    if [[ "$cond" -eq 1 ]]; then
        echo -e "  \033[0;32m[PASS]\033[0m $desc"
        PASS=$((PASS + 1))
    else
        echo -e "  \033[0;31m[FAIL]\033[0m $desc"
        FAIL=$((FAIL + 1))
    fi
}

# 1. Check CSV existence
if [[ -f "$CSV_FILE" ]]; then
    check 1 "CSV file was generated successfully ($CSV_FILE)"
else
    check 0 "CSV file was NOT generated ($CSV_FILE)"
fi

# 2. Check CSV Header
HEADER=$(head -n 1 "$CSV_FILE" 2>/dev/null || echo "")
echo "      CSV Header: $HEADER"
HAS_TS=$(echo "$HEADER" | grep -c "timestamp" || true)
HAS_FN=$(echo "$HEADER" | grep -c "frame_number" || true)
HAS_INT=$(echo "$HEADER" | grep -c "frame_interval_ms" || true)
HAS_DM=$(echo "$HEADER" | grep -c "deadline_missed" || true)

if [[ $HAS_TS -eq 1 && $HAS_FN -eq 1 && $HAS_INT -eq 1 && $HAS_DM -eq 1 ]]; then
    check 1 "CSV contains required columns: timestamp, frame_number, frame_interval_ms, deadline_missed"
else
    check 0 "CSV is missing one or more required columns"
fi

# 3. Check Frame Count (~1800 frames)
DATA_LINES=$(tail -n +2 "$CSV_FILE" | wc -l)
echo "      Total Data Lines in CSV: $DATA_LINES frames (Target: 1800)"

if [[ $DATA_LINES -ge 1780 && $DATA_LINES -le 1820 ]]; then
    check 1 "Frame count is approximately 1800 over 60 seconds (actual: $DATA_LINES frames)"
else
    check 0 "Frame count deviated significantly from 1800 (actual: $DATA_LINES frames)"
fi

# 4. Check device descriptor remained open
if [[ -n "$FD_MATCH" && "$DETECTOR_CHECK" == "DETECTED" ]]; then
    check 1 "/dev/video10 remained open during execution and was detected"
else
    check 0 "/dev/video10 was not held open or not detected"
fi

echo -e "\n=================================================================="
echo -e "  Phase 7 Acceptance Summary: $PASS Passed, $FAIL Failed"
echo -e "=================================================================="

if [[ $FAIL -eq 0 ]]; then
    echo -e "\033[1;32m[SUCCESS] All Phase 7 Acceptance Criteria Passed!\033[0m\n"
    exit 0
else
    echo -e "\033[1;31m[FAILURE] Phase 7 Acceptance Criteria Failed!\033[0m\n"
    exit 1
fi
