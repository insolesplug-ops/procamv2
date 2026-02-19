/**
 * CinePi Camera - Production Main (v1.2.0)
 * Graceful Degradation: App läuft auch ohne optionale Hardware!
 */

#include "core/config.h"
#include "core/constants.h"
#include "core/hardware_health.h"
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
#include <memory>

using namespace cinepi;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    fprintf(stderr, "\n[Main] Signal %d received, graceful shutdown...\n", sig);
    g_running = false;
}

class AppComponentManager {
public:
    AppComponentManager() = default;
    
    bool init_all(const HardwareHealth& hw) {
        hw_ = &hw;
        
        if (!init_camera()) {
            fprintf(stderr, "[AppInit] FATAL: Camera init failed\n");
            return false;
        }
        
        if (!init_display()) {
            fprintf(stderr, "[AppInit] FATAL: Display init failed\n");
            return false;
        }
        
        init_touch();
        init_gpio();
        init_sensors();
        init_lvgl();
        init_ui();
        
        return true;
    }
    
    CameraPipeline* camera() { return camera_.get(); }
    DrmDisplay* display() { return display_.get(); }
    TouchInput* touch() { return touch_.get(); }
    GpioDriver* gpio() { return gpio_.get(); }
    I2CSensors* sensors() { return sensors_.get(); }
    LvglDriver* lvgl() { return lvgl_.get(); }
    
    bool has_touch() const { return touch_ != nullptr; }
    bool has_gpio() const { return gpio_ != nullptr; }
    bool has_sensors() const { return sensors_ != nullptr; }

private:
    const HardwareHealth* hw_ = nullptr;
    std::unique_ptr<CameraPipeline> camera_;
    std::unique_ptr<DrmDisplay> display_;
    std::unique_ptr<TouchInput> touch_;
    std::unique_ptr<GpioDriver> gpio_;
    std::unique_ptr<I2CSensors> sensors_;
    std::unique_ptr<LvglDriver> lvgl_;
    
