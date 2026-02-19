/**
 * CinePi Camera - Main Application Entry Point
 * Raspberry Pi 3A+ / Waveshare 4.3" DSI / IMX219
 *
 * Zero-copy architecture:
 *   libcamera -> DMA-BUF -> DRM Plane 0 (camera)
 *   LVGL -> Dumb Buffer -> DRM Plane 1 (UI overlay)
 */

#include "core/config.h"
#include "core/constants.h"
#include "drivers/drm_display.h"
#include "drivers/touch_input.h"
#include "drivers/gpio_driver.h"
#include "drivers/i2c_sensors.h"
#include "camera/camera_pipeline.h"
#include "camera/photo_capture.h"
#include "ui/lvgl_driver.h"
#include "ui/scene_manager.h"
#include "ui/camera_scene.h"
#include "ui/gallery_scene.h"
#include "ui/settings_scene.h"
#include "gallery/photo_manager.h"
#include "power/power_manager.h"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <sys/stat.h>

using namespace cinepi;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    fprintf(stderr, "\n[Main] Signal %d received, shutting down...\n", sig);
    g_running = false;
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "╔═══════════════════════════════════════╗\n");
    fprintf(stderr, "║     CinePi Camera v1.0.0              ║\n");
    fprintf(stderr, "║     Raspberry Pi 3A+ / IMX219          ║\n");
    fprintf(stderr, "╚═══════════════════════════════════════╝\n");

    // ─── Signal Handling ────────────────────────────────────────────
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    // ─── Load Configuration ─────────────────────────────────────────
    auto& config = ConfigManager::instance();
    config.load();

    // Ensure photo directory exists
    mkdir(config.get().photo_dir.c_str(), 0755);

    fprintf(stderr, "[Main] Config loaded (ISO=%d, Shutter=%dus)\n",
            config.get().camera.iso, config.get().camera.shutter_us);

    // ─── Initialize Hardware ────────────────────────────────────────
    DrmDisplay display;
    if (!display.init()) {
        fprintf(stderr, "[Main] FATAL: DRM display init failed\n");
        return 1;
    }

    TouchInput touch;
    if (!touch.init()) {
        fprintf(stderr, "[Main] WARNING: Touch init failed (continuing without touch)\n");
    }

    GpioDriver gpio;
    if (!gpio.init()) {
        fprintf(stderr, "[Main] WARNING: GPIO init failed (continuing without GPIO)\n");
    }

    I2CSensors sensors;
    if (!sensors.init()) {
        fprintf(stderr, "[Main] WARNING: I2C sensors init failed (continuing without sensors)\n");
    }
    sensors.start_polling();

    // ─── Initialize Camera ──────────────────────────────────────────
    CameraPipeline camera;
    if (!camera.init()) {
        fprintf(stderr, "[Main] FATAL: Camera init failed\n");
        return 1;
    }

    // Connect camera frames to DRM display (zero-copy DMA-BUF)
    camera.set_frame_callback([&display](int dmabuf_fd, int w, int h, int stride, uint32_t fmt) {
        display.set_camera_dmabuf(dmabuf_fd, w, h, stride, fmt);
    });

    // ─── Initialize LVGL + UI ───────────────────────────────────────
    LvglDriver lvgl;
    if (!lvgl.init(display, touch)) {
        fprintf(stderr, "[Main] FATAL: LVGL init failed\n");
        return 1;
    }

    SceneManager scene_mgr;
    scene_mgr.init(camera, gpio, sensors, display, lvgl);

    CameraScene camera_scene;
    camera_scene.init();

    GalleryScene gallery_scene;
    gallery_scene.init();

    SettingsScene settings_scene;
    settings_scene.init();

    // ─── Initialize Photo Manager ───────────────────────────────────
    PhotoManager photo_mgr;
    photo_mgr.init(camera, gpio, sensors);
    photo_mgr.on_capture_done([](bool success, const std::string& path) {
        if (success) {
            fprintf(stderr, "[Main] Photo saved: %s\n", path.c_str());
        }
    });

    // ─── Initialize Power Manager ───────────────────────────────────
    PowerManager power;
    power.init(display, camera, touch, gpio, sensors, lvgl);
    power.set_timeout(config.get().display.standby_sec);

    // ─── Start Camera Preview ───────────────────────────────────────
    if (!camera.start_preview()) {
        fprintf(stderr, "[Main] FATAL: Camera preview start failed\n");
        return 1;
    }

    // ─── Set Initial Backlight ──────────────────────────────────────
    {
        FILE* fp = fopen(BACKLIGHT_BRIGHTNESS, "w");
        if (fp) {
            fprintf(fp, "%d", config.get().display.brightness);
            fclose(fp);
        }
    }

    fprintf(stderr, "[Main] ═══ Entering main loop ═══\n");

    // ─── Main Loop ──────────────────────────────────────────────────
    // Target: 30 FPS = 33ms per frame
    using clock = std::chrono::steady_clock;
    auto frame_duration = std::chrono::milliseconds(33);
    Scene prev_scene = Scene::Camera;

    while (g_running) {
        auto frame_start = clock::now();

        // Power management (standby/wake)
        power.update();

        if (!power.is_standby()) {
            // Update LVGL (process timers, animations, input)
            lvgl.tick();

            // Update scene manager (status bar, level, callbacks)
            scene_mgr.update();

            // Scene-specific updates
            Scene cur = scene_mgr.current_scene();
            if (cur != prev_scene) {
                // Scene transition
                if (prev_scene == Scene::Gallery) gallery_scene.leave();
                if (prev_scene == Scene::Settings) settings_scene.leave();
                if (cur == Scene::Gallery) gallery_scene.enter();
                if (cur == Scene::Settings) settings_scene.enter();
                prev_scene = cur;
            }

            if (cur == Scene::Camera) {
                camera_scene.update(camera, sensors);
            }

            // Commit DRM planes
            display.commit();
        }

        // Frame rate limiting
        auto frame_end = clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frame_end - frame_start);
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    // ─── Cleanup ────────────────────────────────────────────────────
    fprintf(stderr, "[Main] Shutting down...\n");

    config.save();
    camera.stop_preview();
    sensors.stop_polling();
    camera.deinit();
    gpio.deinit();
    touch.deinit();
    sensors.deinit();
    lvgl.deinit();
    display.deinit();

    fprintf(stderr, "[Main] Goodbye.\n");
    return 0;
}
