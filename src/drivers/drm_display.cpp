/**
 * CinePi Camera - DRM/KMS Display Driver (Zero-Copy, Dual-Plane)
 *
 * PRIMARY plane  z=0  : libcamera DMA-BUF imported once per buffer fd,
 *                       hardware scaler fills the full CRTC area.
 *                       Zero memcpy, zero mmap of camera buffers.
 *
 * OVERLAY plane  z=10 : LVGL UI in an ARGB8888 dumb buffer.
 *                       Double-buffered (front/back) to prevent tearing.
 *                       alpha=0 pixels are transparent → camera shows through.
 *
 * Stride / format discipline:
 *   Camera FB is registered with the EXACT stride reported by libcamera so
 *   the display controller never mis-interprets the line pitch.
 *   UI dumb-buffer pitch is supplied by the kernel (DRM_IOCTL_MODE_CREATE_DUMB)
 *   and passed verbatim to drmModeAddFB2.
 *
 * Runtime mode detection:
 *   mode_w_ / mode_h_ are read from the DRM connector at init time.
 *   This means the code is correct both BEFORE and AFTER the
 *   /boot/config.txt portrait-rotation fix.
 *
 * /boot/config.txt fix (do this once on the Pi):
 *   REMOVE:  display_lcd_rotate=1          ← DispmanX only, breaks KMS
 *   CHANGE:  dtoverlay=vc4-kms-dsi-generic,...
 *   TO:      dtoverlay=vc4-kms-dsi-generic,...,rotate=90
 *   (or add "video=DSI-1:480x800@60,rotate=90" to /boot/cmdline.txt)
 *   After this the KMS connector will report 480×800 natively and
 *   libcamera Transform::Rot90 is no longer needed either.
 */

#include "drivers/drm_display.h"
#include "core/constants.h"
#include "core/config.h"

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

namespace cinepi {

// ─── internal helper ─────────────────────────────────────────────────────────

static void set_plane_zpos(int fd, uint32_t plane_id, uint64_t zpos)
{
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (p) {
            if (strcmp(p->name, "zpos") == 0)
                drmModeObjectSetProperty(fd, plane_id,
                                         DRM_MODE_OBJECT_PLANE,
                                         p->prop_id, zpos);
            drmModeFreeProperty(p);
        }
    }
    drmModeFreeObjectProperties(props);
}

// ─── ctor / dtor ─────────────────────────────────────────────────────────────

DrmDisplay::DrmDisplay()  = default;
DrmDisplay::~DrmDisplay() { deinit(); }

// ─── public: init / deinit ───────────────────────────────────────────────────

bool DrmDisplay::init()
{
    // Prefer card1 (DSI), fall back to card0
    const char *dev_paths[] = { "/dev/dri/card1", "/dev/dri/card0" };
    for (auto path : dev_paths) {
        drm_fd_ = open(path, O_RDWR | O_CLOEXEC);
        if (drm_fd_ >= 0) {
            fprintf(stderr, "[DRM] opened %s (fd=%d)\n", path, drm_fd_);
            break;
        }
    }
    if (drm_fd_ < 0) {
        fprintf(stderr, "[DRM] cannot open DRM device: %s\n", strerror(errno));
        return false;
    }

    // Universal planes gives access to the PRIMARY plane via drmModeSetPlane
    if (drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
        fprintf(stderr, "[DRM] WARNING: universal planes not available\n");

    if (!find_crtc())      return false;   // sets mode_w_, mode_h_
    if (!alloc_ui_bufs())  return false;   // double-buffered ARGB overlay
    discover_overlay_plane();              // ui_plane_id_

    initialized_ = true;
    fprintf(stderr,
            "[DRM] ready – mode %dx%d  cam_plane=%u  ui_plane=%u\n",
            mode_w_, mode_h_, camera_plane_id_, ui_plane_id_);
    return true;
}

void DrmDisplay::deinit()
{
    if (drm_fd_ < 0) return;

    // Release camera FB cache
    for (auto &e : cam_fb_cache_) {
        if (e.fb_id)      drmModeRmFB(drm_fd_, e.fb_id);
        if (e.gem_handle) {
            drm_gem_close gc{}; gc.handle = e.gem_handle;
            drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gc);
        }
    }
    cam_fb_cache_.clear();

    // Release UI double buffers
    for (auto &b : ui_bufs_) destroy_dumb(b);

    // Release blank seed buffer
    if (blank_fb_id_) { drmModeRmFB(drm_fd_, blank_fb_id_); blank_fb_id_ = 0; }
    if (blank_gem_)   {
        drm_mode_destroy_dumb dd{}; dd.handle = blank_gem_;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        blank_gem_ = 0;
    }

    close(drm_fd_);
    drm_fd_ = -1;
    initialized_ = false;
}

