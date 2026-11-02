/* See run/src/input/events/README.md. */
#ifndef MC_RUN_INPUT_EVENTS_INDEX_H
#define MC_RUN_INPUT_EVENTS_INDEX_H

#include "arena/index.h"
#include "input/ops/index.h"

#include <linux/input-event-codes.h>
#include <stdint.h>

enum : uint32_t {
    INPUT_KEY_CODES = KEY_CNT, /* keys and mouse buttons share these codes: KEY_W, BTN_LEFT */
    INPUT_KEY_WORDS = 12u,
};
static_assert(INPUT_KEY_CODES == INPUT_KEY_WORDS * 64u);

/* One bit per code. */
typedef struct input_keys_t {
    uint64_t words[INPUT_KEY_WORDS];
} input_keys_t;
static_assert(sizeof(input_keys_t) == 96);

/* What one poll found, over all devices. Edges are kept apart from state so that a press and release between two polls is
 * still seen: down says nothing about it, but pressed and released both do. */
typedef struct input_frame_t {
    input_keys_t down;     /* held now, on any device */
    input_keys_t pressed;  /* went from held by no device to held by one */
    input_keys_t released; /* went from held by one device to held by none */
    input_keys_t repeated; /* the kernel's auto-repeat, for typing; it does not change down */
    int64_t mouse_dx;      /* raw counts since the last poll, with no acceleration */
    int64_t mouse_dy;
    int64_t wheel;            /* detents; away from the user is positive */
    int64_t wheel_horizontal; /* detents; right is positive */
} input_frame_t;
static_assert(sizeof(input_frame_t) == 416);

/* One open device's event stream. */
typedef struct input_stream_t {
    input_keys_t keys;   /* what this device holds down, so that it can be let go if the device vanishes */
    int32_t fd;          /* borrowed from input/discovery, which opened it */
    uint8_t discarding;  /* after SYN_DROPPED: everything up to the next SYN_REPORT is unreliable */
    uint8_t resync_owed; /* ask the kernel what is held, once this read's events have been taken */
    uint8_t _pad0[2];
} input_stream_t;
static_assert(sizeof(input_stream_t) == 104);

/* Plain counters, read by the one thread that polls. */
typedef struct input_events_stats_t {
    uint64_t events;          /* every event read, of any type */
    uint64_t ignored;         /* a key event with a code or value that means nothing */
    uint64_t dropped;         /* SYN_DROPPED: the kernel's queue overflowed because we polled too slowly */
    uint64_t torn_reads;      /* a read that ended partway through an event; never expected from evdev */
    uint64_t resync_failures; /* EVIOCGKEY said no after a drop, so a key may be wrong until its next event */
} input_events_stats_t;
static_assert(sizeof(input_events_stats_t) == 40);

typedef struct input_events_t {
    input_frame_t *borrowed_frame;
    uint8_t *borrowed_holders; /* for each code, how many devices hold it down */
    const input_ops_t *borrowed_ops;
    void *borrowed_context;
    input_events_stats_t stats;
} input_events_t;
static_assert(sizeof(input_events_t) == 72);

/* Takes its state from the arena. Nothing here can fail. */
void input_events_init(input_events_t *events, arena_t *arena, const input_ops_t *ops, void *context);
void input_events_deinit(input_events_t *events);

/* Starts a new frame: edges and movement are cleared, and what is held stays held. */
void input_events_begin_frame(input_events_t *events);

/* Starts a stream on an open descriptor, holding nothing. A key that was already down is not reported until it is pressed
 * again, so the Enter that started the program does not count as a press. */
void input_events_stream_start(input_stream_t *stream, int32_t fd);

/* Reads what the device has into the frame, up to a fixed amount per call, so that one device cannot hold up a frame.
 * Returns 0, or -errno when the device can no longer be read, which is how an unplugged device usually first shows up. */
int32_t input_events_drain(input_events_t *events, input_stream_t *stream);

/* Lets go of everything this stream holds, for a device that has gone. */
void input_events_stream_release(input_events_t *events, input_stream_t *stream);

/* Whether code is set. A code out of range is not. */
bool input_keys_has(const input_keys_t *keys, uint32_t code);

/* Whether any code is set. */
bool input_keys_any(const input_keys_t *keys);

#endif
