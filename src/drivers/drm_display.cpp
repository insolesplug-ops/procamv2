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
#include <algorithm>
#include <unordered_map>
#include <vector>
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
static std::unordered_map<int, std::pair<void*, size_t>> s_dmabuf_maps;
static std::vector<int> s_x_map;
static std::vector<int> s_y_map;
static int s_map_src_w = 0;
static int s_map_src_h = 0;
static int s_map_dst_w = 0;
static int s_map_dst_h = 0;
static int s_map_off_x = 0;
static int s_map_off_y = 0;

static void set_plane_zpos_if_available(int drm_fd, uint32_t plane_id, uint64_t zpos) {
    drmModeObjectProperties* props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return;

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes* prop = drmModeGetProperty(drm_fd, props->props[i]);
        if (!prop) continue;

        if (strcmp(prop->name, "zpos") == 0) {
            drmModeObjectSetProperty(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, prop->prop_id, zpos);
            drmModeFreeProperty(prop);
            break;
        }

        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);
}

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

    for (auto& kv : s_dmabuf_maps) {
        if (kv.second.first && kv.second.first != MAP_FAILED) {
            munmap(kv.second.first, kv.second.second);
        }
    }
    s_dmabuf_maps.clear();

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

    // Always use software compositing for UI on top of primary framebuffer.
    // This avoids plane ordering/alpha quirks on some Pi KMS stacks.
    ui_plane_.id = 0;
    fprintf(stderr, "[DRM] UI software composition enabled\n");

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

    if (!camera_plane_.map) {
        fprintf(stderr, "[DRM] camera_plane_ map is null\n");
        return false;
    }

    // DEBUG: Log first camera frame
    static bool first_frame = true;
    if (first_frame) {
        fprintf(stderr, "[DRM] First camera frame: %dx%d stride=%d fmt=0x%x fd=%d\n",
                width, height, stride, format, dmabuf_fd);
        first_frame = false;
    }

    // Robust path: map camera dmabuf and scale into full primary framebuffer.
    const size_t src_size = static_cast<size_t>(stride) * static_cast<size_t>(height);
    void* src_map = nullptr;

    auto map_it = s_dmabuf_maps.find(dmabuf_fd);
    if (map_it == s_dmabuf_maps.end()) {
        src_map = mmap(nullptr, src_size, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
        if (src_map == MAP_FAILED) {
            fprintf(stderr, "[DRM] mmap(dmabuf) failed: %s\n", strerror(errno));
            return false;
        }
        s_dmabuf_maps.emplace(dmabuf_fd, std::make_pair(src_map, src_size));
    } else {
        src_map = map_it->second.first;
    }

    const int dst_w = camera_plane_.width;
    const int dst_h = camera_plane_.height;
    if (width <= 0 || height <= 0 || dst_w <= 0 || dst_h <= 0) {
        return false;
    }

    // Contain scaling (no excessive zoom): fit whole camera image into output.
    // Clear full frame first so no stale terminal/ghost pixels remain.
    for (int y = 0; y < dst_h; y++) {
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(camera_plane_.map + y * camera_plane_.pitch);
        for (int x = 0; x < dst_w; x++) {
            dst_row[x] = 0xFF000000;
        }
    }

    const float scale_x = static_cast<float>(dst_w) / static_cast<float>(width);
    const float scale_y = static_cast<float>(dst_h) / static_cast<float>(height);
    const float scale = std::min(scale_x, scale_y);

    const int out_w = std::max(1, static_cast<int>(width * scale));
    const int out_h = std::max(1, static_cast<int>(height * scale));
    const int dst_off_x = (dst_w - out_w) / 2;
    const int dst_off_y = (dst_h - out_h) / 2;

    if (s_map_src_w != width || s_map_src_h != height ||
        s_map_dst_w != out_w || s_map_dst_h != out_h ||
        s_map_off_x != dst_off_x || s_map_off_y != dst_off_y) {
        s_x_map.assign(out_w, 0);
        s_y_map.assign(out_h, 0);
        for (int x = 0; x < out_w; x++) {
            s_x_map[x] = std::min(width - 1, (x * width) / out_w);
        }
        for (int y = 0; y < out_h; y++) {
            s_y_map[y] = std::min(height - 1, (y * height) / out_h);
        }
        s_map_src_w = width;
        s_map_src_h = height;
        s_map_dst_w = out_w;
        s_map_dst_h = out_h;
        s_map_off_x = dst_off_x;
        s_map_off_y = dst_off_y;
    }

    for (int y = 0; y < out_h; y++) {
        const int src_y = s_y_map[y];
        const uint8_t* src_row = reinterpret_cast<const uint8_t*>(src_map) + src_y * stride;
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(camera_plane_.map + (dst_off_y + y) * camera_plane_.pitch) + dst_off_x;

        for (int x = 0; x < out_w; x++) {
            const int src_x = s_x_map[x];
            const uint8_t* p = src_row + src_x * 3;
            const uint8_t r = p[0];
            const uint8_t g = p[1];
            const uint8_t b = p[2];
            dst_row[x] = (0xFFu << 24) | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }

    return true;
}

bool DrmDisplay::commit() {
    if (drm_fd_ < 0) return false;

    // DEBUG: Log first commit
    static bool first_commit = true;
    if (first_commit) {
        fprintf(stderr, "[DRM] First commit() - ui_plane_.id=%u\n", ui_plane_.id);
        first_commit = false;
    }

    // Software-composite UI buffer over primary camera buffer.
    if (camera_plane_.map && ui_plane_.map) {
        const int dst_w = camera_plane_.width;
        const int dst_h = camera_plane_.height;
        const int ui_w = DISPLAY_W;
        const int ui_h = DISPLAY_H;

        // Rotate portrait UI (480x800) into landscape display (800x480-ish)
        // and center it in the physical primary framebuffer.
        const int rot_w = ui_h; // 800
        const int rot_h = ui_w; // 480
        const int off_x = (dst_w - rot_w) / 2;
        const int off_y = (dst_h - rot_h) / 2;

        for (int y = 0; y < rot_h; y++) {
            uint32_t* dst_row = reinterpret_cast<uint32_t*>(camera_plane_.map + (off_y + y) * camera_plane_.pitch) + off_x;

            for (int x = 0; x < rot_w; x++) {
                const int src_x = y;
                const int src_y = ui_h - 1 - x;
                const uint32_t src = reinterpret_cast<const uint32_t*>(ui_plane_.map + src_y * ui_plane_.pitch)[src_x];

                const uint8_t sa = static_cast<uint8_t>(src >> 24);
                if (sa == 0) continue;

                const uint8_t sr = static_cast<uint8_t>((src >> 16) & 0xFF);
                const uint8_t sg = static_cast<uint8_t>((src >> 8) & 0xFF);
                const uint8_t sb = static_cast<uint8_t>(src & 0xFF);

                uint32_t& dst = dst_row[x];
                const uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xFF);
                const uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xFF);
                const uint8_t db = static_cast<uint8_t>(dst & 0xFF);

                const uint8_t out_r = static_cast<uint8_t>((sr * sa + dr * (255 - sa)) / 255);
                const uint8_t out_g = static_cast<uint8_t>((sg * sa + dg * (255 - sa)) / 255);
                const uint8_t out_b = static_cast<uint8_t>((sb * sa + db * (255 - sa)) / 255);
                dst = (0xFFu << 24) | (static_cast<uint32_t>(out_r) << 16)
                    | (static_cast<uint32_t>(out_g) << 8) | static_cast<uint32_t>(out_b);
            }
        }
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
