/* See README.md. */
#include "display/flip/index.h"

#include "log/index.h"
#include "time/index.h"

#include <drm/drm_fourcc.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

enum : uint32_t {
    FLIP_EVENT_BYTES = 1024u,
    PATTERN_BORDER = 2u,
    PATTERN_BARS = 8u,
    PATTERN_SQUARE_MAX = 64u,
    PATTERN_SPEED = 4u, /* pixels per frame */
    PATTERN_SQUARE_COLOUR = 0x00ff8000u,
    PATTERN_WHITE = 0x00ffffffu,
};
enum : int32_t {
    FLIP_DRAIN_POLL_MS = 20,
};
enum : int64_t {
    FLIP_DRAIN_NS = 200000000,
};

static const uint32_t pattern_bars[PATTERN_BARS] = {
    0x00c0c0c0u, 0x00c0c000u, 0x0000c0c0u, 0x0000c000u, 0x00c000c0u, 0x00c00000u, 0x000000c0u, 0x00000000u,
};

static void flip_record_failure(display_flip_t *flip, const char *call, int32_t error) {
    if (flip->failed_call == nullptr) {
        flip->failed_call = call;
        flip->failed_errno = error;
    }
}

/* ---- Buffers ----------------------------------------------------------------------------------------------------- */

static void flip_buffer_destroy(display_flip_t *flip, display_buffer_t *buffer) {
    const display_device_t *const device = flip->borrowed_device;
    if (buffer->fb_id != 0u) {
        uint32_t fb_id = buffer->fb_id;
        const int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_RMFB, &fb_id);
        if (result != 0) {
            LOG_WARN("display: RMFB %" PRIu32 ": %s", buffer->fb_id, strerror(-result));
        }
    }
    if (buffer->owned_map != nullptr) {
        display_device_unmap(device, buffer->owned_map, buffer->map_bytes);
    }
    if (buffer->handle != 0u) {
        struct drm_mode_destroy_dumb destroy = {.handle = buffer->handle};
        const int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        if (result != 0) {
            LOG_WARN("display: DESTROY_DUMB %" PRIu32 ": %s", buffer->handle, strerror(-result));
        }
    }
    memset(buffer, 0, sizeof(*buffer));
}

static bool flip_buffer_create(display_flip_t *flip, display_buffer_t *buffer) {
    const display_device_t *const device = flip->borrowed_device;
    const uint32_t width = flip->borrowed_mode->mode.hdisplay;
    const uint32_t height = flip->borrowed_mode->mode.vdisplay;

    struct drm_mode_create_dumb create = {.height = height, .width = width, .bpp = DISPLAY_FLIP_BYTES_PER_PIXEL * 8u};
    int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (result != 0) {
        LOG_ERROR("display: CREATE_DUMB %" PRIu32 "x%" PRIu32 ": %s", width, height, strerror(-result));
        return false;
    }
    buffer->handle = create.handle;
    buffer->pitch = create.pitch;
    buffer->map_bytes = create.size;
    buffer->width = width;
    buffer->height = height;
    /* Everything that draws trusts these, so the driver's answer is checked before anything is written. */
    if (create.pitch < width * DISPLAY_FLIP_BYTES_PER_PIXEL || create.pitch % DISPLAY_FLIP_BYTES_PER_PIXEL != 0u ||
        create.size < (uint64_t)create.pitch * height) {
        LOG_ERROR("display: CREATE_DUMB answered pitch %" PRIu32 " and size %" PRIu64 " for %" PRIu32 "x%" PRIu32,
                  create.pitch, (uint64_t)create.size, width, height);
        return false;
    }

    struct drm_mode_map_dumb map = {.handle = create.handle};
    result = display_device_ioctl(device, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (result != 0) {
        LOG_ERROR("display: MAP_DUMB: %s", strerror(-result));
        return false;
    }
    result = display_device_map(device, buffer->map_bytes, map.offset, &buffer->owned_map);
    if (result != 0) {
        buffer->owned_map = nullptr;
        LOG_ERROR("display: mapping a dumb buffer: %s", strerror(-result));
        return false;
    }

    struct drm_mode_fb_cmd2 framebuffer = {.width = width, .height = height, .pixel_format = DRM_FORMAT_XRGB8888};
    framebuffer.handles[0] = buffer->handle;
    framebuffer.pitches[0] = buffer->pitch;
    result = display_device_ioctl(device, DRM_IOCTL_MODE_ADDFB2, &framebuffer);
    if (result != 0) {
        LOG_ERROR("display: ADDFB2: %s", strerror(-result));
        return false;
    }
    buffer->fb_id = framebuffer.fb_id;
    return true;
}

display_status_t display_flip_init(display_flip_t *flip, display_device_t *device, const display_mode_t *mode) {
    memset(flip, 0, sizeof(*flip));
    flip->borrowed_device = device;
    flip->borrowed_mode = mode;
    flip->period_ns = (uint64_t)TIME_NS_PER_SECOND * 1000u / mode->refresh_mhz;
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        if (!flip_buffer_create(flip, &flip->buffers[i])) {
            display_flip_deinit(flip);
            return DISPLAY_STATUS_ERROR;
        }
    }
    return DISPLAY_STATUS_OK;
}

