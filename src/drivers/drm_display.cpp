/**
 * CinePi Camera - DRM/KMS Display Driver
 * Dual-plane compositing for Waveshare 4.3" DSI LCD
 *
 * Plane 0 (primary): Camera preview via DMA-BUF import (zero-copy)
 * Plane 1 (overlay): LVGL UI framebuffer (ARGB8888 with alpha)
 */

#include "drivers/drm_display.h"
#include "core/constants.h"
#include "core/config.h"

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

namespace cinepi {

// Track the GEM handle imported from DMA-BUF separately from dumb buffer handles.
// This is file-scoped because the header does not expose it.
static uint32_t s_camera_imported_gem_handle = 0;

DrmDisplay::DrmDisplay() = default;

DrmDisplay::~DrmDisplay() {
    deinit();
}

bool DrmDisplay::init() {
    // Open DRM device - try DSI first, then card0
    const char* dev_paths[] = {"/dev/dri/card1", "/dev/dri/card0"};
    for (auto path : dev_paths) {
        drm_fd_ = open(path, O_RDWR | O_CLOEXEC);
        if (drm_fd_ >= 0) {
            fprintf(stderr, "[DRM] Opened %s (fd=%d)\n", path, drm_fd_);
            break;
        }
    }
    if (drm_fd_ < 0) {
        fprintf(stderr, "[DRM] Failed to open DRM device: %s\n", strerror(errno));
        return false;
    }

    // Enable universal planes and atomic modesetting
    if (drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        fprintf(stderr, "[DRM] Warning: universal planes not supported\n");
    }
    if (drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        fprintf(stderr, "[DRM] Warning: atomic modesetting not supported, using legacy\n");
    }

    if (!find_crtc()) {
        fprintf(stderr, "[DRM] Failed to find CRTC\n");
        return false;
    }

    if (!setup_ui_plane()) {
        fprintf(stderr, "[DRM] Failed to setup UI overlay plane\n");
        return false;
    }

    initialized_ = true;
    fprintf(stderr, "[DRM] Display initialized: %dx%d\n", DISPLAY_W, DISPLAY_H);
    return true;
}

void DrmDisplay::deinit() {
    if (drm_fd_ < 0) return;

    destroy_dumb_buffer(ui_plane_);
    destroy_dumb_buffer(camera_plane_);

    // Close any imported GEM handle from DMA-BUF
    if (s_camera_imported_gem_handle) {
        struct drm_gem_close gem_close = {};
        gem_close.handle = s_camera_imported_gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
        s_camera_imported_gem_handle = 0;
    }

    if (camera_fb_id_) {
        drmModeRmFB(drm_fd_, camera_fb_id_);
        camera_fb_id_ = 0;
    }

    close(drm_fd_);
    drm_fd_ = -1;
    initialized_ = false;
}

bool DrmDisplay::find_crtc() {
    drmModeRes* res = drmModeGetResources(drm_fd_);
    if (!res) {
        fprintf(stderr, "[DRM] drmModeGetResources failed\n");
        return false;
    }

    // Find the first connected connector (DSI display)
    drmModeConnector* conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_id_ = conn->connector_id;
            break;
        }
        if (conn) {
            drmModeFreeConnector(conn);
            conn = nullptr;
        }
    }

    if (!conn) {
        fprintf(stderr, "[DRM] No connected display found\n");
        drmModeFreeResources(res);
        return false;
    }

    fprintf(stderr, "[DRM] Connector %u: %s, %d modes\n",
            conn->connector_id,
            conn->connector_type == DRM_MODE_CONNECTOR_DSI ? "DSI" : "other",
            conn->count_modes);

    // Use the preferred mode or the first one
    drmModeModeInfo mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];
            break;
        }
    }
    fprintf(stderr, "[DRM] Mode: %dx%d @ %dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);

    // Find encoder -> CRTC
    if (conn->encoder_id) {
        drmModeEncoder* enc = drmModeGetEncoder(drm_fd_, conn->encoder_id);
        if (enc) {
            crtc_id_ = enc->crtc_id;
            encoder_id_ = enc->encoder_id;
            drmModeFreeEncoder(enc);
        }
    }

    // If no CRTC found via encoder, try all encoders
    if (!crtc_id_) {
        for (int i = 0; i < conn->count_encoders; i++) {
            drmModeEncoder* enc = drmModeGetEncoder(drm_fd_, conn->encoders[i]);
            if (!enc) continue;
            for (int j = 0; j < res->count_crtcs; j++) {
                if (enc->possible_crtcs & (1u << j)) {
                    crtc_id_ = res->crtcs[j];
                    encoder_id_ = enc->encoder_id;
                    drmModeFreeEncoder(enc);
                    goto crtc_found;
                }
            }
            drmModeFreeEncoder(enc);
        }
    }

crtc_found:
    if (!crtc_id_) {
        fprintf(stderr, "[DRM] No CRTC available\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return false;
    }

    // Find CRTC index
    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] == crtc_id_) {
            crtc_idx_ = i;
            break;
        }
    }

    // Set mode on CRTC using legacy API as initial setup
    // Primary framebuffer MUST match the mode dimensions (landscape)
    // even though we'll display portrait via plane rotation
    DrmPlane primary;
    if (!create_dumb_buffer(primary, mode.hdisplay, mode.vdisplay, 32)) {
        fprintf(stderr, "[DRM] Failed to create primary dumb buffer\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return false;
    }

    // Clear to black
    memset(primary.map, 0, primary.size);

    if (drmModeSetCrtc(drm_fd_, crtc_id_, primary.fb_id, 0, 0,
                        &connector_id_, 1, &mode) != 0) {
        fprintf(stderr, "[DRM] drmModeSetCrtc failed: %s\n", strerror(errno));
        destroy_dumb_buffer(primary);
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return false;
    }

    // Keep primary buffer for now (camera plane will replace it)
    camera_plane_ = primary;

    fprintf(stderr, "[DRM] CRTC %u configured (index %d)\n", crtc_id_, crtc_idx_);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return true;
}