// ─── public: UI buffer access ─────────────────────────────────────────────────
// LVGL always writes into the BACK buffer.  commit() flips front/back.

uint8_t *DrmDisplay::get_ui_buffer()
{
    return ui_bufs_[back_idx_].map;
}

int DrmDisplay::get_ui_pitch() const
{
    return static_cast<int>(ui_bufs_[back_idx_].pitch);
}

// ─── public: camera plane (zero-copy) ────────────────────────────────────────

bool DrmDisplay::set_camera_dmabuf(int dmabuf_fd, int width, int height,
                                    int stride, uint32_t drm_fourcc)
{
    if (drm_fd_ < 0 || !camera_plane_id_) return false;

    CamFbEntry *e = get_or_import(dmabuf_fd, width, height, stride, drm_fourcc);
    if (!e) return false;

    // Hardware scaler: camera dimensions → full CRTC area.  Zero CPU cost.
    int ret = drmModeSetPlane(drm_fd_, camera_plane_id_, crtc_id_,
                               e->fb_id, 0,
                               0, 0, mode_w_, mode_h_,         // dst
                               0, 0, width << 16, height << 16); // src (16.16 fixed)
    if (ret != 0) {
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[DRM] SetPlane(camera) failed: %s\n", strerror(errno));
            warned = true;
        }
        return false;
    }
    return true;
}

// ─── public: commit (flip UI overlay) ────────────────────────────────────────
// Presents the BACK buffer and makes the old FRONT the new BACK.

bool DrmDisplay::commit()
{
    if (drm_fd_ < 0 || !ui_plane_id_) return true;

    UiBuf &front = ui_bufs_[back_idx_];

    // src: full logical DISPLAY_W × DISPLAY_H canvas
    // dst: full CRTC scanout area (HW scales if mode != DISPLAY_W×DISPLAY_H)
    int ret = drmModeSetPlane(drm_fd_, ui_plane_id_, crtc_id_,
                               front.fb_id, 0,
                               0, 0, mode_w_,    mode_h_,
                               0, 0, DISPLAY_W << 16, DISPLAY_H << 16);
    if (ret != 0) {
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[DRM] SetPlane(UI) failed: %s\n", strerror(errno));
            warned = true;
        }
        return false;
    }

    // Flip: the just-displayed buffer becomes the new back buffer for LVGL to draw into
    back_idx_ ^= 1;
    return true;
}

// ─── public: backlight ───────────────────────────────────────────────────────

void DrmDisplay::set_blank(bool blank)
{
    FILE *fp = fopen(BACKLIGHT_POWER, "w");
    if (fp) { fprintf(fp, "%d", blank ? 4 : 0); fclose(fp); }

    fp = fopen(BACKLIGHT_BRIGHTNESS, "w");
    if (fp) {
        int bri = blank ? 0 : ConfigManager::instance().get().display.brightness;
        fprintf(fp, "%d", bri);
        fclose(fp);
    }
}

// ─── private: CRTC / mode setup ──────────────────────────────────────────────