void display_flip_deinit(display_flip_t *flip) {
    if (flip->borrowed_device == nullptr) {
        return;
    }
    /* Only owed once the display was taken. A failure at init must keep master for the owner to drop. */
    if (flip->modeset_done != 0u || flip->borrowed_device->console_graphics != 0u) {
        if (!display_flip_restore(flip)) {
            LOG_WARN("display: at least one ioctl failed while giving the display back");
        }
    }
    /* After the restore, so that neither framebuffer is on screen when it goes. */
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        flip_buffer_destroy(flip, &flip->buffers[i]);
    }
    memset(flip, 0, sizeof(*flip));
}

/* ---- Taking and giving back -------------------------------------------------------------------------------------- */

bool display_flip_take_over(display_flip_t *flip) {
    display_device_t *const device = flip->borrowed_device;
    const display_mode_t *const mode = flip->borrowed_mode;
    int32_t result = display_device_console_graphics(device);
    if (result != 0) {
        flip_record_failure(flip, "KDSETMODE(KD_GRAPHICS)", -result);
        return false;
    }
    uint32_t connector_id = mode->connector_id;
    struct drm_mode_crtc crtc = {.crtc_id = mode->crtc_id,
                                 .fb_id = flip->buffers[0].fb_id,
                                 .set_connectors_ptr = (uint64_t)(uintptr_t)&connector_id,
                                 .count_connectors = 1u,
                                 .mode_valid = 1u};
    crtc.mode = mode->mode;
    flip->modeset_done = 1u;
    flip->front = 0u;
    result = display_device_ioctl(device, DRM_IOCTL_MODE_SETCRTC, &crtc);
    if (result != 0) {
        flip_record_failure(flip, "SETCRTC", -result);
        return false;
    }
    flip->last_event_ns = time_now_ns();
    return true;
}

