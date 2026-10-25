#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Environment Verification Script
# Phase 0: Verifies system prerequisites, cgroups v2, /dev/video10, and toolchain
# ==============================================================================

set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

TOTAL_CHECKS=0
PASSED_CHECKS=0
FAILED_CHECKS=0
WARNINGS=0

print_header() {
    echo -e "\n${BOLD}${BLUE}=== $1 ===${NC}"
}

pass() {
    echo -e "  ${GREEN}[PASS]${NC} $1"
    PASSED_CHECKS=$((PASSED_CHECKS + 1))
    TOTAL_CHECKS=$((TOTAL_CHECKS + 1))
}

fail() {
    echo -e "  ${RED}[FAIL]${NC} $1"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
    TOTAL_CHECKS=$((TOTAL_CHECKS + 1))
}

warn() {
    echo -e "  ${YELLOW}[WARN]${NC} $1"
    WARNINGS=$((WARNINGS + 1))
}

echo -e "${BOLD}=================================================================="
echo -e "  Interactive Session Protector — Environment Verification"
echo -e "==================================================================${NC}"
echo "Date: $(date)"
echo "Kernel: $(uname -r 2>/dev/null || echo 'unknown')"
echo "Host: $(uname -n 2>/dev/null || echo 'unknown')"

# ------------------------------------------------------------------------------
# 1. cgroup v2 Check
# ------------------------------------------------------------------------------
print_header "1. Verifying cgroups v2 Mount and Controllers"

CGROUP_PATH="/sys/fs/cgroup"
if [[ -d "$CGROUP_PATH" ]]; then
    # Check filesystem type
    FS_TYPE=$(stat -f -c %T "$CGROUP_PATH" 2>/dev/null || true)
    if [[ "$FS_TYPE" == "cgroup2fs" ]]; then
        pass "cgroups v2 unified hierarchy detected at $CGROUP_PATH (type: $FS_TYPE)"
    elif grep -qs "cgroup2 $CGROUP_PATH" /proc/mounts 2>/dev/null; then
        pass "cgroup2 mount point confirmed in /proc/mounts"
    else
        fail "$CGROUP_PATH exists but does not appear to be cgroup v2 (stat: '$FS_TYPE')"
    fi

    # Check available controllers
    CONTROLLERS_FILE="$CGROUP_PATH/cgroup.controllers"
    if [[ -r "$CONTROLLERS_FILE" ]]; then
        CONTROLLERS=$(cat "$CONTROLLERS_FILE")
        pass "Available cgroup v2 controllers: $CONTROLLERS"
        
        # Verify critical controllers: cpu, io, memory
        for ctrl in cpu io memory; do
            if echo "$CONTROLLERS" | grep -qw "$ctrl"; then
                pass "Critical controller '$ctrl' is present"
            else
                fail "Critical controller '$ctrl' is missing from $CONTROLLERS_FILE"
            fi
        done
    else
        fail "Cannot read $CONTROLLERS_FILE (permission issue or not cgroup v2)"
    fi

    # Check subtree control permissions
    if [[ -w "$CGROUP_PATH/cgroup.subtree_control" ]]; then
        pass "Root cgroup.subtree_control is writable (running as root or delegated)"
    else
        warn "Root cgroup.subtree_control is not directly writable. The daemon will require sudo/root or user cgroup delegation."
    fi
else
    fail "cgroups mount path $CGROUP_PATH not found"
fi

# ------------------------------------------------------------------------------
# 2. /dev/video10 Webcam Device Check
# ------------------------------------------------------------------------------
print_header "2. Verifying Webcam Test Device (/dev/video10)"

VIDEO_DEV="/dev/video10"
if [[ -e "$VIDEO_DEV" ]]; then
    if [[ -c "$VIDEO_DEV" ]]; then
        pass "$VIDEO_DEV exists and is a valid character device"
        
        # Check permissions
        if [[ -r "$VIDEO_DEV" && -w "$VIDEO_DEV" ]]; then
            pass "$VIDEO_DEV has read and write permissions for current user"
        else
            warn "$VIDEO_DEV is not read/write accessible by $(whoami). (Try adding user to 'video' group or run with sudo)"
        fi
        
        # Check if v4l2-ctl is available to query details
        if command -v v4l2-ctl >/dev/null 2>&1; then
            CARD_NAME=$(v4l2-ctl --device="$VIDEO_DEV" --info 2>/dev/null | grep -i "Card type" | awk -F: '{print $2}' | xargs || true)
            if [[ -n "$CARD_NAME" ]]; then
                pass "V4L2 Device identified: $CARD_NAME"
            fi
        fi
    else
        fail "$VIDEO_DEV exists but is NOT a character device"
    fi