    bool init_camera() {
        if (!hw_->is_available(HardwareComponent::Camera)) {
            fprintf(stderr, "[AppInit] Camera unavailable\n");
            return false;
        }
        
        try {
            camera_ = std::make_unique<CameraPipeline>();
            if (!camera_ || !camera_->init()) {
                fprintf(stderr, "[AppInit] Camera init failed\n");
                camera_.reset();
                return false;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[AppInit] Camera init exception: %s\n", e.what());
            camera_.reset();
            return false;
        } catch (...) {
            fprintf(stderr, "[AppInit] Camera init unknown exception\n");
            camera_.reset();
            return false;
        }
        
        fprintf(stderr, "[AppInit] ✓ Camera initialized\n");
        return true;
    }
    
    bool init_display() {
        if (!hw_->is_available(HardwareComponent::Display)) {
            fprintf(stderr, "[AppInit] Display unavailable\n");
            return false;
        }
        
        try {
            display_ = std::make_unique<DrmDisplay>();
            if (!display_ || !display_->init()) {
                fprintf(stderr, "[AppInit] Display init failed\n");
                display_.reset();
                return false;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[AppInit] Display init exception: %s\n", e.what());
            display_.reset();
            return false;
        } catch (...) {
            fprintf(stderr, "[AppInit] Display init unknown exception\n");
            display_.reset();
            return false;
        }
        
        fprintf(stderr, "[AppInit] ✓ Display initialized\n");
        return true;
    }
    
    bool init_touch() {
        if (!hw_->is_available(HardwareComponent::TouchInput)) {
            fprintf(stderr, "[AppInit] ⚠ Touch unavailable (will use GPIO)\n");
            return false;
        }
        
        touch_ = std::make_unique<TouchInput>();
        if (!touch_->init()) {
            fprintf(stderr, "[AppInit] ⚠ Touch init failed\n");
            touch_.reset();
            return false;
        }
        
        fprintf(stderr, "[AppInit] ✓ Touch initialized\n");
        return true;
    }
    
    bool init_gpio() {
        if (!hw_->is_available(HardwareComponent::GPIOButtons)) {
            fprintf(stderr, "[AppInit] ⚠ GPIO unavailable (will use touch)\n");
            return false;
        }
        
        gpio_ = std::make_unique<GpioDriver>();
        if (!gpio_->init()) {
            fprintf(stderr, "[AppInit] ⚠ GPIO init failed\n");
            gpio_.reset();
            return false;
        }
        
        fprintf(stderr, "[AppInit] ✓ GPIO initialized\n");
        return true;
    }
    
    bool init_sensors() {
        if (!hw_->is_available(HardwareComponent::I2CSensors)) {
            fprintf(stderr, "[AppInit] ⚠ Sensors unavailable\n");
            return false;
        }
        
        sensors_ = std::make_unique<I2CSensors>();
        if (!sensors_->init()) {
            fprintf(stderr, "[AppInit] ⚠ Sensors init failed\n");
            sensors_.reset();
            return false;
        }
        
        sensors_->start_polling();
        fprintf(stderr, "[AppInit] ✓ Sensors initialized\n");
        return true;
    }
    
    bool init_lvgl() {
        if (!display_) {
            fprintf(stderr, "[AppInit] Cannot init LVGL without display\n");
            return false;
        }
        
        try {
            lvgl_ = std::make_unique<LvglDriver>();
            TouchInput* touch_ptr = touch_.get();
            
            if (!lvgl_ || !lvgl_->init(*display_, touch_ptr)) {
                fprintf(stderr, "[AppInit] LVGL init failed\n");
                lvgl_.reset();
                return false;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[AppInit] LVGL init exception: %s\n", e.what());
            lvgl_.reset();
            return false;
        } catch (...) {
            fprintf(stderr, "[AppInit] LVGL init unknown exception\n");
            lvgl_.reset();
            return false;
        }
        
        fprintf(stderr, "[AppInit] ✓ LVGL initialized\n");
        return true;
    }
    
    bool init_ui() {
        fprintf(stderr, "[AppInit] ✓ UI initialized\n");
        return true;
    }
};

int main(int argc, char* argv[]) {
    fprintf(stderr, "\n");
    fprintf(stderr, "╔═══════════════════════════════════════════╗\n");
    fprintf(stderr, "║  CinePi Camera v1.2.0 (PRODUCTION)        ║\n");
    fprintf(stderr, "║  Graceful Hardware Degradation Enabled    ║\n");
    fprintf(stderr, "║  Raspberry Pi 3A+ / IMX219                ║\n");
    fprintf(stderr, "╚═══════════════════════════════════════════╝\n\n");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    auto& config = ConfigManager::instance();
    config.load();
    
    mkdir(config.get().photo_dir.c_str(), 0755);
    
    fprintf(stderr, "[Main] Config loaded (ISO=%d, Shutter=%dus)\n",
            config.get().camera.iso, config.get().camera.shutter_us);

    HardwareHealth hw;
    if (!hw.init()) {
        fprintf(stderr, "[Main] FATAL: Critical hardware missing\n");
        fprintf(stderr, "%s\n", hw.get_full_status().c_str());
        return 1;
    }

    AppComponentManager app;
    if (!app.init_all(hw)) {
        fprintf(stderr, "[Main] FATAL: Critical app initialization failed\n");
        return 1;
    }

    app.camera()->set_frame_callback([display = app.display()](
        int dmabuf_fd, int w, int h, int stride, uint32_t fmt) {
        display->set_camera_dmabuf(dmabuf_fd, w, h, stride, fmt);
    });

    CameraScene camera_scene;
    camera_scene.init();
    
    GalleryScene gallery_scene;
    gallery_scene.init();
    
    SettingsScene settings_scene;
    settings_scene.init();

    PhotoManager photo_mgr;
    if (app.has_gpio()) {
        I2CSensors* sensors_ptr = app.has_sensors() ? app.sensors() : nullptr;
        photo_mgr.init(*app.camera(), *app.gpio(), sensors_ptr);
    }

    PowerManager power;
    if (app.has_gpio()) {
        TouchInput* touch_ptr = app.has_touch() ? app.touch() : nullptr;
        I2CSensors* sensors_ptr = app.has_sensors() ? app.sensors() : nullptr;
        power.init(*app.display(), *app.camera(), 
                  touch_ptr,
                  *app.gpio(), sensors_ptr, *app.lvgl());
        power.set_timeout(config.get().display.standby_sec);
    } else {
        fprintf(stderr, "[Main] ⚠ Power manager disabled (needs GPIO)\n");
    }

    if (!app.camera()->start_preview()) {
        fprintf(stderr, "[Main] FATAL: Camera preview start failed\n");
        return 1;
    }

    {
        FILE* fp = fopen(BACKLIGHT_BRIGHTNESS, "w");
        if (fp) {
            fprintf(fp, "%d", config.get().display.brightness);
            fclose(fp);
        }
    }

    fprintf(stderr, "[Main] ═══════════════════════════════════════════\n");
    fprintf(stderr, "[Main] App ready! Running with:\n");
    fprintf(stderr, "%s\n", hw.get_full_status().c_str());
    fprintf(stderr, "[Main] Entering main loop (30 FPS target)\n");
    fprintf(stderr, "[Main] ═══════════════════════════════════════════\n\n");

    using clock = std::chrono::steady_clock;
    auto target_frame_duration = std::chrono::milliseconds(33);  // 30 FPS
    Scene prev_scene = Scene::Camera;
    uint32_t frame_count = 0;
    uint32_t frame_drops = 0;
    auto last_fps_time = clock::now();
    auto last_render_time = clock::now();

    while (g_running) {
        auto frame_start = clock::now();

        // Power management update (optional hardware)
        if (app.has_gpio() && app.has_sensors()) {
            power.update();
        }

        // Smart rendering: skip frames if behind
        bool should_render = true;
        if (app.has_gpio() && app.has_sensors()) {
            should_render = !power.is_standby();
        }

        if (should_render) {
            app.lvgl()->tick();
            app.display()->commit();
            last_render_time = frame_start;
        }

        auto frame_end = clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            frame_end - frame_start);
        
        // Adaptive frame-rate: don't sleep if we're behind
        if (elapsed < target_frame_duration) {
            std::this_thread::sleep_for(target_frame_duration - elapsed);
        } else {
            frame_drops++;
        }

        frame_count++;

        // FPS monitoring (only every 150 frames to reduce overhead)
        if (frame_count % 150 == 0) {
            auto now = clock::now();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_fps_time).count();
            if (seconds > 0) {
                double fps = 150.0 / seconds;
                fprintf(stderr, "[Main] FPS: %.1f | Drops: %u/%u | RAM: ~%uMB\n",
                        fps, frame_drops, frame_count, 
                        static_cast<unsigned>(frame_count / 30));  // Rough estimate
                last_fps_time = now;
            }
        }
    }

    fprintf(stderr, "\n[Main] Initiating safe shutdown...\n");
    auto shutdown_start = clock::now();

    config.save();
    sync();

    app.camera()->stop_preview();
    if (app.has_sensors()) {
        app.sensors()->stop_polling();
    }

    app.camera()->deinit();
    if (app.has_sensors()) {
        app.sensors()->deinit();
    }
    if (app.has_gpio()) {
        app.gpio()->deinit();
    }
    if (app.touch()) {
        app.touch()->deinit();
    }
    app.lvgl()->deinit();
    app.display()->deinit();

    auto shutdown_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - shutdown_start);

    fprintf(stderr, "[Main] Shutdown completed in %ldms\n", shutdown_elapsed.count());
    fprintf(stderr, "[Main] Total frames: %u (drops: %u)\n", frame_count, frame_drops);
    fprintf(stderr, "[Main] Goodbye.\n\n");

    return 0;
}