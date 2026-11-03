/* See run/src/input/README.md. */
#ifndef MC_RUN_INPUT_INDEX_H
#define MC_RUN_INPUT_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "input/discovery/index.h"
#include "input/events/index.h"
#include "input/ops/index.h"

#include <stdint.h>

/* Where the kernel puts evdev nodes. Only the program passes this: every test has its own. */
#define INPUT_DEVICE_DIRECTORY "/dev/input"

/* A position moved by the mouse and held inside a rectangle. Starts in the middle of it. */
typedef struct input_cursor_t {
    int64_t x;
    int64_t y;
    uint8_t started;
    uint8_t _pad0[7];
} input_cursor_t;
static_assert(sizeof(input_cursor_t) == 24);

/* Zeroed means not running, so _deinit is safe whether or not _init ever ran. */
typedef struct input_t {
    arena_t arena_setup; /* Process lifetime: what the parts ask for at _init */
    heap_region_t owned_region;
    input_discovery_t discovery;
    input_events_t events;
    input_cursor_t cursor; /* for the test pattern, until there is a game to move */
    uint8_t running;
    uint8_t _pad0[7];
} input_t;
static_assert(sizeof(input_t) == 320);
static_assert(alignof(input_t) == 64);

/* Reserves its memory, starts watching directory and opens every keyboard and mouse in it, logging what it found. No
 * devices is not a failure, since one may yet be plugged in. Returns false, leaving nothing behind, only if memory could
 * not be had. ops is input_ops_real() outside tests. Call this before the console goes dark, and only when this process
 * holds the console: see the README. */
bool input_init(input_t *input, const char *directory, const input_ops_t *ops, void *context);

/* Closes every device and releases everything. */
void input_deinit(input_t *input);

/* Takes in hot-plug changes and everything every device has, and returns what they add up to since the last poll. Never
 * blocks, allocates or logs. The frame stays valid until the next poll. */
const input_frame_t *input_poll(input_t *input);

/* Which kinds of device are plugged in as of the last poll: INPUT_KIND_KEYBOARD and INPUT_KIND_MOUSE. A person with no way to play should not be left in a
 * world that goes on without them. */
[[nodiscard]] uint8_t input_kinds_now(const input_t *input);

/* Logs what the polls counted, for after the console is back. Does nothing if input is not running. */
void input_report(const input_t *input);

/* Moves a cursor by a frame's mouse movement, and holds it in 0 to room_x and 0 to room_y. */
void input_cursor_move(input_cursor_t *cursor, const input_frame_t *frame, uint32_t room_x, uint32_t room_y);

/* The test pattern's source for its cursor square, with an input_t * as context: it has the shape of
 * display_cursor_source_t, which this layer does not include. Polls once. Says whether to keep going, which Esc ends; out_active says
 * that a key or button is held, so that a run shows the keyboard working as well as the mouse. */
bool input_test_pattern_cursor(void *context, uint32_t room_x, uint32_t room_y, uint32_t *out_x, uint32_t *out_y,
                               bool *out_active);

/* The test pattern's way to throw away what waited while another VT was on screen, with the shape of
 * display_cursor_discard_t. Reads it all and keeps track of what is held, but neither its presses nor its movement are
 * seen by the next poll. */
void input_test_pattern_discard(void *context);

#endif
