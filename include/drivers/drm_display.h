#pragma once
/**
 * CinePi Camera - DRM/KMS Display Driver
 * Zero-copy dual-plane compositing:
 *   Plane 0: Camera preview (RGB565/RGB888)
 *   Plane 1: LVGL UI overlay (ARGB8888)
 */

#include <cstdint>
#include <functional>

struct _drmModePlane;

namespace cinepi {

struct DrmPlane {
    uint32_t id         = 0;
    uint32_t fb_id      = 0;
    int      dumb_fd    = -1;     // GEM handle for dumb buffer
    uint32_t gem_handle = 0;
    uint32_t pitch      = 0;
    uint64_t size       = 0;
    uint8_t* map        = nullptr;
    int      width      = 0;
    int      height     = 0;
};

class DrmDisplay {
public:
    DrmDisplay();
    ~DrmDisplay();

    bool init();
    void deinit();

    // Get the UI overlay framebuffer pointer (ARGB8888, 480x800)
    uint8_t* get_ui_buffer();
    int      get_ui_pitch() const;

    // Import a DMA-BUF fd as the camera plane (zero-copy)
    bool set_camera_dmabuf(int dmabuf_fd, int width, int height, int stride, uint32_t format);

    // Commit both planes atomically
    bool commit();

    // Blank/unblank display
    void set_blank(bool blank);

    int get_drm_fd() const { return drm_fd_; }

private:
    bool find_crtc();
    bool setup_ui_plane();
    uint32_t find_plane_for_layer(int layer_idx);
    bool create_dumb_buffer(DrmPlane& plane, int w, int h, int bpp);
    void destroy_dumb_buffer(DrmPlane& plane);

    int      drm_fd_        = -1;
    uint32_t connector_id_  = 0;
    uint32_t crtc_id_       = 0;
    uint32_t encoder_id_    = 0;
    int      crtc_idx_      = -1;
    uint32_t mode_blob_id_  = 0;

    // Plane 0: Camera stream
    DrmPlane camera_plane_;
    uint32_t camera_fb_id_  = 0;   // from imported dmabuf

    // Plane 1: UI overlay
    DrmPlane ui_plane_;

    bool initialized_       = false;
};

} // namespace cinepi