bool display_flip_restore(display_flip_t *flip) {
    if (flip->restored != 0u) {
        return flip->restore_failed == 0u;
    }
    flip->restored = 1u;
    display_device_t *const device = flip->borrowed_device;
    const display_mode_t *const mode = flip->borrowed_mode;
    bool ok = true;
    if (flip->modeset_done != 0u) {
        struct drm_mode_crtc crtc = mode->saved_crtc;
        crtc.set_connectors_ptr = (uint64_t)(uintptr_t)mode->saved_connectors;
        crtc.count_connectors = mode->saved_connector_count;
        if (crtc.mode_valid == 0u || crtc.fb_id == 0u || crtc.count_connectors == 0u) {
            /* It was off, or nothing can be put back: off is what it goes back to, and fbcon sets up its own. */
            crtc.fb_id = 0u;
            crtc.mode_valid = 0u;
            crtc.count_connectors = 0u;
            crtc.set_connectors_ptr = 0u;
        }
        if (display_device_ioctl(device, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0) {
            /* Without the saved framebuffer, turn the CRTC off and let fbcon set it up again. */
            crtc = (struct drm_mode_crtc){.crtc_id = mode->crtc_id};
            (void)display_device_ioctl(device, DRM_IOCTL_MODE_SETCRTC, &crtc);
            ok = false;
        }
    }
    ok = display_device_drop_master(device) == 0 && ok;
    ok = display_device_console_text(device) == 0 && ok;
    flip->restore_failed = (uint8_t)!ok;
    return ok;
}

/* ---- Flipping ---------------------------------------------------------------------------------------------------- */

display_buffer_t *display_flip_back(display_flip_t *flip) {
    return &flip->buffers[flip->front ^ 1u];
}

bool display_flip_submit(display_flip_t *flip) {
    if (flip->flip_pending != 0u) {
        flip_record_failure(flip, "a flip submitted with one still pending", EBUSY);
        return false;
    }
    struct drm_mode_crtc_page_flip request = {.crtc_id = flip->borrowed_mode->crtc_id,
                                              .fb_id = display_flip_back(flip)->fb_id,
                                              .flags = DRM_MODE_PAGE_FLIP_EVENT};
    const int32_t result = display_device_ioctl(flip->borrowed_device, DRM_IOCTL_MODE_PAGE_FLIP, &request);
    if (result != 0) {
        flip_record_failure(flip, "PAGE_FLIP", -result);
        return false;
    }
    flip->stats.flips_submitted++;
    flip->flip_pending = 1u;
    return true;
}

static void flip_record_event(display_flip_t *flip, const struct drm_event_vblank *event, int64_t now_ns) {
    display_flip_stats_t *const stats = &flip->stats;
    if (flip->flip_pending == 0u) {
        stats->unexpected_events++; /* it says nothing about which buffer is on screen */
        return;
    }
    const uint64_t kernel_ns =
        (uint64_t)event->tv_sec * (uint64_t)TIME_NS_PER_SECOND + (uint64_t)event->tv_usec * 1000u;
    const bool use_kernel = flip->borrowed_device->timestamp_monotonic != 0u && kernel_ns != 0u;
    const int64_t at_ns = use_kernel ? (int64_t)kernel_ns : now_ns;
    if (stats->events >= DISPLAY_FLIP_WARMUP_EVENTS) {
        const uint64_t interval = at_ns > flip->last_event_ns ? (uint64_t)(at_ns - flip->last_event_ns) : 0u;
        stats->min_ns = stats->intervals == 0u || interval < stats->min_ns ? interval : stats->min_ns;
        stats->max_ns = interval > stats->max_ns ? interval : stats->max_ns;
        stats->sum_ns += interval;
        stats->late += interval > flip->period_ns + flip->period_ns / 2u ? 1u : 0u;
        stats->intervals++;
        /* Unsigned subtraction: right across a wrap. Some drivers leave the sequence at 0, which says nothing. */
        const bool known = event->sequence != 0u && flip->last_sequence != 0u;
        stats->sequence_gaps += known && event->sequence - flip->last_sequence > 1u ? 1u : 0u;
    }
    flip->last_event_ns = at_ns;
    flip->last_sequence = event->sequence;
    stats->events++;
    flip->flip_pending = 0u;
    flip->front ^= 1u;
}

uint32_t display_flip_take_events(display_flip_t *flip, const uint8_t *bytes, uint64_t length, int64_t now_ns) {
    uint32_t found = 0;
    uint64_t at = 0;
    while (at + sizeof(struct drm_event) <= length) {
        struct drm_event header;
        memcpy(&header, bytes + at, sizeof(header));
        if (header.length < sizeof(header) || at + header.length > length) {
            break; /* a length of 0 would never end, and one past the end cannot be read */
        }
        if (header.type == DRM_EVENT_FLIP_COMPLETE && header.length >= sizeof(struct drm_event_vblank)) {
            struct drm_event_vblank event;
            memcpy(&event, bytes + at, sizeof(event));
            /* crtc_id is 0 from kernels too old to fill it. */
            if (event.crtc_id == 0u || event.crtc_id == flip->borrowed_mode->crtc_id) {
                flip_record_event(flip, &event, now_ns);
                found++;
            }
        }
        at += header.length;
    }
    return found;
}

display_flip_wait_t display_flip_wait(display_flip_t *flip, int32_t timeout_ms) {
    const int32_t ready = display_device_wait_readable(flip->borrowed_device, timeout_ms);
    if (ready < 0) {
        flip_record_failure(flip, "waiting for flip events", -ready);
        return DISPLAY_FLIP_WAIT_FAILED;
    }
    if (ready == 0) {
        return DISPLAY_FLIP_WAIT_NOTHING;
    }
    alignas(8) uint8_t bytes[FLIP_EVENT_BYTES];
    const int64_t count = display_device_read(flip->borrowed_device, bytes, sizeof(bytes));
    if (count < 0) {
        flip_record_failure(flip, "reading flip events", (int32_t)-count);
        return DISPLAY_FLIP_WAIT_FAILED;
    }
    const uint32_t found = display_flip_take_events(flip, bytes, (uint64_t)count, time_now_ns());
    return found != 0u ? DISPLAY_FLIP_WAIT_EVENT : DISPLAY_FLIP_WAIT_NOTHING;
}

void display_flip_drain(display_flip_t *flip) {
    const int64_t started_ns = time_now_ns();
    while (flip->flip_pending != 0u && time_now_ns() - started_ns < FLIP_DRAIN_NS) {
        if (display_flip_wait(flip, FLIP_DRAIN_POLL_MS) == DISPLAY_FLIP_WAIT_FAILED) {
            return;
        }
    }
}

/* ---- The test pattern -------------------------------------------------------------------------------------------- */

uint32_t display_flip_pattern_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (x < PATTERN_BORDER || y < PATTERN_BORDER || x + PATTERN_BORDER >= width || y + PATTERN_BORDER >= height) {
        return PATTERN_WHITE; /* so a cropped or overscanned edge shows */
    }
    if ((uint64_t)y * 4u >= (uint64_t)height * 3u) {
        const uint32_t grey = (uint32_t)((uint64_t)x * 256u / width);
        return grey << 16u | grey << 8u | grey;
    }
    return pattern_bars[(uint64_t)x * PATTERN_BARS / width];
}

