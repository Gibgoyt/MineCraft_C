/* See README.md. The Owner of the input system: its parts borrow the arena here, and this is the only file in the system
 * that reserves memory. */
#include "input/index.h"

#include "log/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint64_t {
    INPUT_SETUP_BYTES = HEAP_REGION_ALIGNMENT,
};

static void input_log_devices(const input_t *input, const char *directory) {
    const input_discovery_t *const discovery = &input->discovery;
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES; i++) {
        const input_device_t *const device = &discovery->borrowed_devices[i];
        if (device->in_use != 0u) {
            LOG_INFO("input: %s: %s%s%s", device->file_name, device->label,
                     (device->kinds & INPUT_KIND_KEYBOARD) != 0u ? ", keyboard" : "",
                     (device->kinds & INPUT_KIND_MOUSE) != 0u ? ", mouse" : "");
        }
    }
    if (discovery->device_count == 0u) {
        LOG_WARN(
            "input: no keyboard or mouse is usable in %s, which takes the input group or a seat ACL; last error: %s",
            directory, discovery->stats.last_errno != 0 ? strerror(discovery->stats.last_errno) : "none");
    }
    if (discovery->watch_fd < 0) {
        LOG_WARN("input: cannot watch %s for hot-plug (%s); will keep trying", directory,
                 strerror(discovery->stats.last_errno));
    }
}

bool input_init(input_t *input, const char *directory, const input_ops_t *ops, void *context) {
    memset(input, 0, sizeof(*input));
    if (!heap_reserve(INPUT_SETUP_BYTES, &input->owned_region)) {
        return false;
    }
    arena_init(&input->arena_setup, "input_setup", input->owned_region.owned_base, input->owned_region.bytes);
    input_events_init(&input->events, &input->arena_setup, ops, context);
    input_discovery_init(&input->discovery, &input->arena_setup, ops, context, directory);
    input->running = 1u;
    input_log_devices(input, directory);
    return true;
}

void input_deinit(input_t *input) {
    if (input->running == 0u) {
        return;
    }
    input_discovery_deinit(&input->discovery);
    input_events_deinit(&input->events);
    arena_deinit(&input->arena_setup);
    heap_release(&input->owned_region);
    memset(input, 0, sizeof(*input));
}

const input_frame_t *input_poll(input_t *input) {
    input_events_begin_frame(&input->events);
    input_discovery_poll(&input->discovery, &input->events);
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES; i++) {
        input_device_t *const device = &input->discovery.borrowed_devices[i];
        if (device->in_use != 0u && input_events_drain(&input->events, &device->stream) < 0) {
            input_discovery_lost(&input->discovery, &input->events, i);
        }
    }
    return input->events.borrowed_frame;
}

void input_report(const input_t *input) {
    if (input->running == 0u) {
        return;
    }
    const input_discovery_stats_t *const found = &input->discovery.stats;
    const input_events_stats_t *const events = &input->events.stats;
    LOG_INFO("input: %" PRIu64 " events; %" PRIu32 " devices now, %" PRIu64 " opened, %" PRIu64 " removed, %" PRIu64
             " lost, %" PRIu64 " rejected, %" PRIu64 " open failures, %" PRIu64 " left out",
             events->events, input->discovery.device_count, found->opened, found->removed, found->lost, found->rejected,
             found->open_failures, found->table_full);
    if (events->dropped != 0u || events->torn_reads != 0u || events->resync_failures != 0u || events->ignored != 0u) {
        LOG_WARN("input: %" PRIu64 " queue overflows, %" PRIu64 " torn reads, %" PRIu64 " failed resyncs, %" PRIu64
                 " meaningless key events",
                 events->dropped, events->torn_reads, events->resync_failures, events->ignored);
    }
}

static int64_t input_clamp(int64_t value, uint32_t room) {
    if (value < 0) {
        return 0;
    }
    return value > (int64_t)room ? (int64_t)room : value;
}

void input_cursor_move(input_cursor_t *cursor, const input_frame_t *frame, uint32_t room_x, uint32_t room_y) {
    if (cursor->started == 0u) {
        cursor->x = (int64_t)(room_x / 2u);
        cursor->y = (int64_t)(room_y / 2u);
        cursor->started = 1u;
    }
    cursor->x = input_clamp(cursor->x + frame->mouse_dx, room_x);
    cursor->y = input_clamp(cursor->y + frame->mouse_dy, room_y);
}

bool input_test_pattern_cursor(void *context, uint32_t room_x, uint32_t room_y, uint32_t *out_x, uint32_t *out_y,
                               bool *out_active) {
    input_t *const input = context;
    const input_frame_t *const frame = input_poll(input);
    input_cursor_move(&input->cursor, frame, room_x, room_y);
    *out_x = (uint32_t)input->cursor.x;
    *out_y = (uint32_t)input->cursor.y;
    *out_active = input_keys_any(&frame->down);
    return !input_keys_has(&frame->pressed, KEY_ESC);
}