bool DrmDisplay::find_crtc()
{
    drmModeRes *res = drmModeGetResources(drm_fd_);
    if (!res) {
        fprintf(stderr, "[DRM] drmModeGetResources failed\n");
        return false;
    }

    // Pick the first connected connector that has at least one mode
    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn         = c;
            connector_id_ = c->connector_id;
        } else if (c) {
            drmModeFreeConnector(c);
        }
    }
    if (!conn) {
        fprintf(stderr, "[DRM] no connected display\n");
        drmModeFreeResources(res);
        return false;
    }

    // Preferred mode first, else index 0
    drmModeModeInfo mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++)
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
            { mode = conn->modes[i]; break; }

    mode_w_ = mode.hdisplay;
    mode_h_ = mode.vdisplay;
    fprintf(stderr, "[DRM] connector %u (%s) → mode %dx%d@%dHz\n",
            conn->connector_id,
            conn->connector_type == DRM_MODE_CONNECTOR_DSI ? "DSI" : "other",
            mode_w_, mode_h_, mode.vrefresh);

    // Resolve CRTC via attached encoder
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(drm_fd_, conn->encoder_id);
        if (enc) {
            crtc_id_    = enc->crtc_id;
            encoder_id_ = enc->encoder_id;
            drmModeFreeEncoder(enc);
        }
    }
    // Fallback: walk all encoders × CRTCs
    if (!crtc_id_) {
        for (int i = 0; i < conn->count_encoders && !crtc_id_; i++) {
            drmModeEncoder *enc = drmModeGetEncoder(drm_fd_, conn->encoders[i]);
            if (!enc) continue;
            for (int j = 0; j < res->count_crtcs; j++) {
                if (enc->possible_crtcs & (1u << j)) {
                    crtc_id_    = res->crtcs[j];
                    encoder_id_ = enc->encoder_id;
                    crtc_idx_   = j;
                    drmModeFreeEncoder(enc);
                    goto crtc_ok;
                }
            }
            drmModeFreeEncoder(enc);
        }
    }
    // Record CRTC index (needed for plane matching)
    for (int i = 0; i < res->count_crtcs; i++)
        if (res->crtcs[i] == crtc_id_) { crtc_idx_ = i; break; }

crtc_ok:
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    if (!crtc_id_) { fprintf(stderr, "[DRM] no CRTC\n"); return false; }
    fprintf(stderr, "[DRM] CRTC %u (idx %d)\n", crtc_id_, crtc_idx_);

    // ── Create a minimal black seed buffer to call drmModeSetCrtc ────────────
    // This establishes the video mode.  The camera DMA-BUF takes over the
    // primary plane from the first live frame onward.

    drm_mode_create_dumb cd{};
    cd.width  = mode_w_;
    cd.height = mode_h_;
    cd.bpp    = 32;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
        fprintf(stderr, "[DRM] CREATE_DUMB(seed) failed: %s\n", strerror(errno));
        return false;
    }
    blank_gem_ = cd.handle;

    // Zero-fill seed buffer
    drm_mode_map_dumb md{}; md.handle = cd.handle;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0) {
        void *p = mmap(nullptr, cd.size, PROT_WRITE, MAP_SHARED, drm_fd_, md.offset);
        if (p != MAP_FAILED) { memset(p, 0, cd.size); munmap(p, cd.size); }
    }

    // Register seed FB (XRGB8888 – alpha channel ignored by primary plane)
    uint32_t h[4] = {cd.handle}, s[4] = {cd.pitch}, o[4] = {0};
    if (drmModeAddFB2(drm_fd_, mode_w_, mode_h_, DRM_FORMAT_XRGB8888,
                       h, s, o, &blank_fb_id_, 0) != 0) {
        fprintf(stderr, "[DRM] AddFB2(seed) failed: %s\n", strerror(errno));
        return false;
    }

    // Set mode
    if (drmModeSetCrtc(drm_fd_, crtc_id_, blank_fb_id_, 0, 0,
                        &connector_id_, 1, &mode) != 0) {
        fprintf(stderr, "[DRM] SetCrtc failed: %s\n", strerror(errno));
        return false;
    }
    fprintf(stderr, "[DRM] mode set OK: %dx%d\n", mode_w_, mode_h_);

    // Discover PRIMARY plane (camera)
    camera_plane_id_ = find_plane_type(DRM_PLANE_TYPE_PRIMARY);
    if (!camera_plane_id_)
        fprintf(stderr, "[DRM] WARNING: no primary plane found\n");
    else {
        set_plane_zpos(drm_fd_, camera_plane_id_, 0);
        fprintf(stderr, "[DRM] camera primary plane: %u\n", camera_plane_id_);
    }
    return true;
}

