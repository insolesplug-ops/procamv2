# CinePi Camera v1.2.2 - Complete Setup Guide für Raspberry Pi 3A+

## 📋 Voraussetzungen

- Raspberry Pi 3A+ (512MB RAM)
- Waveshare 4.3" DSI Touch Display (480x800)
- IMX219 Camera Module
- Micro SD-Karte (16GB+)
- Raspberry Pi OS (64-bit oder 32-bit)

---

## 🚀 **SCHRITT 1: Raspberry Pi OS Einrichtung**

```bash
# 1. SSH verbinden
ssh pi@<your-pi-ip>

# 2. System aktualisieren
sudo apt-get update
sudo apt-get upgrade -y

# 3. Benötigte Pakete installieren
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    libdrm-dev libgbm-dev libcamera-dev libcamera-apps \
    libjpeg-dev libturbojpeg0-dev \
    libgpiod-dev i2c-tools libi2c-dev \
    nlohmann-json3-dev fbi gdb valgrind

# 4. Git konfigurieren (optional)
git config --global user.name "YourName"
git config --global user.email "you@example.com"
```

---

## 📥 **SCHRITT 2: Projekt klonen und bauen**

```bash
# 1. Repository klonen
cd ~
git clone https://github.com/insolesplug-ops/procamv2.git cinepi_app
cd cinepi_app

# 2. LVGL Abhängigkeit klonen
git clone --depth 1 -b release/v8.3 https://github.com/lvgl/lvgl.git

# 3. Build-Verzeichnis erstellen
mkdir build && cd build

# 4. CMake konfigurieren
cmake .. -DCMAKE_BUILD_TYPE=Release

# 5. Kompilieren (dauert ca. 15-20 Minuten auf Pi 3A+)
make -j2

# ✓ Bei erfolg: executable "build/cinepi_app" erstellt
```

---

## 🔧 **SCHRITT 3: Autostart einrichten**

```bash
# 1. Setup-Script ausführen
cd ~/cinepi_app
sudo bash scripts/setup_autostart.sh

# 2. Service prüfen
sudo systemctl status cinepi_app

# 3. Testen (App sollte starten)
sudo systemctl start cinepi_app

# 4. Logs ansehen (für Debugging)
sudo journalctl -u cinepi_app -f
```

---

## 📺 **SCHRITT 4: Display-Konfiguration (Waveshare DSI)**

Füge folgendes zur `/boot/firmware/config.txt` hinzu:

```bash
sudo nano /boot/firmware/config.txt
```

Ergänze diese Zeilen:

```ini
# ─── GPU Memory ─────────────────────────
gpu_mem=128

# ─── DRM/KMS Display ────────────────────
dtoverlay=vc4-kms-v3d
dtoverlay=dpi24
dpi_group=2
dpi_mode=87
dpi_timings=480 0 20 0 50 800 0 10 0 10 0 0 0 60 0 6400000 1

# ─── Camera ─────────────────────────────
dtoverlay=imx219

# ─── Disable default rotation ───────────
display_rotate=0

# ─── Enable I2C (für Sensoren) ──────────
dtparam=i2c_arm=on
```

Dann **reboot**: `sudo reboot`

---

## 🎮 **SCHRITT 5: Runtime-Befehle**

```bash
# ✓ App starten
sudo systemctl start cinepi_app

# ✓ App stoppen
sudo systemctl stop cinepi_app

# ✓ App neu starten
sudo systemctl restart cinepi_app

# ✓ Status prüfen
sudo systemctl status cinepi_app

# ✓ Logs live ansehen
sudo journalctl -u cinepi_app -f

# ✓ Letzte 50 Zeilen logs
sudo journalctl -u cinepi_app -n 50

# ✓ Autostart deaktivieren
sudo systemctl disable cinepi_app

# ✓ Autostart reaktivieren
sudo systemctl enable cinepi_app
```

---

## 🐛 **Troubleshooting**

### Camera funktioniert nicht
```bash
libcamera-hello --help
# Wenn Fehler: sudo raspi-config → Interface Options → libcamera aktivieren
```

### Display bleibt schwarz
```bash
# DRM/KMS Backend prüfen
cat /sys/class/graphics/fb0/name

# LVGL Logs prüfen
sudo journalctl -u cinepi_app -f | grep -i "display\|drm"
```

### Touch Input funktioniert nicht
```bash
# Touch-Gerät testen
cat /dev/input/event0
# Mit Finger antippen - sollte Ausgabe zeigen

# Oder: Input-Events prüfen
sudo apt install evtest
sudo evtest /dev/input/event0
```

### Memory-Fehler
```bash
# Speichernutzung prüfen
free -h
systemctl status cinepi_app | grep Memory

# Bei zu hohem Speicher:
# 1. LVGL buffer reduzieren: include/core/lv_conf.h LV_MEM_SIZE
# 2. Galerie-Cache kleiner machen
```

### CPU-Last hoch
```bash
# Top-Prozesse anschauen
top

# FPS-Logs prüfen
sudo journalctl -u cinepi_app -f | grep FPS
```

---

## 📊 **Performance-Metriken (Erwartet auf Pi 3A+)**

| Metrik | Zielwert | Max-Warnung |
|--------|----------|------------|
| **Speicher** | ~150 MB | > 200 MB |
| **CPU-Load** | 1.2 - 1.8 | > 2.5 |
| **FPS** | 30 | < 25 |
| **Frame-Drops** | 0-5 / 5min | > 20 / 5min |

---

## 🔐 **Sicherheit & Berechtigungen**

```bash
# Service läuft als 'pi' User mit:
# - video group (für Kamera/Display)
# - input group (für Touch)
# - gpio group (für Buttons/Flash)

# Berechtigungen überprüfen
groups pi

# Falls Gruppe fehlt:
sudo usermod -aG video pi
sudo usermod -aG input pi
sudo usermod -aG gpio pi
# Dann: ssh neu verbinden
```

---

## 📝 **Update-Prozess**

```bash
# 1. Code von GitHub holen
cd ~/cinepi_app
git pull origin main

# 2. Neu bauen
cd build
cmake ..
make -j2

# 3. Service neu starten
sudo systemctl restart cinepi_app

# 4. Logs prüfen
sudo journalctl -u cinepi_app -f
```

---

## 📞 **Support & Debugging**

**Komplette Logs dumpen:**
```bash
sudo journalctl -u cinepi_app -n 1000 > cinepi_logs.txt
```

**Systemd Service Status:**
```bash
systemctl cat cinepi_app
```

**Automatischer Neustart aktivieren:**
```bash
# Ist bereits aktiviert! (Restart=on-failure, RestartSec=5)
# Kann in scripts/cinepi_app.service angepasst werden
```

---

## 🎉 **Fertig!**

Die App sollte jetzt:
- ✅ Beim Booten automatisch starten
- ✅ 30 FPS auf dem Display zeigen
- ✅ Touch-Input unterstützen
- ✅ Bei Crash automatisch neu starten
- ✅ Ressourcen-effizient laufen (Pi 3A+ optimiert)

**Viel Erfolg beim Fotografieren! 📸**

---

**Version:** v1.2.2 (Februar 2026)
**Target:** Raspberry Pi 3A+ mit Waveshare 4.3" DSI
**Support:** Siehe GitHub Issues
