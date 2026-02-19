#!/bin/bash
# CinePi Recovery Script
# Disables autostart if app keeps crashing

set -e

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  CinePi Recovery Script v1.2.2"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Check if running as sudo
if [ "$EUID" != 0 ]; then
    echo "ERROR: This script must be run with sudo"
    exit 1
fi

echo "[1/4] Stopping cinepi_app service..."
systemctl stop cinepi_app 2>/dev/null || true

echo "[2/4] Disabling autostart..."
systemctl disable cinepi_app

echo "[3/4] Pulling latest fixes from GitHub..."
cd /home/pi/cinepi_app
sudo -u pi git pull origin main

echo "[4/4] Rebuilding application..."
cd /home/pi/cinepi_app/build
# Clean rebuild
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Recovery Complete!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Next steps:"
echo "  1. Test the app manually:"
echo "     /home/pi/cinepi_app/build/cinepi_app"
echo ""
echo "  2. If it works, re-enable autostart:"
echo "     sudo bash ../scripts/setup_autostart.sh"
echo ""
echo "  3. Reboot to test:"
echo "     sudo reboot"
echo ""
