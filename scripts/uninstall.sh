#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector - Uninstaller
# ==============================================================================
set -euo pipefail

if [ "${EUID}" -ne 0 ]; then
    echo "Error: This uninstaller must be run as root (or via sudo)."
    exit 1
fi

echo "=== Uninstalling Interactive Session Protector ==="

# Stop and disable systemd service if running
if systemctl is-active --quiet isp.service 2>/dev/null; then
    echo "Stopping isp.service..."
    systemctl stop isp.service
fi

if systemctl is-enabled --quiet isp.service 2>/dev/null; then
    echo "Disabling isp.service..."
    systemctl disable isp.service
fi

# Remove systemd service file
if [ -f /etc/systemd/system/isp.service ]; then
    rm -f /etc/systemd/system/isp.service
    systemctl daemon-reload
fi

# Remove binaries
rm -f /usr/local/bin/protector
rm -f /usr/local/bin/isp_overlay

echo "Removed binaries and service."
echo "Note: Configuration directory /etc/isp was preserved."
echo "To completely purge configurations, run: sudo rm -rf /etc/isp"
echo "=== Uninstallation complete ==="