// ─── private: UI overlay double-buffer allocation ────────────────────────────

bool DrmDisplay::alloc_ui_bufs()
{
    for (int i = 0; i < 2; i++) {
        if (!create_dumb(ui_bufs_[i], DISPLAY_W, DISPLAY_H, UI_BPP)) {
            fprintf(stderr, "[DRM] alloc_ui_bufs: create_dumb[%d] failed\n", i);
            return false;
        }
        memset(ui_bufs_[i].map, 0, ui_bufs_[i].size); // fully transparent
    }
    back_idx_ = 0;
    fprintf(stderr, "[DRM] UI double buffers: ARGB8888 %dx%d pitch=%u\n",
            DISPLAY_W, DISPLAY_H, ui_bufs_[0].pitch);
    return true;
}

// ─── private: overlay plane discovery ────────────────────────────────────────

void DrmDisplay::discover_overlay_plane()
{
    ui_plane_id_ = find_plane_type(DRM_PLANE_TYPE_OVERLAY);
    if (!ui_plane_id_) {
        fprintf(stderr, "[DRM] no overlay plane – UI will not be visible\n");
        return;
    }
    set_plane_zpos(drm_fd_, ui_plane_id_, 10);

    // Initial placement of the back buffer (will be refreshed every commit())
    drmModeSetPlane(drm_fd_, ui_plane_id_, crtc_id_,
                    ui_bufs_[back_idx_].fb_id, 0,
                    0, 0, mode_w_, mode_h_,
                    0, 0, DISPLAY_W << 16, DISPLAY_H << 16);

    fprintf(stderr, "[DRM] UI overlay plane: %u  ARGB8888 %dx%d (double-buffered)\n",
            ui_plane_id_, DISPLAY_W, DISPLAY_H);
}

// ─── private: plane type query ────────────────────────────────────────────────

uint32_t DrmDisplay::find_plane_type(uint32_t wanted_type)
{
    drmModePlaneRes *pr = drmModeGetPlaneResources(drm_fd_);
    if (!pr) return 0;

    uint32_t result = 0;
    for (uint32_t i = 0; i < pr->count_planes && !result; i++) {
        drmModePlane *pl = drmModeGetPlane(drm_fd_, pr->planes[i]);
        if (!pl) continue;

        // Must belong to our CRTC
        if (!(pl->possible_crtcs & (1u << crtc_idx_))) {
            drmModeFreePlane(pl); continue;
        }

        drmModeObjectProperties *props =
            drmModeObjectGetProperties(drm_fd_, pl->plane_id,
                                        DRM_MODE_OBJECT_PLANE);
        if (props) {
            for (uint32_t j = 0; j < props->count_props; j++) {
                drmModePropertyRes *p = drmModeGetProperty(drm_fd_, props->props[j]);
                if (p) {
                    if (strcmp(p->name, "type") == 0 &&
                        props->prop_values[j] == wanted_type)
                        result = pl->plane_id;
                    drmModeFreeProperty(p);
                    if (result) break;
                }
            }
            drmModeFreeObjectProperties(props);
        }
        drmModeFreePlane(pl);
    }
    drmModeFreePlaneResources(pr);
    return result;
}

// ─── private: DMA-BUF import cache ───────────────────────────────────────────
// libcamera DMA-BUF fds are stable for the allocator's lifetime.
// After the first CAMERA_BUF_COUNT frames every call is a cache hit → no
// per-frame kernel ioctl overhead.

