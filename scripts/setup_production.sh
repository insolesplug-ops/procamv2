#!/bin/bash
##############################################################################
# CinePi Camera - Production System Setup
# Target: Raspberry Pi 3 Model A+ running Raspberry Pi OS Bookworm (64-bit)
#
# This script:
#   1. Installs all dependencies
#   2. Configures boot parameters for DSI display + IMX219
#   3. Clones and builds LVGL
#   4. Builds the application
#   5. Installs systemd services (app + boot splash)
#   6. Configures silent boot
#
# Usage: sudo bash setup_production.sh
##############################################################################

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[SETUP]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ─── Preflight Checks ───────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    err "This script must be run as root (sudo)"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INSTALL_DIR="/home/pi/cinepi_app"
PHOTO_DIR="/home/pi/photos"

log "Project directory: $PROJECT_DIR"
log "Install directory: $INSTALL_DIR"

# ─── 1. System Update & Dependencies ────────────────────────────────
log "Installing system dependencies..."
apt-get update -qq

apt-get install -y -qq \
    build-essential cmake git pkg-config \
    libdrm-dev libgbm-dev \
    libcamera-dev libcamera-apps \
    libjpeg-dev libturbojpeg0-dev \
    libgpiod-dev i2c-tools libi2c-dev \
    nlohmann-json3-dev \
    fbi \
    gdb valgrind

log "Dependencies installed."

# ─── 2. Clone LVGL if not present ───────────────────────────────────
LVGL_DIR="$PROJECT_DIR/lvgl"
if [[ ! -d "$LVGL_DIR" ]]; then
    log "Cloning LVGL v8.3..."
    cd "$PROJECT_DIR"
    git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl.git
else
    log "LVGL already present at $LVGL_DIR"
fi

# ─── 3. Boot Configuration ──────────────────────────────────────────
log "Configuring boot parameters..."

BOOT_CONFIG="/boot/firmware/config.txt"
# Fallback for older Pi OS
[[ ! -f "$BOOT_CONFIG" ]] && BOOT_CONFIG="/boot/config.txt"

# Backup existing config
cp "$BOOT_CONFIG" "${BOOT_CONFIG}.bak.$(date +%s)"

cat > "$BOOT_CONFIG" << 'BOOTCFG'
[all]
# ─── GPU ─────────────────────────────────────────
gpu_mem=128
dtoverlay=vc4-kms-v3d

# ─── Camera (IMX219) ────────────────────────────
start_x=1
camera_auto_detect=1

# ─── Display (Waveshare 4.3" DSI) ───────────────
dtoverlay=vc4-kms-dsi-generic
framebuffer_width=480
framebuffer_height=800
display_lcd_rotate=1

# ─── Silent Boot ────────────────────────────────
disable_splash=1
boot_delay=0

# ─── I2C ─────────────────────────────────────────
dtparam=i2c_arm=on
dtparam=i2c1=on

# ─── Performance ────────────────────────────────
arm_freq=1200
core_freq=400

# ─── Power (disable unused) ─────────────────────
dtoverlay=disable-bt
BOOTCFG

log "Boot config written to $BOOT_CONFIG"

# ─── 4. Kernel Command Line (Silent Boot) ───────────────────────────
CMDLINE="/boot/firmware/cmdline.txt"
[[ ! -f "$CMDLINE" ]] && CMDLINE="/boot/cmdline.txt"

# Backup
cp "$CMDLINE" "${CMDLINE}.bak.$(date +%s)"

# Read existing PARTUUID
PARTUUID=$(grep -oP 'root=PARTUUID=\K[^ ]+' "$CMDLINE" || echo "")
if [[ -z "$PARTUUID" ]]; then
    # Try to detect
    PARTUUID=$(blkid -s PARTUUID -o value /dev/mmcblk0p2 2>/dev/null || echo "fixme")
fi

echo "console=serial0,115200 console=tty3 root=PARTUUID=${PARTUUID} rootfstype=ext4 quiet splash loglevel=0 logo.nologo vt.global_cursor_default=0 fsck.mode=force fsck.repair=yes" > "$CMDLINE"

