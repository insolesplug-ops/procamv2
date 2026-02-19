#pragma once
/**
 * CinePi Camera - DRM/KMS Display Driver
 *
 * Zero-copy dual-plane architecture:
 *   PRIMARY  plane (z=0)  : libcamera DMA-BUF → DRM FB import, HW scaler.
 *   OVERLAY  plane (z=10) : LVGL ARGB8888 dumb buffer, double-buffered.
 */

#include <cstdint>
#include <vector>

namespace cinepi {

// ── UI overlay dumb buffer (double-buffered, one instance per slot) ──────────
struct UiBuf {
    uint32_t fb_id      = 0;
    uint32_t gem_handle = 0;
    uint32_t pitch      = 0;   // kernel-supplied, use verbatim in AddFB2
    uint64_t size       = 0;
    uint8_t *map        = nullptr;
};

// ── DMA-BUF import cache entry ──────────────────────────────────────────────
struct CamFbEntry {
    int      dmabuf_fd  = -1;   // key: libcamera DMA-BUF fd
    uint32_t gem_handle = 0;
    uint32_t fb_id      = 0;
};

class DrmDisplay {
public:
    DrmDisplay();
    ~DrmDisplay();

    bool     init();
    void     deinit();

    // LVGL writes here (always the back buffer).
    uint8_t *get_ui_buffer();
    int      get_ui_pitch() const;

    // Zero-copy camera presentation.  Imports the DMA-BUF once, then calls
    // drmModeSetPlane so the display HW scaler fills the full screen.
    bool set_camera_dmabuf(int dmabuf_fd, int width, int height,
                           int stride, uint32_t drm_fourcc);

    // Flip the UI double buffer (call once per LVGL vsync tick).
    bool commit();

    // Backlight.
    void set_blank(bool blank);

    int get_drm_fd()  const { return drm_fd_; }
    int get_mode_w()  const { return mode_w_; }
    int get_mode_h()  const { return mode_h_; }

private:
    bool     find_crtc();
    bool     alloc_ui_bufs();
    void     discover_overlay_plane();
    uint32_t find_plane_type(uint32_t drm_plane_type);
    CamFbEntry *get_or_import(int fd, int w, int h, int stride, uint32_t fourcc);
    bool     create_dumb(UiBuf &b, int w, int h, int bpp);
    void     destroy_dumb(UiBuf &b);

    int      drm_fd_        = -1;
    uint32_t connector_id_  = 0;
    uint32_t crtc_id_       = 0;
    uint32_t encoder_id_    = 0;
    int      crtc_idx_      = -1;

    // Live mode dimensions (read from connector at init time).
    int      mode_w_        = 0;
    int      mode_h_        = 0;

    // Discovered DRM plane IDs (0 = not found).
    uint32_t camera_plane_id_ = 0;
    uint32_t ui_plane_id_     = 0;

    // Seed/blank FB to establish the mode via drmModeSetCrtc.
    uint32_t blank_fb_id_   = 0;
    uint32_t blank_gem_     = 0;

    // DMA-BUF → DRM FB registration cache.
    std::vector<CamFbEntry> cam_fb_cache_;

    // UI overlay double buffers.  back_idx_ is the one LVGL draws into next.
    UiBuf    ui_bufs_[2];
    int      back_idx_      = 0;

    bool     initialized_   = false;
};

} // namespace cinepi