static uint32_t *pattern_row(display_buffer_t *buffer, uint32_t y) {
    return (uint32_t *)(void *)(buffer->owned_map + (uint64_t)y * buffer->pitch);
}

void display_flip_pattern_fill(display_buffer_t *buffer) {
    for (uint32_t y = 0; y < buffer->height; y++) {
        uint32_t *const row = pattern_row(buffer, y);
        for (uint32_t x = 0; x < buffer->width; x++) {
            row[x] = display_flip_pattern_pixel(x, y, buffer->width, buffer->height);
        }
    }
    buffer->has_square = 0u;
}

static uint32_t pattern_square_side(const display_buffer_t *buffer) {
    const uint32_t shorter = buffer->width < buffer->height ? buffer->width : buffer->height;
    return shorter / 4u < PATTERN_SQUARE_MAX ? shorter / 4u : PATTERN_SQUARE_MAX;
}

void display_flip_pattern_frame(display_buffer_t *buffer, uint64_t frame) {
    const uint32_t side = pattern_square_side(buffer);
    if (side == 0u) {
        return; /* otherwise side is at most a quarter of the width, so there is always room to travel */
    }
    if (buffer->has_square != 0u) {
        for (uint32_t y = buffer->square_y; y < buffer->square_y + side; y++) {
            uint32_t *const row = pattern_row(buffer, y);
            for (uint32_t x = buffer->square_x; x < buffer->square_x + side; x++) {
                row[x] = display_flip_pattern_pixel(x, y, buffer->width, buffer->height);
            }
        }
    }
    /* Back and forth along the screen, so a dropped frame or a tear shows up as a jump. */
    const uint64_t travel = buffer->width - side;
    const uint64_t distance = frame * PATTERN_SPEED % (travel * 2u);
    const uint32_t new_x = (uint32_t)(distance < travel ? distance : travel * 2u - distance);
    const uint32_t new_y = (buffer->height - side) / 2u;
    for (uint32_t y = new_y; y < new_y + side; y++) {
        uint32_t *const row = pattern_row(buffer, y);
        for (uint32_t x = new_x; x < new_x + side; x++) {
            row[x] = PATTERN_SQUARE_COLOUR;
        }
    }
    buffer->square_x = new_x;
    buffer->square_y = new_y;
    buffer->has_square = 1u;
}
