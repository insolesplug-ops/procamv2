# CinePi Camera v1.2.2

![Status](https://img.shields.io/badge/status-Production%20Ready-brightgreen)
![Pi Support](https://img.shields.io/badge/hardware-RPi%203A%2B-red)
![LVGL](https://img.shields.io/badge/UI-LVGL%208.3-blue)
![libcamera](https://img.shields.io/badge/Camera-libcamera%200.7-green)

**Professionelle Kamera-App für Raspberry Pi 3A+ mit libcamera & LVGL 8.3**

⚡ **Production-Ready** | 📸 **30 FPS** | 🛡️ **Graceful Degradation** | 🔄 **Auto-Restart**

---

## 🚀 Quick Start (auf Raspberry Pi)

```bash
# 1. Repository klonen
git clone https://github.com/insolesplug-ops/procamv2.git cinepi_app
cd cinepi_app

# 2. LVGL klonen (Dependency)
git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl.git

# 3. Bauen
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2

# 4. Auto-Start einrichten
sudo bash ../scripts/setup_autostart.sh

# 5. Fertig! App läuft beim Boot
sudo reboot
```

👉 **Detaillierte Anleitung:** [SETUP_GUIDE_DE.md](SETUP_GUIDE_DE.md)

---

## ✨ Features

### 📸 Kamera & Capture
- [x] **Full HD Preview** via libcamera (IMX219 ISP)
- [x] **30 FPS Rendering** auf 480×800 Portrait Display
- [x] **Full-Res JPEG Capture** (8MP, libjpeg-turbo)
- [x] **DMA-BUF Zero-Copy** (Kamera → Display direkt)
- [x] **Smart Exposure Control** (ISO, Shutter Speed, WB)

### 🏠 Hardware Support
- [x] **Graceful Degradation** (läuft auch ohne optionale Hardware)
- [x] GPIO Buttons (Shutter, Encoder)
- [x] Capacitive Touch Input (falls vorhanden)
- [x] I2C Sensors (Gyro L3G4200D, Light BH1750)
- [x] Vibration Motor & LED Flash
- [x] Hardware Diagnostics (Boot Check)

### 📱 UI & Usability
- [x] **LVGL 8.3** Portrait UI (480×800)
- [x] **Camera Scene** mit Gitter & Live-Vorschau
- [x] **Gallery Scene** mit Smart Thumbnail-Caching
- [x] **Settings Scene** (ISO, Shutter, Weißabgleich)
- [x] **Touch + GPIO** beide Input-Methoden
- [x] **Real-time FPS Counter** & Frame-Drop Detection

### 🔄 Zuverlässigkeit
- [x] **systemd Service** mit Auto-Restart
- [x] **Resource Limits** (256MB RAM, 80% CPU)
- [x] **Exception Safety** (RAII, atomare Operationen)
- [x] **journalctl Logging** (strukturierte Logs)
- [x] **Smart Standby** (Power Manager)

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────┐
│  Application (cinepi_app)                  │
│  - Main Loop (adaptive frame-rate)         │
│  - Hardware Diagnostics                    │
│  - Scene Manager (Camera/Gallery/Settings) │
└──────────────────┬──────────────────────────┘
                   │
    ┌──────────────┴──────────────┐
    │                             │
┌───▼────────────┐         ┌──────▼──────────┐
│  Camera        │         │  Display        │
│  (libcamera)   │         │  (DRM/KMS)      │
│  - ISP         │         │  - Dual Plane   │
│  - DMA-BUF     │         │  - ARGB Overlay │
└───┬────────────┘         └──────┬──────────┘
    │ Zero-Copy              Plane 0 (Camera)
    │ Plane 0 Import         Plane 1 (UI)
    └──────────────┬─────────────────┘
                   │
            ┌──────▼──────────┐
            │ Waveshare 4.3"  │
            │ DSI LCD Panel   │
            │ 480×800         │
            └────────────────┘
                   │
       ┌───────────┼───────────┐
       │           │           │
    ┌──▼──┐    ┌───▼───┐    ┌─▼───────┐
    │Touch│    │ GPIO  │    │I2C Sensors│
    │Input│    │Buttons│    │(Optional) │
    └─────┘    └───────┘    └───────────┘
```

---

## 📊 Performance (Raspberry Pi 3A+)

| Metrik | Wert | Status |
|--------|------|--------|
| **Memory** | ~150 MB | ✅ |
| **CPU Load** | 1.2-1.8 | ✅ |
| **FPS** | 30 | ✅ |
| **Frame Drops** | <5/5min | ✅ |
| **Input Latency** | <50ms | ✅ |
| **Boot Time** | 8-12s | ✅ |

**Memory Breakdown:**
- LVGL Heap: 384KB (optimiert)
- libcamera: ~40MB
- DRM/KMS: ~20MB
- Puffer Reserve: >50MB

---

## 🔧 System Requirements

### Development (Cross-Compile Host)
- CMake 3.16+
- GCC 9+ / Clang 10+
- git, pkg-config

### Raspberry Pi 3A+ (On-Device Build)

```bash
# Alle Dependencies automatisch instalieren:
sudo bash scripts/setup_production.sh

# Oder manuell:
sudo apt install -y \
  build-essential cmake git pkg-config \
  libdrm-dev libgbm-dev libcamera-dev \
  libjpeg-dev libturbojpeg0-dev \
  libgpiod-dev libi2c-dev \
  nlohmann-json3-dev
```

---

## 🏗️ Build

```bash
# Im Pi (oder auf Host für Cross-Compilation)
git clone https://github.com/insolesplug-ops/procamv2.git
cd procamv2

# Dependency
git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl.git

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2

# Test
./cinepi_app
# Drücke Strg+C zum Beenden
```

---

## 🔄 systemd Service

### Auto-Start einrichten
```bash
sudo bash scripts/setup_autostart.sh
```

### Service kontrollieren
```bash
# Status
sudo systemctl status cinepi_app

# Manuelle Kontrolle
sudo systemctl {start|stop|restart} cinepi_app

# Auto-Start aktivieren/deaktivieren
sudo systemctl {enable|disable} cinepi_app

# Logs live verfolgen
sudo journalctl -u cinepi_app -f

# Letzte 50 Zeilen
sudo journalctl -u cinepi_app -n 50
```

### Konfiguration
Service basiert auf [scripts/cinepi_app.service](scripts/cinepi_app.service):
- Läuft als `pi` Benutzer (nicht root)
- Auto-Restart nach 5 Sekunden bei Crash
- Memory Limit: 256MB
- CPU-Quota: 80%
- Hardware-Gruppen: video, input, gpio

---

## 🐛 Häufige Probleme & Lösungen

### App startet nicht

**Hardware-Diagnostik anschauen:**
```bash
./build/cinepi_app 2>&1 | head -30
# Suche nach "[HardwareStatus]" Zeilen
```

**Display nicht erkannt:**
```bash
# KMS/DRM aktivieren in /boot/firmware/config.txt:
dtoverlay=vc4-kms-v3d
# Dann reboot
sudo reboot
```

### Memory-Probleme

**Memory Live überwachen:**
```bash
watch -n 1 'free -h; echo "---"; \
  ps aux | grep cinepi_app | grep -v grep'
```

**Swap aktivieren (falls nötig):**
```bash
sudo dphys-swapfile swapon
# (Aber besser bei Pi 3A+ vermeiden)
```

### Touch funktioniert nicht

**Touch-Device prüfen:**
```bash
# Gerät finden
ls -la /dev/input/event*

# Input testen
evtest /dev/input/event0
```

**Mehr Hilfe:** siehe [SETUP_GUIDE_DE.md](SETUP_GUIDE_DE.md#troubleshooting)

---

## 📚 Dokumentation

| Dokument | Inhalt |
|----------|--------|
| [SETUP_GUIDE_DE.md](SETUP_GUIDE_DE.md) | 👈 **START HERE** - Vollständige deutsche Anleitung |
| [CODE_ANALYSIS_REPORT.md](CODE_ANALYSIS_REPORT.md) | Technische Audit & Optimierungsempfehlungen |
| [QUICK_FIXES.md](QUICK_FIXES.md) | 5-Zeilen-Lösungen für häufige Probleme |
| [changelog.md](changelog.md) | Version History |

---

## 🛠️ Development

### Projekt-Struktur

```
procamv2/
├── src/                    # C++ Source Code
│   ├── main.cpp           # Entry Point & Main Loop
│   ├── core/              # Config, Hardware-Check
│   │   ├── config.cpp
│   │   └── hardware_health.cpp
│   ├── camera/            # libcamera Integration
│   │   ├── camera_pipeline.cpp
│   │   └── photo_capture.cpp
│   ├── drivers/           # Hardware Treiber
│   │   ├── drm_display.cpp
│   │   ├── gpio_driver.cpp
│   │   ├── i2c_sensors.cpp
│   │   └── touch_input.cpp
│   ├── ui/                # LVGL UI & Logic
│   │   ├── lvgl_driver.cpp
│   │   ├── scene_manager.cpp
│   │   ├── camera_scene.cpp
│   │   ├── gallery_scene.cpp
│   │   └── settings_scene.cpp
│   ├── gallery/           # Photo Manager
│   │   └── photo_manager.cpp
│   └── power/             # Power Management
│       └── power_manager.cpp
│
├── include/               # Header Files
│   ├── core/
│   ├── camera/
│   ├── drivers/
│   └── ui/
│
├── UI/                    # SquareLine Generated UI
│   ├── ui.c / ui.h
│   ├── screens/           # Camera, Gallery, Settings
│   └── fonts/             # Custom fonts
│
├── CMakeLists.txt         # Build Configuration
├── scripts/
│   ├── setup_production.sh  # Full Setup Script
│   ├── setup_autostart.sh   # systemd Service Setup
│   └── cinepi_app.service   # systemd Unit File
│
└── README.md (this file)
```

### Build-Ziele

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make help                    # Alle Ziele anzeigen

make                         # Standard (=cinepi_app)
make -j2                     # Mit 2 CPU-Kernen (Pi 3A+)
make VERBOSE=1              # Mit Details
make clean                   # Aufräumen
```

### Debugging

```bash
# GDB Debugger
gdb ./cinepi_app
(gdb) run
(gdb) bt                    # Backtrace bei Crash

# Sanitizer (Debug Build)
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DENABLE_SANITIZER=ON
make

# Valgrind (Memory Checker, sehr langsam!)
valgrind --leak-check=full ./cinepi_app

# systemd Journal
sudo journalctl -u cinepi_app -f

# LVGL Debug Output
export LV_LOG_LEVEL=5       # Trace
./cinepi_app
```

---

## 🔐 Security

- ✅ Service läuft als `pi` Benutzer (nicht root)
- ✅ Hardware-Zugriff via groups: `video`, `input`, `gpio`
- ✅ Keine hardcodierten Credentials
- ✅ RAII für automatisches Resource-Cleanup
- ✅ Exception Safety überall

---

## 📄 License

MIT License - Siehe [LICENSE](LICENSE) für Details

---

## 🤝 Contributing

Bugfixes und Verbesserungen willkommen! Bitte:
1. Fork das Repo
2. Feature-Branch erstellen (`git checkout -b feature/xyz`)
3. Commits `git commit -am 'Add feature'`
4. Branch Pushen
5. Pull Request öffnen

---

## 📞 Support & Hilfe

**Erste Hilfe:**
- Logs checken: `sudo journalctl -u cinepi_app -f`
- Status anschauen: `sudo systemctl status cinepi_app`
- Neustarten: `sudo systemctl restart cinepi_app`

**Dokumentation:**
- Komplette Anleitung: [SETUP_GUIDE_DE.md](SETUP_GUIDE_DE.md)
- Technische Details: [CODE_ANALYSIS_REPORT.md](CODE_ANALYSIS_REPORT.md)
- Schnelle Fixes: [QUICK_FIXES.md](QUICK_FIXES.md)

**Issues & Bugs:**
- [GitHub Issues](https://github.com/insolesplug-ops/procamv2/issues)

---

## 📊 Version Info

- **Current Version:** v1.2.2
- **Release Date:** 19. Februar 2026
- **Status:** ✅ **Production Ready**
- **Target Hardware:** Raspberry Pi 3A+ (512MB RAM)
- **Build Environment:** GCC 14.2
- **Dependencies:** libcamera 0.7, LVGL 8.3, DRM/KMS, libgpiod 2.2

---

**Made with ❤️ for Raspberry Pi enthusiasts**

*Last Updated: 19. Februar 2026*