DrmDisplay::CamFbEntry *
DrmDisplay::get_or_import(int fd, int w, int h, int stride, uint32_t fourcc)
{
    // Cache lookup
    for (auto &e : cam_fb_cache_)
        if (e.dmabuf_fd == fd) return &e;

    CamFbEntry entry{};
    entry.dmabuf_fd = fd;

    // Import DMA-BUF → GEM handle
    if (drmPrimeFDToHandle(drm_fd_, fd, &entry.gem_handle) != 0) {
        fprintf(stderr, "[DRM] PrimeFDToHandle(fd=%d) failed: %s\n",
                fd, strerror(errno));
        return nullptr;
    }

    // Register DRM FB with the EXACT stride from libcamera.
    // Getting this wrong causes the entire pixel-soup / stride mismatch.
    uint32_t handles[4] = { entry.gem_handle };
    uint32_t strides[4] = { static_cast<uint32_t>(stride) };  // ← exact!
    uint32_t offsets[4] = { 0 };

    if (drmModeAddFB2(drm_fd_, w, h, fourcc,
                       handles, strides, offsets,
                       &entry.fb_id, 0) != 0) {
        fprintf(stderr,
                "[DRM] AddFB2(camera %dx%d stride=%d fmt=0x%08x) failed: %s\n",
                w, h, stride, fourcc, strerror(errno));
        drm_gem_close gc{}; gc.handle = entry.gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_GEM_CLOSE, &gc);
        return nullptr;
    }

    cam_fb_cache_.push_back(entry);
    fprintf(stderr,
            "[DRM] camera FB imported: fd=%d %dx%d stride=%d fmt=0x%08x → fb=%u\n",
            fd, w, h, stride, fourcc, entry.fb_id);
    return &cam_fb_cache_.back();
}

// ─── private: dumb buffer helpers ────────────────────────────────────────────

bool DrmDisplay::create_dumb(UiBuf &b, int w, int h, int bpp)
{
    drm_mode_create_dumb cd{};
    cd.width  = w;
    cd.height = h;
    cd.bpp    = bpp;

    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
        fprintf(stderr, "[DRM] CREATE_DUMB %dx%d@%d failed: %s\n",
                w, h, bpp, strerror(errno));
        return false;
    }
    b.gem_handle = cd.handle;
    b.pitch      = cd.pitch;   // kernel-supplied – use verbatim in AddFB2
    b.size       = cd.size;

    // Register as DRM framebuffer.  ARGB8888 for overlay so alpha is respected.
    uint32_t fmt     = (bpp == 32) ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_RGB565;
    uint32_t h4[4]   = { cd.handle };
    uint32_t s4[4]   = { cd.pitch };   // verbatim kernel pitch → no stride mismatch
    uint32_t o4[4]   = { 0 };

    if (drmModeAddFB2(drm_fd_, w, h, fmt, h4, s4, o4, &b.fb_id, 0) != 0) {
        fprintf(stderr, "[DRM] AddFB2 dumb %dx%d failed: %s\n",
                w, h, strerror(errno));
        return false;
    }

    // Memory-map for CPU writes (LVGL draws here)
    drm_mode_map_dumb md{}; md.handle = cd.handle;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) {
        fprintf(stderr, "[DRM] MAP_DUMB failed: %s\n", strerror(errno));
        return false;
    }
    b.map = static_cast<uint8_t *>(
        mmap(nullptr, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED,
             drm_fd_, md.offset));
    if (b.map == MAP_FAILED) {
        b.map = nullptr;
        fprintf(stderr, "[DRM] mmap dumb failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

void DrmDisplay::destroy_dumb(UiBuf &b)
{
    if (b.map)        { munmap(b.map, b.size); b.map = nullptr; }
    if (b.fb_id)      { drmModeRmFB(drm_fd_, b.fb_id); b.fb_id = 0; }
    if (b.gem_handle) {
        drm_mode_destroy_dumb dd{}; dd.handle = b.gem_handle;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        b.gem_handle = 0;
    }
}

} // namespace cinepi