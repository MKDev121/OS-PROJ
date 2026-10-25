#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector - Production Installer
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Installing Interactive Session Protector (ISP) ==="

# Check root privileges
if [ "${EUID}" -ne 0 ]; then
    echo "Error: This installer must be run as root (or via sudo)."
    exit 1
fi

# 1. Build if necessary
echo "[1/5] Building protector and overlay binaries..."
cmake -B "${ROOT_DIR}/build" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${ROOT_DIR}/build" --target protector isp_overlay

# 2. Install binaries
echo "[2/5] Installing binaries to /usr/local/bin..."
install -m 0755 "${ROOT_DIR}/build/protector" /usr/local/bin/protector
install -m 0755 "${ROOT_DIR}/build/isp_overlay" /usr/local/bin/isp_overlay

# 3. Install configuration
echo "[3/5] Setting up configuration in /etc/isp/..."
mkdir -p /etc/isp
if [ ! -f /etc/isp/protector.conf ]; then
    install -m 0644 "${ROOT_DIR}/config/protector.conf" /etc/isp/protector.conf
    echo "      Installed default config to /etc/isp/protector.conf"
else
    echo "      Preserving existing /etc/isp/protector.conf"
fi

# 4. Install systemd service
echo "[4/6] Installing systemd service..."
install -m 0644 "${SCRIPT_DIR}/isp.service" /etc/systemd/system/isp.service
systemctl daemon-reload

# 5. Install autostart desktop entry for user desktop sessions
echo "[5/6] Installing desktop overlay HUD autostart entry..."
mkdir -p /etc/xdg/autostart
cat << 'EOF' > /etc/xdg/autostart/isp_overlay.desktop
[Desktop Entry]
Type=Application
Name=Interactive Session Protector Overlay
Comment=Floating HUD displaying resource protection during video calls
Exec=/usr/local/bin/isp_overlay
Terminal=false
Categories=Utility;System;
X-GNOME-Autostart-enabled=true
EOF
chmod 0644 /etc/xdg/autostart/isp_overlay.desktop

# Restart service if already running
if systemctl is-active --quiet isp.service; then
    echo "Restarting isp.service with updated binary..."
    systemctl restart isp.service
fi

# 6. Summary
echo "[6/6] Installation complete!"
echo ""
echo "To enable and start the daemon as a system service:"
echo "    sudo systemctl enable --now isp.service"
echo ""
echo "To view live daemon status and protected sessions:"
echo "    protector --status"
echo ""
echo "The Dear ImGui floating session shield HUD autostarts on login,"
echo "stays hidden when idle, and automatically turns on when any video call starts."
echo "To test or run it manually in this session:"
echo "    isp_overlay &"
echo ""
echo "To view system logs:"
echo "    journalctl -u isp.service -f"
echo "======================================================"
