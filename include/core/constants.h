#pragma once
/**
 * CinePi Camera - Hardware & Display Constants
 *
 * Display physical mode as reported by KMS/DRM (after config.txt fix):
 *   - Remove "display_lcd_rotate=1" from /boot/config.txt
 *   - Add rotate=90 to dtoverlay=vc4-kms-dsi-generic line
 *   - KMS will then report the connector as 480x800 natively
 *
 * Until config.txt is fixed the DRM mode will read as 800x480.
 * drm_display.cpp reads the actual mode at runtime via drmModeGetConnector
 * and stores it in mode_w_ / mode_h_.  The constants below describe the
 * LOGICAL (portrait) coordinate system used by LVGL and the app.
 */

#include <cstdint>

namespace cinepi {

// ─── Display (logical portrait) ─────────────────────────────────────
constexpr int DISPLAY_W         = 480;   // Portrait width  (matches LVGL canvas)
constexpr int DISPLAY_H         = 800;   // Portrait height (matches LVGL canvas)
constexpr int UI_BPP            = 32;    // ARGB8888 for overlay plane

// ─── Camera ─────────────────────────────────────────────────────────
constexpr int PREVIEW_W         = 640;   // Sensor landscape output width
constexpr int PREVIEW_H         = 480;   // Sensor landscape output height
constexpr int PREVIEW_FPS       = 30;
constexpr int CAPTURE_W         = 3280;
constexpr int CAPTURE_H         = 2464;
constexpr int CAMERA_BUF_COUNT  = 4;

// ─── GPIO (BCM numbering) ──────────────────────────────────────────
constexpr int GPIO_ENCODER_CLK  = 5;
constexpr int GPIO_ENCODER_DT   = 6;
constexpr int GPIO_ENCODER_BTN  = 13;
constexpr int GPIO_VIBRATION    = 18;
constexpr int GPIO_SHUTTER_BTN  = 26;
constexpr int GPIO_LED_FLASH    = 27;
constexpr const char* GPIO_CHIP = "/dev/gpiochip0";

// ─── I2C ────────────────────────────────────────────────────────────
constexpr const char* I2C_DEV   = "/dev/i2c-1";
constexpr uint8_t I2C_ADDR_GYRO  = 0x69;  // L3G4200D
constexpr uint8_t I2C_ADDR_LIGHT = 0x23;  // BH1750

// ─── Backlight ──────────────────────────────────────────────────────
constexpr const char* BACKLIGHT_BRIGHTNESS = "/sys/class/backlight/rpi_backlight/brightness";
constexpr const char* BACKLIGHT_POWER      = "/sys/class/backlight/rpi_backlight/bl_power";

// ─── Performance ────────────────────────────────────────────────────
constexpr int LVGL_BUF_LINES    = 40;    // Number of lines in LVGL draw buffer
constexpr int TOUCH_READ_MS     = 30;
constexpr int GYRO_READ_MS      = 100;
constexpr int LIGHT_READ_MS     = 500;
constexpr int BATTERY_READ_MS   = 5000;

// ─── Photo ──────────────────────────────────────────────────────────
constexpr int GALLERY_THUMB_W   = 480;
constexpr int GALLERY_THUMB_H   = 360;   // Aspect ratio preserved
constexpr int JPEG_QUALITY      = 95;

} // namespace cinepi
