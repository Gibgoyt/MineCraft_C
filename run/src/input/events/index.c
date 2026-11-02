/* See README.md. Turns raw evdev events into one input_frame_t. Silent: this runs while the console is dark, so whatever
 * goes wrong is counted, for input_report to tell afterwards. */
#include "input/events/index.h"

#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>

enum : uint32_t {
    EVENTS_READ_BATCH = 64u,
    EVENTS_READS_PER_DRAIN = 16u, /* so that a device sending without end cannot hold up a frame */
};
enum : int32_t {
    EVENTS_VALUE_REPEAT = 2,
};
static_assert(sizeof(struct input_event) == 24, "evdev's 64-bit layout: anything else would misread every event");

static void keys_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= 1ull << (code % 64u);
}

static void keys_clear(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] &= ~(1ull << (code % 64u));
}

bool input_keys_has(const input_keys_t *keys, uint32_t code) {
    return code < INPUT_KEY_CODES && (keys->words[code / 64u] >> (code % 64u) & 1u) != 0u;
}

bool input_keys_any(const input_keys_t *keys) {
    uint64_t any = 0;
    for (uint32_t i = 0; i < INPUT_KEY_WORDS; i++) {
        any |= keys->words[i];
    }
    return any != 0u;
}

void input_events_init(input_events_t *events, arena_t *arena, const input_ops_t *ops, void *context) {
    memset(events, 0, sizeof(*events));
    events->borrowed_frame = arena_alloc_zeroed(arena, sizeof(input_frame_t), alignof(input_frame_t));
    events->borrowed_holders = arena_alloc_zeroed(arena, INPUT_KEY_CODES, 1u);
    events->borrowed_ops = ops;
    events->borrowed_context = context;
}

void input_events_deinit(input_events_t *events) {
    memset(events, 0, sizeof(*events));
}

void input_events_begin_frame(input_events_t *events) {
    input_frame_t *const frame = events->borrowed_frame;
    memset(&frame->pressed, 0, sizeof(frame->pressed));
    memset(&frame->released, 0, sizeof(frame->released));
    memset(&frame->repeated, 0, sizeof(frame->repeated));
    frame->mouse_dx = 0;
    frame->mouse_dy = 0;
    frame->wheel = 0;
    frame->wheel_horizontal = 0;
}

void input_events_stream_start(input_stream_t *stream, int32_t fd) {
    memset(stream, 0, sizeof(*stream));
    stream->fd = fd;
}

/* Both directions do nothing if the device is already there, so nothing a device sends can unbalance the holders. */
static void events_key_down(input_events_t *events, input_stream_t *stream, uint32_t code) {
    if (input_keys_has(&stream->keys, code)) {
        return;
    }
    keys_set(&stream->keys, code);
    events->borrowed_holders[code]++;
    if (events->borrowed_holders[code] == 1u) {
        keys_set(&events->borrowed_frame->down, code);
        keys_set(&events->borrowed_frame->pressed, code);
    }
}

static void events_key_up(input_events_t *events, input_stream_t *stream, uint32_t code) {
    if (!input_keys_has(&stream->keys, code)) {
        return;
    }
    keys_clear(&stream->keys, code);
    events->borrowed_holders[code]--;
    if (events->borrowed_holders[code] == 0u) {
        keys_clear(&events->borrowed_frame->down, code);
        keys_set(&events->borrowed_frame->released, code);
    }
}

static void events_take_key(input_events_t *events, input_stream_t *stream, uint32_t code, int32_t value) {
    if (code >= INPUT_KEY_CODES) {
        events->stats.ignored++;
    } else if (value == 0) {
        events_key_up(events, stream, code);
    } else if (value == 1) {
        events_key_down(events, stream, code);
    } else if (value == EVENTS_VALUE_REPEAT) {
        if (input_keys_has(&stream->keys, code)) {
            keys_set(&events->borrowed_frame->repeated, code);
        }
    } else {
        events->stats.ignored++;
    }
}

static void events_take_rel(input_frame_t *frame, uint32_t code, int32_t value) {
    switch (code) {
    case REL_X:
        frame->mouse_dx += value;
        break;
    case REL_Y:
        frame->mouse_dy += value;
        break;
    case REL_WHEEL:
        frame->wheel += value;
        break;
    case REL_HWHEEL:
        frame->wheel_horizontal += value;
        break;
    default:
        break; /* REL_WHEEL_HI_RES and friends repeat what these already said */
    }
}

/* After a drop some transitions are gone for good, so the only truth left is the kernel's own idea of what is down. A
 * failure here is left for the next read to find, if the device has gone. */
static void events_resync(input_events_t *events, input_stream_t *stream) {
    input_keys_t now;
    memset(&now, 0, sizeof(now));
    const int32_t result =
        events->borrowed_ops->ioctl_call(events->borrowed_context, stream->fd, EVIOCGKEY(sizeof(now)), &now);
    if (result < 0) {
        events->stats.resync_failures++;
        return;
    }
    for (uint32_t code = 0; code < INPUT_KEY_CODES; code++) {
        if (input_keys_has(&now, code)) {
            events_key_down(events, stream, code);
        } else {
            events_key_up(events, stream, code);
        }
    }
}

/* The kernel's answer is newer than every event already read, so asking waits until those are taken. Asked earlier, a stale
 * release still in the batch would undo the answer. Owed at the drop as well as at the report, since a report may not come
 * until the device next does something. */
static void events_take_syn(input_events_t *events, input_stream_t *stream, uint32_t code) {
    if (code == SYN_DROPPED) {
        events->stats.dropped++;
        stream->discarding = 1u;
        stream->resync_owed = 1u;
    } else if (code == SYN_REPORT && stream->discarding != 0u) {
        stream->discarding = 0u;
        stream->resync_owed = 1u;
    }
}

static void events_take(input_events_t *events, input_stream_t *stream, const struct input_event *event) {
    events->stats.events++;
    if (event->type == EV_SYN) {
        events_take_syn(events, stream, event->code);
    } else if (stream->discarding != 0u) {
        return;
    } else if (event->type == EV_KEY) {
        events_take_key(events, stream, event->code, event->value);
    } else if (event->type == EV_REL) {
        events_take_rel(events->borrowed_frame, event->code, event->value);
    }
}

int32_t input_events_drain(input_events_t *events, input_stream_t *stream) {
    struct input_event batch[EVENTS_READ_BATCH];
    for (uint32_t read_number = 0; read_number < EVENTS_READS_PER_DRAIN; read_number++) {
        const int64_t got =
            events->borrowed_ops->read_fd(events->borrowed_context, stream->fd, (uint8_t *)batch, sizeof(batch));
        if (got < 0) {
            return (int32_t)got;
        }
        const uint64_t bytes = (uint64_t)got;
        if (bytes % sizeof(batch[0]) != 0u) {
            events->stats.torn_reads++;
        }
        const uint64_t count = bytes / sizeof(batch[0]);
        for (uint64_t i = 0; i < count; i++) {
            events_take(events, stream, &batch[i]);
        }
        if (stream->resync_owed != 0u) {
            stream->resync_owed = 0u;
            events_resync(events, stream);
        }
        if (bytes < sizeof(batch)) {
            break; /* it had no more, so another read would only cost a system call */
        }
    }
    return 0;
}

void input_events_stream_release(input_events_t *events, input_stream_t *stream) {
    for (uint32_t code = 0; code < INPUT_KEY_CODES; code++) {
        events_key_up(events, stream, code);
    }
}