else
    fail "$VIDEO_DEV does not exist"
    echo -e "       ${YELLOW}-> Solution:${NC} Install v4l2loopback and load the kernel module:"
    echo -e "          sudo apt-get install -y v4l2loopback-dkms"
    echo -e "          sudo modprobe v4l2loopback video_nr=10 card_label=\"InteractiveSessionTest\" exclusive_caps=1"
fi

# ------------------------------------------------------------------------------
# 3. Compiler & CMake Check (C++17, g++, cmake)
# ------------------------------------------------------------------------------
print_header "3. Verifying Compiler (g++ C++17) and CMake"

if command -v g++ >/dev/null 2>&1; then
    GXX_VER=$(g++ --version | head -n1)
    pass "g++ compiler found: $GXX_VER"

    # Test C++17 compilation and execution
    TMP_SRC=$(mktemp /tmp/test_cpp17_XXXXXX.cpp 2>/dev/null || mktemp test_cpp17_XXXXXX.cpp)
    TMP_BIN="${TMP_SRC%.cpp}.out"

    cat << 'EOF' > "$TMP_SRC"
#include <iostream>
#include <string_view>
#include <optional>
#include <filesystem>
#include <variant>

int main() {
    std::string_view message = "C++17 compilation & execution successful";
    std::optional<int> sample_val = 100;
    std::variant<int, std::string> var = "variant_ok";
    if (sample_val.has_value() && std::holds_alternative<std::string>(var)) {
        std::cout << message << std::endl;
        return 0;
    }
    return 1;
}
EOF

    if g++ -std=c++17 -Wall -Wextra "$TMP_SRC" -o "$TMP_BIN" 2>&1; then
        pass "g++ compiles C++17 with -Wall -Wextra successfully"
        if "$TMP_BIN" >/dev/null 2>&1; then
            pass "Compiled C++17 test binary executes cleanly"
        else
            fail "C++17 test binary exited with non-zero status"
        fi
    else
        fail "Failed to compile C++17 test file with g++ -std=c++17 -Wall -Wextra"
    fi
    rm -f "$TMP_SRC" "$TMP_BIN"
else
    fail "g++ compiler not found in PATH. (Install with: sudo apt install g++)"
fi

if command -v cmake >/dev/null 2>&1; then
    CMAKE_VER=$(cmake --version | head -n1)
    pass "CMake found: $CMAKE_VER"
else
    fail "cmake not found in PATH. (Install with: sudo apt install cmake)"
fi

# ------------------------------------------------------------------------------
# 4. Required Commands & Workload Tools Check
# ------------------------------------------------------------------------------
print_header "4. Verifying Required Commands and Workload Tools"

REQUIRED_COMMANDS=(
    "stress-ng:Stress testing workload tool for CPU/memory/IO"
    "tar:Archiving workload tool for background compression load"
    "dd:Block device / file direct I/O workload tool"
    "python3:Python runtime for analysis and plotting"
    "modprobe:Kernel module utility (for v4l2loopback)"
    "make:Build automation tool for CMake build tree"
)

for item in "${REQUIRED_COMMANDS[@]}"; do
    cmd="${item%%:*}"
    desc="${item#*:}"
    if command -v "$cmd" >/dev/null 2>&1; then
        cmd_path=$(command -v "$cmd")
        pass "Found '$cmd' ($cmd_path) — $desc"
    else
        fail "Missing required tool: '$cmd' — $desc"
    fi
done

# Check Python environment & scientific libraries
if command -v python3 >/dev/null 2>&1; then
    PY_VER=$(python3 --version 2>&1)
    pass "Python version: $PY_VER"

    if python3 -c "import pandas, matplotlib; print('Data packages available')" >/dev/null 2>&1; then
        pass "Python analysis libraries (pandas, matplotlib) are installed"
    else
        warn "Python libraries 'pandas' and/or 'matplotlib' are not yet installed."
        echo -e "       ${YELLOW}-> Note:${NC} Install with: sudo apt install -y python3-pandas python3-matplotlib"
    fi
fi

# ------------------------------------------------------------------------------
# Summary and Exit Status
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}=================================================================="
echo -e "  Verification Summary"
echo -e "==================================================================${NC}"
echo -e "Total Checks : $TOTAL_CHECKS"
echo -e "Passed       : ${GREEN}$PASSED_CHECKS${NC}"
echo -e "Failed       : ${RED}$FAILED_CHECKS${NC}"
echo -e "Warnings     : ${YELLOW}$WARNINGS${NC}"

if [[ $FAILED_CHECKS -eq 0 ]]; then
    echo -e "\n${GREEN}${BOLD}[SUCCESS] Environment satisfies all Phase 0 requirements!${NC}\n"
    exit 0
else
    echo -e "\n${RED}${BOLD}[FAILURE] Environment verification failed ($FAILED_CHECKS check(s) failed).${NC}"
    echo -e "Please review the failure messages above and consult docs/ENVIRONMENT_SETUP.md.\n"
    exit 1
fi
