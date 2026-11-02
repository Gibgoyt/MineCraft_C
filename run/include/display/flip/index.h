/* See run/src/display/flip/README.md. */
#ifndef MC_RUN_DISPLAY_FLIP_INDEX_H
#define MC_RUN_DISPLAY_FLIP_INDEX_H

#include "display/device/index.h"
#include "display/mode/index.h"

#include <stdint.h>

enum : uint32_t {
    DISPLAY_FLIP_WARMUP_EVENTS = 8u, /* the first events follow the modeset and are not representative */
    DISPLAY_FLIP_BUFFERS = 2u,
    DISPLAY_FLIP_BYTES_PER_PIXEL = 4u,
};

/* One dumb buffer, mapped and registered as a framebuffer. Pixels are XRGB8888, and rows are pitch bytes apart. This is
 * write-combined device memory on most drivers, so it is written and never read. */
typedef struct display_buffer_t {
    uint8_t *owned_map;
    uint64_t map_bytes;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t square_x; /* where the test pattern's square is in this buffer */
    uint32_t square_y;
    uint8_t has_square;
    uint8_t _pad0[3];
} display_buffer_t;
static_assert(sizeof(display_buffer_t) == 48);

/* Flip timing, from page-flip events. Plain counters, read by the one thread that flips. */
typedef struct display_flip_stats_t {
    uint64_t flips_submitted;
    uint64_t events;
    uint64_t unexpected_events; /* a flip event with no flip of ours pending */
    uint64_t intervals;         /* those counted below, which leave out the warm-up */
    uint64_t sum_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t late;          /* intervals over 1.5 periods */
    uint64_t sequence_gaps; /* vblanks that passed with no flip */
} display_flip_stats_t;
static_assert(sizeof(display_flip_stats_t) == 72);

typedef struct display_flip_t {
    display_device_t *borrowed_device;
    const display_mode_t *borrowed_mode;
    const char *failed_call; /* the first call that failed, or nullptr: failures while the console is dark wait here */
    display_buffer_t buffers[DISPLAY_FLIP_BUFFERS];
    display_flip_stats_t stats;
    uint64_t period_ns;
    int64_t last_event_ns;
    int32_t failed_errno; /* 0 for a failure that is not a failed call */
    uint32_t last_sequence;
    uint32_t front;       /* which of buffers is on screen */
    uint8_t flip_pending; /* submitted, with no event yet: the back buffer may still be scanned out */
    uint8_t modeset_done; /* set before the attempt: a failed SETCRTC may still have disturbed the CRTC */
    uint8_t restored;
    uint8_t restore_failed;
    uint8_t suspended;     /* another VT is on screen and we hold no master: the CRTC is not ours to touch */
    uint8_t skip_interval; /* the next event's interval is the time away on another VT, which says nothing */
    uint8_t _pad0[6];
} display_flip_t;
static_assert(sizeof(display_flip_t) == 232);

/* What waiting for a flip found. */
typedef enum display_flip_wait_t : uint8_t {
    DISPLAY_FLIP_WAIT_NOTHING = 0, /* nothing yet; look at your clock and come back */
    DISPLAY_FLIP_WAIT_EVENT = 1,   /* at least one event was read */
    DISPLAY_FLIP_WAIT_FAILED = 2,  /* see failed_call */
} display_flip_wait_t;

/* Creates, maps and registers two buffers of the chosen mode's size. Touches nothing that is on screen. On failure logs
 * why and leaves nothing behind. */
display_status_t display_flip_init(display_flip_t *flip, display_device_t *device, const display_mode_t *mode);

/* Restores the display if that is still owed, then removes the buffers. */
void display_flip_deinit(display_flip_t *flip);

/* KD_GRAPHICS, then shows buffer 0. From here to display_flip_restore the console cannot print, so a failure is recorded
 * in failed_call and failed_errno, for the caller to log afterwards. */
bool display_flip_take_over(display_flip_t *flip);

/* The buffer that is not on screen. With no flip pending it is ours to draw on. */
display_buffer_t *display_flip_back(display_flip_t *flip);

/* Asks for the back buffer to be shown at the next vblank, and for an event when it is. One flip at a time: with one
 * pending this fails, as the kernel would with EBUSY. */
bool display_flip_submit(display_flip_t *flip);

/* Waits up to timeout_ms for the card's events and takes them in. */
display_flip_wait_t display_flip_wait(display_flip_t *flip, int32_t timeout_ms);

/* Takes in a block of bytes read from the card. Stops at anything malformed. now_ns is used where the kernel's own time
 * is not on our clock. Returns how many flip events for our CRTC it found. */
uint32_t display_flip_take_events(display_flip_t *flip, const uint8_t *bytes, uint64_t length, int64_t now_ns);

/* Waits a bounded time for a pending flip to finish, since a CRTC is best changed with none in flight. */
void display_flip_drain(display_flip_t *flip);

/* Gives the display back: the saved CRTC while still master, then DROP_MASTER, then KD_TEXT, which makes fbcon re-apply
 * its mode, and it cannot while another client holds master. Runs once; a second call returns the first answer. Only
 * ioctls and flags, so it is safe in a signal handler: display/vt_guard depends on that. */
bool display_flip_restore(display_flip_t *flip);

/* For a VT switch away: the saved CRTC, so that whatever comes next starts from what it left, then DROP_MASTER. The console
 * stays in KD_GRAPHICS, since that belongs to this VT and not to the one coming up. Drain first. Failures are only reported:
 * the switch must go ahead regardless, because the other VT cannot appear until it does. */
bool display_flip_suspend(display_flip_t *flip);

/* For the VT switch back: SET_MASTER, then shows the front buffer again. false with no failure recorded means master was
 * refused, as it can be while the other VT's client lets go, so try again. A failed modeset is recorded in failed_call. */
bool display_flip_resume(display_flip_t *flip);

/* For a process holding a copy of this flip made before the takeover (display/vt_guard's guardian): marks everything owed,
 * so that display_flip_restore in that process does all of it. What is no longer owed fails harmlessly at the kernel. */
void display_flip_assume_taken(display_flip_t *flip);

/* The test pattern, in pure pixels. */

/* The colour of the still picture at a position: a 2-pixel white border, eight bars, a grey ramp along the bottom. */
uint32_t display_flip_pattern_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/* Fills a whole buffer with the still picture. Slow: for before the loop. */
void display_flip_pattern_fill(display_buffer_t *buffer);

/* Moves the square to where it is in this frame, repainting only the part it left, so that it costs almost nothing per
 * frame. Each buffer remembers where its own square was. */
void display_flip_pattern_frame(display_buffer_t *buffer, uint64_t frame);

#endif