log "Kernel command line configured for silent boot"

# ─── 5. Build Application ───────────────────────────────────────────
log "Building CinePi application..."

mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

# Use all cores but limit to avoid OOM on 512MB
NPROC=$(nproc)
[[ $NPROC -gt 2 ]] && NPROC=2

make -j"$NPROC"

log "Build successful."

# ─── 6. Install Application ─────────────────────────────────────────
log "Installing application..."

mkdir -p "$INSTALL_DIR/build"
mkdir -p "$INSTALL_DIR/assets"
mkdir -p "$PHOTO_DIR"

cp "$PROJECT_DIR/build/cinepi_app" "$INSTALL_DIR/build/"
cp "$PROJECT_DIR/boot_logo.png" "$INSTALL_DIR/assets/" 2>/dev/null || true
cp "$PROJECT_DIR/Inter_regular.ttf" "$INSTALL_DIR/assets/" 2>/dev/null || true
cp "$PROJECT_DIR/inter_bold.ttf" "$INSTALL_DIR/assets/" 2>/dev/null || true

chown -R pi:pi "$INSTALL_DIR" "$PHOTO_DIR"
chmod +x "$INSTALL_DIR/build/cinepi_app"

log "Application installed to $INSTALL_DIR"

# ─── 7. Systemd Services ────────────────────────────────────────────
log "Installing systemd services..."

# Main application service
cat > /etc/systemd/system/cinepi.service << EOF
[Unit]
Description=CinePi Camera Application
After=multi-user.target

[Service]
Type=simple
User=root
WorkingDirectory=$INSTALL_DIR
Environment="XDG_RUNTIME_DIR=/run/user/0"
ExecStart=$INSTALL_DIR/build/cinepi_app
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

# Boot splash service
cat > /etc/systemd/system/splash@boot.service << EOF
[Unit]
Description=Boot Splash Screen
DefaultDependencies=no
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/bin/fbi -noverbose -a $INSTALL_DIR/assets/boot_logo.png
StandardOutput=null
StandardError=null

[Install]
WantedBy=sysinit.target
EOF

systemctl daemon-reload
systemctl enable cinepi.service
systemctl enable splash@boot.service

log "Systemd services installed and enabled."

# ─── 8. Permissions ─────────────────────────────────────────────────
log "Setting up permissions..."

# Add pi user to required groups
usermod -a -G video,input,i2c,gpio pi 2>/dev/null || true

# udev rules for DRM/input access
cat > /etc/udev/rules.d/99-cinepi.rules << 'UDEVRULES'
# DRM
SUBSYSTEM=="drm", GROUP="video", MODE="0660"
# Input (touch)
SUBSYSTEM=="input", GROUP="input", MODE="0660"
# I2C
SUBSYSTEM=="i2c-dev", GROUP="i2c", MODE="0660"
# GPIO
SUBSYSTEM=="gpio", GROUP="gpio", MODE="0660"
UDEVRULES

udevadm control --reload-rules

log "Permissions configured."

# ─── 9. Disable Unnecessary Services ────────────────────────────────
log "Disabling unnecessary services for faster boot..."

systemctl disable bluetooth.service 2>/dev/null || true
systemctl disable hciuart.service 2>/dev/null || true
systemctl disable triggerhappy.service 2>/dev/null || true
systemctl disable avahi-daemon.service 2>/dev/null || true
systemctl disable ModemManager.service 2>/dev/null || true

# ─── Done ────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}  CinePi Camera setup complete!            ${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo "  Binary:    $INSTALL_DIR/build/cinepi_app"
echo "  Photos:    $PHOTO_DIR"
echo "  Config:    /home/pi/.cinepi_config.json"
echo "  Service:   systemctl status cinepi"
echo "  Logs:      journalctl -u cinepi -f"
echo ""
echo -e "${YELLOW}  Reboot to apply boot configuration:${NC}"
echo "    sudo reboot"
echo ""
