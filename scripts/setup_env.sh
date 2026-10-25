#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Environment Setup Script
# Automates installation of toolchain, v4l2loopback, cgroups v2, and stress tools
# ==============================================================================

set -euo pipefail

BOLD='\033[1m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BOLD}${BLUE}=================================================================="
echo -e "  Interactive Session Protector — Environment Setup"
echo -e "==================================================================${NC}"

# Check sudo / root
if [[ $EUID -ne 0 ]]; then
   echo -e "${YELLOW}[!] This script requires root privileges to install packages and load kernel modules.${NC}"
   echo -e "    Re-running with sudo..."
   exec sudo "$0" "$@"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo -e "\n${BOLD}[1/4] Updating package repositories...${NC}"
apt-get update -y

echo -e "\n${BOLD}[2/4] Installing dependencies...${NC}"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++ \
    make \
    stress-ng \
    v4l2loopback-dkms \
    v4l2loopback-utils \
    v4l-utils \
    linux-headers-$(uname -r 2>/dev/null || echo "generic") \
    python3 \
    python3-pip \
    python3-pandas \
    python3-matplotlib \
    ca-certificates \
    curl \
    tar \
    coreutils

echo -e "\n${BOLD}[3/4] Configuring v4l2loopback device /dev/video10...${NC}"

# Unload if already loaded to ensure our video_nr=10 is applied
if lsmod | grep -q "^v4l2loopback"; then
    echo "  Unloading existing v4l2loopback module..."
    modprobe -r v4l2loopback || true
fi

# Load module with device number 10
echo "  Loading v4l2loopback module with video_nr=10..."
modprobe v4l2loopback video_nr=10 card_label="InteractiveSessionTest" exclusive_caps=1

# Persist module config for reboots
echo "  Persisting v4l2loopback configuration..."
echo "v4l2loopback" > /etc/modules-load.d/v4l2loopback.conf
echo 'options v4l2loopback video_nr=10 card_label="InteractiveSessionTest" exclusive_caps=1' > /etc/modprobe.d/v4l2loopback.conf

# Give read/write permissions to video device
if [[ -c "/dev/video10" ]]; then
    chmod 0666 /dev/video10
    echo -e "  ${GREEN}[OK] /dev/video10 created with permissions: $(ls -l /dev/video10)${NC}"
else
    echo -e "  ${YELLOW}[WARN] /dev/video10 device node not immediately visible. Checking udev...${NC}"
    udevadm settle || true
fi

# Ensure cgroups v2 subtree control enables cpu, io, memory in root if writable
CGROUP_ROOT="/sys/fs/cgroup"
if [[ -w "$CGROUP_ROOT/cgroup.subtree_control" ]]; then
    echo -e "\n${BOLD}[4/4] Enabling cgroup v2 controllers in root cgroup...${NC}"
    for ctrl in cpu io memory pids; do
        if grep -qw "$ctrl" "$CGROUP_ROOT/cgroup.controllers"; then
            echo "+$ctrl" > "$CGROUP_ROOT/cgroup.subtree_control" 2>/dev/null || true
        fi
    done
    echo "  Active root subtree control: $(cat $CGROUP_ROOT/cgroup.subtree_control 2>/dev/null || echo 'N/A')"
else
    echo -e "\n${BOLD}[4/4] Skipping root subtree_control setup (not writable or already configured)${NC}"
fi

echo -e "\n${BOLD}${GREEN}=================================================================="
echo -e "  Setup Complete! Running Verification..."
echo -e "==================================================================${NC}\n"

bash "$SCRIPT_DIR/verify_env.sh"
