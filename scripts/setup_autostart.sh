#!/bin/bash
####################################################################
# CinePi Camera - Autostart Setup
# Enables the application to start automatically on boot
####################################################################

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  CinePi Camera - Autostart Setup v1.0  ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════╝${NC}\n"

PROJECT_DIR="/home/pi/cinepi_app"

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}✗ This script must be run as root (use: sudo bash)${NC}"
    exit 1
fi

# Check if project directory exists
if [[ ! -d "$PROJECT_DIR" ]]; then
    echo -e "${RED}✗ Project directory not found: $PROJECT_DIR${NC}"
    exit 1
fi

# Check if executable exists
if [[ ! -f "$PROJECT_DIR/build/cinepi_app" ]]; then
    echo -e "${RED}✗ Executable not found: $PROJECT_DIR/build/cinepi_app${NC}"
    echo "    Please build the project first: cd $PROJECT_DIR/build && make"
    exit 1
fi

echo -e "${YELLOW}[1/3] Setting up systemd service...${NC}"

# Copy service file
if [[ ! -f "$PROJECT_DIR/scripts/cinepi_app.service" ]]; then
    echo -e "${RED}✗ Service file not found!${NC}"
    exit 1
fi

cp "$PROJECT_DIR/scripts/cinepi_app.service" /etc/systemd/system/cinepi_app.service
chmod 644 /etc/systemd/system/cinepi_app.service
echo -e "${GREEN}✓ Service file installed${NC}"

echo -e "${YELLOW}[2/3] Reloading systemd daemon...${NC}"
systemctl daemon-reload
echo -e "${GREEN}✓ Systemd reloaded${NC}"

echo -e "${YELLOW}[3/3] Install complete (NOT enabled yet for safety)${NC}"
echo -e "${GREEN}✓ Service installed (but not auto-starting yet)${NC}"

echo ""
echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  ✓ Installation Complete - MANUAL START REQUIRED!         ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}\n"

echo -e "${YELLOW}IMPORTANT - Service is NOT auto-starting${NC}"
echo "This is intentional - test manually first to avoid boot crashes!"
echo ""
echo "Step 1: Test the app manually"
echo "  ${YELLOW}sudo systemctl start cinepi_app${NC}"
echo "  ${YELLOW}sleep 2${NC}"
echo "  ${YELLOW}sudo journalctl -u cinepi_app -n 20${NC}"
echo ""
echo "Step 2: If it works, enable autostart"
echo "  ${YELLOW}sudo systemctl enable cinepi_app${NC}"
echo ""
echo "Step 3: Then reboot"
echo "  ${YELLOW}sudo reboot${NC}"
echo ""
echo "Other commands:"
echo "  Stop:        ${YELLOW}sudo systemctl stop cinepi_app${NC}"
echo "  Restart:     ${YELLOW}sudo systemctl restart cinepi_app${NC}"
echo "  Live logs:   ${YELLOW}sudo journalctl -u cinepi_app -f${NC}"
echo "  Disable:     ${YELLOW}sudo systemctl disable cinepi_app${NC}"
echo ""
