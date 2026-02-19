# CinePi Camera - Production Firmware

C++17 camera firmware for Raspberry Pi 3 Model A+ with Waveshare 4.3" DSI LCD and Sony IMX219 sensor.

## Hardware Requirements

- **SBC:** Raspberry Pi 3 Model A+ (512MB RAM)
- **Display:** Waveshare 4.3" DSI LCD (800x480, capacitive touch)
- **Camera:** Sony IMX219 8MP (CSI)
- **Battery:** LiPo 4000mAh with Waveshare UPS HAT (C)
- **Sensors:** L3G4200D gyroscope (I2C 0x69), BH1750 light sensor (I2C 0x23)
- **Controls:** Rotary encoder (GPIO 5/6/13), Shutter button (GPIO 26)
- **Output:** 6x LED flash array (GPIO 27), Vibration motor (GPIO 18)

## Architecture

```
libcamera (IMX219)
  -> DMA-BUF Export (640x480 @ 30fps)
  -> DRM Plane 0 (Camera, zero-copy)
       |
       +-- DRM Plane 1 (ARGB8888)
              <- LVGL 8.3 (UI overlay)
                   <- Touch (rotated coordinates)
```

## Quick Setup (on Raspberry Pi)

```bash
# Clone and run the setup script
cd /home/pi
git clone <repo> cinepi_app
cd cinepi_app
sudo bash scripts/setup_production.sh
sudo reboot
```

The setup script handles everything: dependencies, boot config, LVGL clone, build, systemd services, and splash screen.

## Manual Build

```bash
# Install dependencies
sudo apt install -y build-essential cmake git libdrm-dev libgbm-dev \
    libcamera-dev libcamera-apps libjpeg-dev libturbojpeg0-dev \
    libgpiod-dev i2c-tools libi2c-dev nlohmann-json3-dev fbi

# Clone LVGL
git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl.git

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2

# Run
sudo ./cinepi_app
```

## Project Structure

```
procamv2/
├── CMakeLists.txt              # Build system
├── scripts/
│   └── setup_production.sh     # Full system setup
├── include/
│   ├── core/
│   │   ├── lv_conf.h           # LVGL configuration
│   │   ├── config.h            # Application settings
│   │   └── constants.h         # Hardware/display constants
│   ├── drivers/
│   │   ├── drm_display.h       # DRM/KMS dual-plane driver
│   │   ├── touch_input.h       # Capacitive touch (rotated)
│   │   ├── gpio_driver.h       # libgpiod buttons/encoder/flash
│   │   └── i2c_sensors.h       # BH1750 + L3G4200D
│   ├── camera/
│   │   ├── camera_pipeline.h   # libcamera preview + capture
│   │   └── photo_capture.h     # JPEG encoding
│   ├── ui/
│   │   ├── lvgl_driver.h       # LVGL <-> DRM bridge
│   │   ├── scene_manager.h     # UI state management
│   │   ├── camera_scene.h      # Grid overlay + level
│   │   ├── gallery_scene.h     # JPEG viewer (IDCT scaled)
│   │   └── settings_scene.h    # Extended settings UI
│   ├── gallery/
│   │   └── photo_manager.h     # Capture orchestration
│   └── power/
│       └── power_manager.h     # Standby/wake management
├── src/                        # Implementations (.cpp)
├── UI/                         # SquareLine Studio generated code
│   ├── ui.c / ui.h
│   ├── screens/                # Main, Gallery, Settings screens
│   ├── fonts/                  # Inter font bitmaps (Font1-12)
│   └── images/
├── lvgl/                       # LVGL 8.3 (cloned by setup script)
├── assets/
│   ├── boot_logo.png           # 480x800 boot splash
│   ├── Inter_regular.ttf
│   └── inter_bold.ttf
└── README.md
```

## Display Configuration

The Waveshare 4.3" DSI is physically 800x480 landscape but used in 480x800 portrait mode. Rotation is handled by:
- `libcamera Transform::Rot90` for camera preview
- DRM plane scaling (src 480x800 -> dst 800x480)
- Touch coordinate rotation in the input driver

## Memory Budget (512MB)

| Component | Usage |
|---|---|
| System/Kernel | ~220MB |
| GPU (CMA) | 128MB |
| libcamera buffers | ~40MB |
| LVGL framebuffer | ~2MB |
| Application heap | ~50MB |
| **Total** | **~440MB** |

Gallery images are decoded with libjpeg-turbo IDCT scaling (never full 8MP in RAM).

## Configuration

Settings are stored in `/home/pi/.cinepi_config.json` and persist across reboots.

## Logs

```bash
journalctl -u cinepi -f        # Live application logs
systemctl status cinepi         # Service status
```

## Boot Sequence

| Time | Event |
|---|---|
| 0s | Power on (black screen) |
| ~1s | Boot splash (via fbi) |
| ~3-5s | Application starts, takes over display |
| ~5s | Live camera preview with UI overlay |