uint32_t DrmDisplay::find_plane_for_layer(int layer_idx) {
    drmModePlaneRes* plane_res = drmModeGetPlaneResources(drm_fd_);
    if (!plane_res) return 0;

    int found = 0;
    uint32_t result = 0;

    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlane* plane = drmModeGetPlane(drm_fd_, plane_res->planes[i]);
        if (!plane) continue;

        if (plane->possible_crtcs & (1u << crtc_idx_)) {
            // Check plane type via properties
            drmModeObjectProperties* props = drmModeObjectGetProperties(
                drm_fd_, plane->plane_id, DRM_MODE_OBJECT_PLANE);
            if (props) {
                for (uint32_t j = 0; j < props->count_props; j++) {
                    drmModePropertyRes* prop = drmModeGetProperty(drm_fd_, props->props[j]);
                    if (prop && strcmp(prop->name, "type") == 0) {
                        uint64_t type_val = props->prop_values[j];
                        // layer_idx 0 = primary, 1 = overlay
                        if ((layer_idx == 0 && type_val == DRM_PLANE_TYPE_PRIMARY) ||
                            (layer_idx == 1 && type_val == DRM_PLANE_TYPE_OVERLAY)) {
                            result = plane->plane_id;
                        }
                        drmModeFreeProperty(prop);
                        break;
                    }
                    if (prop) drmModeFreeProperty(prop);
                }
                drmModeFreeObjectProperties(props);
            }
            if (result) {
                drmModeFreePlane(plane);
                break;
            }
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(plane_res);
    return result;
}

bool DrmDisplay::setup_ui_plane() {
    // Create UI overlay buffer: 480x800 ARGB8888
    if (!create_dumb_buffer(ui_plane_, DISPLAY_W, DISPLAY_H, UI_BPP)) {
        fprintf(stderr, "[DRM] Failed to create UI dumb buffer\n");
        return false;
    }

    // Clear to transparent
    memset(ui_plane_.map, 0, ui_plane_.size);

    // Find overlay plane
    uint32_t overlay_plane_id = find_plane_for_layer(1);
    if (overlay_plane_id) {
        ui_plane_.id = overlay_plane_id;
        fprintf(stderr, "[DRM] UI overlay plane: %u\n", overlay_plane_id);

        // Set overlay plane - use physical display dimensions
        // (rotation handled by display_lcd_rotate=1 in config.txt)
        if (drmModeSetPlane(drm_fd_, overlay_plane_id, crtc_id_,
                            ui_plane_.fb_id, 0,
                            0, 0, DISPLAY_PHYS_W, DISPLAY_PHYS_H,    // dst: physical screen
                            0, 0, DISPLAY_W << 16, DISPLAY_H << 16   // src: portrait buffer
                           ) != 0) {
            fprintf(stderr, "[DRM] drmModeSetPlane for UI failed: %s (non-fatal, using primary)\n",
                    strerror(errno));
            // Fall back to software compositing if overlay plane not available
        }
    } else {
        fprintf(stderr, "[DRM] No overlay plane found, using software compositing\n");
    }

    return true;
}

uint8_t* DrmDisplay::get_ui_buffer() {
    return ui_plane_.map;
}

int DrmDisplay::get_ui_pitch() const {
    return static_cast<int>(ui_plane_.pitch);
}

bool DrmDisplay::set_camera_dmabuf(int dmabuf_fd, int width, int height,
                                     int stride, uint32_t format) {
    if (drm_fd_ < 0) return false;

    // Remove old FB if exists
    if (camera_fb_id_) {
        drmModeRmFB(drm_fd_, camera_fb_id_);
        camera_fb_id_ = 0;
    }

    // Close the previously imported GEM handle to prevent handle leak.
    // Each drmPrimeFDToHandle call creates a new GEM handle that must be
    // explicitly closed when no longer needed.
    if (s_camera_imported_gem_handle) {
        struct drm_gem_close gem_close = {};
        gem_close.handle = s_camera_imported_gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
        s_camera_imported_gem_handle = 0;
    }

    // On the first call, the initial dumb buffer (black screen) is still alive
    // in camera_plane_. Destroy it now since we are switching to imported DMA-BUFs.
    if (camera_plane_.gem_handle) {
        destroy_dumb_buffer(camera_plane_);
    }

    // Import DMA-BUF as a GEM handle
    uint32_t gem_handle = 0;
    if (drmPrimeFDToHandle(drm_fd_, dmabuf_fd, &gem_handle) != 0) {
        fprintf(stderr, "[DRM] drmPrimeFDToHandle failed: %s\n", strerror(errno));
        return false;
    }

    // Track the imported handle so we can close it on the next frame or at deinit
    s_camera_imported_gem_handle = gem_handle;

    // Create framebuffer from GEM handle
    uint32_t handles[4] = {gem_handle, 0, 0, 0};
    uint32_t strides[4] = {static_cast<uint32_t>(stride), 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(drm_fd_, width, height, format,
                       handles, strides, offsets,
                       &camera_fb_id_, 0) != 0) {
        fprintf(stderr, "[DRM] drmModeAddFB2 failed: %s\n", strerror(errno));
        // Clean up the imported handle on failure
        struct drm_gem_close gem_close = {};
        gem_close.handle = gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
        s_camera_imported_gem_handle = 0;
        return false;
    }

    // Set on primary plane - use physical display dimensions
    uint32_t primary_plane_id = find_plane_for_layer(0);
    if (primary_plane_id) {
        drmModeSetPlane(drm_fd_, primary_plane_id, crtc_id_,
                        camera_fb_id_, 0,
                        0, 0, DISPLAY_PHYS_W, DISPLAY_PHYS_H,   // physical screen
                        0, 0, width << 16, height << 16);
    } else {
        // Fallback: set on CRTC directly
        drmModeSetCrtc(drm_fd_, crtc_id_, camera_fb_id_, 0, 0,
                        &connector_id_, 1, nullptr);
    }

    return true;
}

bool DrmDisplay::commit() {
    if (drm_fd_ < 0) return false;

    // For legacy mode, the planes are already set.
    // If overlay plane exists, update it with current UI buffer.
    if (ui_plane_.id) {
        drmModeSetPlane(drm_fd_, ui_plane_.id, crtc_id_,
                        ui_plane_.fb_id, 0,
                        0, 0, DISPLAY_PHYS_W, DISPLAY_PHYS_H,   // physical screen
                        0, 0, DISPLAY_W << 16, DISPLAY_H << 16);
    }

    // Page flip would be better but works for our refresh rate
    return true;
}

void DrmDisplay::set_blank(bool blank) {
    // Use backlight sysfs for blanking
    FILE* fp = fopen(BACKLIGHT_POWER, "w");
    if (fp) {
        fprintf(fp, "%d", blank ? 4 : 0);  // 4=off, 0=on
        fclose(fp);
    }
    fp = fopen(BACKLIGHT_BRIGHTNESS, "w");
    if (fp) {
        fprintf(fp, "%d", blank ? 0 : ConfigManager::instance().get().display.brightness);
        fclose(fp);
    }
}

bool DrmDisplay::create_dumb_buffer(DrmPlane& plane, int w, int h, int bpp) {
    struct drm_mode_create_dumb create = {};
    create.width = w;
    create.height = h;
    create.bpp = bpp;

    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "[DRM] Create dumb buffer failed: %s\n", strerror(errno));
        return false;
    }

    plane.gem_handle = create.handle;
    plane.pitch = create.pitch;
    plane.size = create.size;
    plane.width = w;
    plane.height = h;

    // Create framebuffer
    uint32_t fb_format = (bpp == 32) ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_RGB565;
    uint32_t handles[4] = {create.handle, 0, 0, 0};
    uint32_t strides[4] = {create.pitch, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(drm_fd_, w, h, fb_format,
                       handles, strides, offsets,
                       &plane.fb_id, 0) != 0) {
        fprintf(stderr, "[DRM] drmModeAddFB2 failed: %s\n", strerror(errno));
        return false;
    }

    // Memory map
    struct drm_mode_map_dumb map_req = {};
    map_req.handle = create.handle;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map_req) != 0) {
        fprintf(stderr, "[DRM] Map dumb buffer failed: %s\n", strerror(errno));
        return false;
    }

    plane.map = static_cast<uint8_t*>(
        mmap(nullptr, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
             drm_fd_, map_req.offset));
    if (plane.map == MAP_FAILED) {
        plane.map = nullptr;
        fprintf(stderr, "[DRM] mmap failed: %s\n", strerror(errno));
        return false;
    }

    fprintf(stderr, "[DRM] Dumb buffer: %dx%d, %dbpp, pitch=%u, size=%llu\n",
            w, h, bpp, create.pitch, (unsigned long long)create.size);
    return true;
}

void DrmDisplay::destroy_dumb_buffer(DrmPlane& plane) {
    if (plane.map) {
        munmap(plane.map, plane.size);
        plane.map = nullptr;
    }
    if (plane.fb_id) {
        drmModeRmFB(drm_fd_, plane.fb_id);
        plane.fb_id = 0;
    }
    if (plane.gem_handle) {
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = plane.gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        plane.gem_handle = 0;
    }
}

} // namespace cinepi
