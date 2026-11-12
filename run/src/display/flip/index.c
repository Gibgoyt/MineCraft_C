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
    PATTERN_CURSOR_SIDE = 24u,
    PATTERN_CURSOR_EDGE = 2u, /* a black edge, so that it shows on every bar */
    PATTERN_CURSOR_COLOUR = 0x00ffffffu,
    PATTERN_CURSOR_ACTIVE_COLOUR = 0x00ff0060u,
    PATTERN_CURSOR_EDGE_COLOUR = 0x00000000u,
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

/* Only ioctls, so safe in a signal handler. */
static bool flip_put_crtc_back(display_flip_t *flip) {
    const display_device_t *const device = flip->borrowed_device;
    const display_mode_t *const mode = flip->borrowed_mode;
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
        return false;
    }
    return true;
}

bool display_flip_suspend(display_flip_t *flip) {
    const bool put_back = flip_put_crtc_back(flip);
    const bool dropped = display_device_drop_master(flip->borrowed_device) == 0;
    flip->suspended = 1u;
    flip->flip_pending = 0u;
    return put_back && dropped;
}

bool display_flip_resume(display_flip_t *flip) {
    display_device_t *const device = flip->borrowed_device;
    const display_mode_t *const mode = flip->borrowed_mode;
    if (display_device_set_master(device) != 0) {
        return false;
    }
    flip->suspended = 0u;
    flip->flip_pending = 0u;
    flip->skip_interval = 1u;
    uint32_t connector_id = mode->connector_id;
    struct drm_mode_crtc crtc = {.crtc_id = mode->crtc_id,
                                 .fb_id = flip->buffers[flip->front].fb_id,
                                 .set_connectors_ptr = (uint64_t)(uintptr_t)&connector_id,
                                 .count_connectors = 1u,
                                 .mode_valid = 1u};
    crtc.mode = mode->mode;
    const int32_t result = display_device_ioctl(device, DRM_IOCTL_MODE_SETCRTC, &crtc);
    if (result != 0) {
        flip_record_failure(flip, "SETCRTC after a VT switch", -result);
        return false;
    }
    flip->last_event_ns = time_now_ns();
    return true;
}

void display_flip_assume_taken(display_flip_t *flip) {
    flip->modeset_done = 1u;
    flip->restored = 0u;
    flip->restore_failed = 0u;
    flip->suspended = 0u;
    flip->borrowed_device->console_graphics = 1u;
    flip->borrowed_device->is_master = 1u;
}

bool display_flip_restore(display_flip_t *flip) {
    if (flip->restored != 0u) {
        return flip->restore_failed == 0u;
    }
    flip->restored = 1u;
    display_device_t *const device = flip->borrowed_device;
    bool ok = true;
    if (flip->modeset_done != 0u && flip->suspended == 0u) {
        ok = flip_put_crtc_back(flip);
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
    const bool async = flip->uncapped != 0u && flip->async_refused == 0u;
    struct drm_mode_crtc_page_flip request = {.crtc_id = flip->borrowed_mode->crtc_id,
                                              .fb_id = display_flip_back(flip)->fb_id,
                                              .flags = DRM_MODE_PAGE_FLIP_EVENT |
                                                       (async ? (uint32_t)DRM_MODE_PAGE_FLIP_ASYNC : 0u)};
    int32_t result = display_device_ioctl(flip->borrowed_device, DRM_IOCTL_MODE_PAGE_FLIP, &request);
    if (async && result == -EINVAL) {
        /* Not every driver and mode can. A picture on the next vblank is still a picture. */
        flip->async_refused = 1u;
        request.flags = DRM_MODE_PAGE_FLIP_EVENT;
        result = display_device_ioctl(flip->borrowed_device, DRM_IOCTL_MODE_PAGE_FLIP, &request);
    }
    if (result != 0) {
        flip_record_failure(flip, "PAGE_FLIP", -result);
        return false;
    }
    flip->stats.flips_submitted++;
    flip->flip_pending = 1u;
    return true;
}

void display_flip_vsync_set(display_flip_t *flip, bool vsync) {
    flip->uncapped = (uint8_t)(vsync ? 0u : 1u);
}

int64_t display_flip_next_ns(const display_flip_t *flip) {
    if (flip->uncapped != 0u || flip->last_event_ns == 0) {
        return 0;
    }
    const uint64_t interval_ns =
        flip->stats.intervals != 0u ? flip->stats.sum_ns / flip->stats.intervals : flip->period_ns;
    return flip->last_event_ns + (int64_t)interval_ns;
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
    if (stats->events >= DISPLAY_FLIP_WARMUP_EVENTS && flip->skip_interval == 0u) {
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
    flip->skip_interval = 0u;
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
    buffer->has_cursor = 0u;
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

uint32_t display_flip_pattern_cursor_side(const display_buffer_t *buffer) {
    const uint32_t shorter = buffer->width < buffer->height ? buffer->width : buffer->height;
    return shorter < PATTERN_CURSOR_SIDE ? shorter : PATTERN_CURSOR_SIDE;
}

void display_flip_pattern_cursor_erase(display_buffer_t *buffer) {
    if (buffer->has_cursor == 0u) {
        return;
    }
    const uint32_t side = display_flip_pattern_cursor_side(buffer);
    for (uint32_t y = buffer->cursor_y; y < buffer->cursor_y + side; y++) {
        uint32_t *const row = pattern_row(buffer, y);
        for (uint32_t x = buffer->cursor_x; x < buffer->cursor_x + side; x++) {
            row[x] = display_flip_pattern_pixel(x, y, buffer->width, buffer->height);
        }
    }
    buffer->has_cursor = 0u;
}

void display_flip_pattern_cursor_draw(display_buffer_t *buffer, uint32_t x, uint32_t y, bool active) {
    const uint32_t side = display_flip_pattern_cursor_side(buffer);
    const uint32_t left = x < buffer->width - side ? x : buffer->width - side;
    const uint32_t top = y < buffer->height - side ? y : buffer->height - side;
    const uint32_t fill = active ? PATTERN_CURSOR_ACTIVE_COLOUR : PATTERN_CURSOR_COLOUR;
    for (uint32_t row_y = 0; row_y < side; row_y++) {
        uint32_t *const row = pattern_row(buffer, top + row_y);
        for (uint32_t column = 0; column < side; column++) {
            const bool edge = column < PATTERN_CURSOR_EDGE || row_y < PATTERN_CURSOR_EDGE ||
                              column + PATTERN_CURSOR_EDGE >= side || row_y + PATTERN_CURSOR_EDGE >= side;
            row[left + column] = edge ? PATTERN_CURSOR_EDGE_COLOUR : fill;
        }
    }
    buffer->cursor_x = left;
    buffer->cursor_y = top;
    buffer->has_cursor = 1u;
}
