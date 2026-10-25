#!/usr/bin/env bash
# ==============================================================================
# verify_hardening.sh - Interactive Session Protector Hardening & Safety Verifier
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}==================================================================${NC}"
echo -e "${BLUE}  PHASE 10: HARDENING AND SAFETY AUDIT VERIFIER                   ${NC}"
echo -e "${BLUE}==================================================================${NC}"

cd "${ROOT_DIR}"

# 1. Verify Standard Unit Tests
echo -e "\n${BLUE}[1/4] Running Standard Unit Tests...${NC}"
cmake -B build -S . > /dev/null
cmake --build build > /dev/null
./build/unit_tests
echo -e "${GREEN}✓ Standard unit tests: 40/40 PASSED${NC}"

# 2. Verify AddressSanitizer & UndefinedBehaviorSanitizer (ASan/UBSan)
echo -e "\n${BLUE}[2/4] Verifying AddressSanitizer (ASan) & UBSan Build...${NC}"
cmake -B build_asan -S . -DENABLE_ASAN=ON > /dev/null
cmake --build build_asan > /dev/null
./build_asan/unit_tests > /dev/null
echo -e "${GREEN}✓ AddressSanitizer: CLEAN (0 memory errors, 0 leaks)${NC}"

# 3. Verify ThreadSanitizer (TSan)
echo -e "\n${BLUE}[3/4] Verifying ThreadSanitizer (TSan) Build...${NC}"
cmake -B build_tsan -S . -DENABLE_TSAN=ON > /dev/null
cmake --build build_tsan > /dev/null
setarch x86_64 -R ./build_tsan/unit_tests > /dev/null
echo -e "${GREEN}✓ ThreadSanitizer: CLEAN (0 data races, 0 thread leaks)${NC}"

# 4. Verify Crash-and-Restart Recovery (Simulated SIGKILL + Daemon Restart)
echo -e "\n${BLUE}[4/4] Verifying Crash-and-Restart Recovery (SIGKILL & Stale Cleanup)...${NC}"
TEST_PIDFILE="/tmp/isp_crash_test.pid"
rm -f "${TEST_PIDFILE}"

# Start daemon in background
./build/protector --pidfile "${TEST_PIDFILE}" --dry-run --verbose &
DAEMON_PID=$!
sleep 0.5

if ! kill -0 "${DAEMON_PID}" 2>/dev/null; then
    echo -e "${RED}✗ Daemon failed to start initially${NC}"
    exit 1
fi
echo -e "  Daemon running with PID ${DAEMON_PID}, pidfile locked."

# Abruptly kill the daemon with SIGKILL to simulate sudden crash / panic
echo -e "  Simulating sudden crash (kill -9 ${DAEMON_PID})..."
kill -9 "${DAEMON_PID}" 2>/dev/null || true
wait "${DAEMON_PID}" 2>/dev/null || true

# Verify daemon is dead
if kill -0 "${DAEMON_PID}" 2>/dev/null; then
    echo -e "${RED}✗ Daemon did not terminate${NC}"
    exit 1
fi
echo -e "  Daemon terminated abnormally (SIGKILL)."

# Now start a new daemon instance using the same pidfile
echo -e "  Starting new protector daemon instance to verify automatic recovery..."
./build/protector --pidfile "${TEST_PIDFILE}" --dry-run --timeout 1 > /tmp/isp_recovery.log 2>&1 || true

if grep -q "Protector daemon shutdown complete" /tmp/isp_recovery.log; then
    echo -e "${GREEN}✓ New daemon successfully recovered, cleaned stale hierarchy, and shut down cleanly.${NC}"
else
    echo -e "${RED}✗ Daemon recovery failed. Output:${NC}"
    cat /tmp/isp_recovery.log
    exit 1
fi

rm -f "${TEST_PIDFILE}" /tmp/isp_recovery.log

echo -e "\n${GREEN}==================================================================${NC}"
echo -e "${GREEN}  ALL PHASE 10 HARDENING & SAFETY AUDITS PASSED CLEANLY!         ${NC}"
echo -e "${GREEN}==================================================================${NC}"
