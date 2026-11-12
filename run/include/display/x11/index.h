/* See run/src/display/x11/README.md. */
#ifndef MC_RUN_DISPLAY_X11_INDEX_H
#define MC_RUN_DISPLAY_X11_INDEX_H

#include "arena/index.h"
#include "display/flip/index.h"
#include "display/x11/wire/index.h"

#include <stdint.h>

typedef struct display_x11_config_t {
    uint32_t width; /* the most it will be: the screen may hold it to less */
    uint32_t height;
    uint8_t fullscreen;
    uint8_t _pad0[7];
} display_x11_config_t;
static_assert(sizeof(display_x11_config_t) == 16);

/* What the window's input means, in evdev's terms. display does not include input: main joins the two. */
typedef struct display_x11_sink_t {
    void (*key)(void *context, uint32_t code, int32_t value); /* value is 0 up, 1 down, 2 repeat */
    void (*motion)(void *context, int32_t dx, int32_t dy);
    void (*wheel)(void *context, int32_t vertical, int32_t horizontal);
    void (*release_all)(void *context); /* the window lost the keyboard, so nothing it knows of is held any more */
    void *context;
} display_x11_sink_t;
static_assert(sizeof(display_x11_sink_t) == 40);

/* Plan 008 phase 02: two segments shared with the server, used in turn, so main never waits for a put; and a spare that is never put, which a
 * frame is drawn into when both are still with the server. */
enum : uint32_t {
    DISPLAY_X11_SEGMENTS = 2u,
    DISPLAY_X11_SPARE = 2u, /* buffers' index of the spare */
    DISPLAY_X11_BUFFERS = 3u,
};

typedef struct display_x11_t {
    uint8_t *owned_shm[DISPLAY_X11_BUFFERS]; /* the pixels: the segments shared with the server, then the spare */
    uint8_t *borrowed_inbox;                 /* bytes read from the socket and not yet framed */
    uint8_t *borrowed_events;                /* input packets, in order, until a poll takes them */
    const char *failed_call;                 /* the first thing that failed, or nullptr */
    uint64_t inbox_bytes;
    uint64_t events_lost; /* the queue was full */
    uint64_t frames;
    uint64_t
        focus_losses; /* the window manager took the focus away: Alt+Tab or a click on another window, never a grab */
    uint64_t puts_skipped; /* plan 008 phase 02: frames drawn into the spare, both segments being with the server */
    int64_t put_sent_ns;   /* the last put's send */
    int64_t put_done_ns;   /* plan 008 phase 01: when that put's completion was read; 0 while it is out */
    int64_t wait_from_ns;  /* when display_x11_free_buffer was last called */
    int64_t wait_ns;       /* and what it took: it never waits for a put while a segment is free */
    int64_t segment_sent_ns[DISPLAY_X11_SEGMENTS]; /* each segment's last put */
    display_buffer_t buffers[DISPLAY_X11_BUFFERS]; /* what a frame is drawn into: owned_map is owned_shm */
    wire_pointer_t pointer;
    int32_t fd;
    int32_t shm_id[DISPLAY_X11_BUFFERS];
    int32_t failed_errno;
    uint32_t window;
    uint32_t gc;
    uint32_t segment[DISPLAY_X11_SEGMENTS]; /* each segment's id on the server */
    uint32_t blank_cursor;
    uint32_t wm_protocols;
    uint32_t wm_delete_window;
    uint32_t event_head;
    uint32_t event_count;
    uint32_t polls_until_grab; /* someone else holds the pointer: ask again in a while, not every frame */
    uint8_t reply[WIRE_PACKET_BYTES];
    uint8_t shm_major;
    uint8_t shm_completion; /* the event code of a finished put */
    uint8_t depth;
    uint8_t reply_ready;
    uint8_t
        put_pending[DISPLAY_X11_SEGMENTS]; /* each segment is with the server: put, and no completion read for it yet */
    uint8_t segment_attached[DISPLAY_X11_SEGMENTS];
    uint8_t last_put; /* the segment last put */
    uint8_t focused;
    uint8_t captured;
    uint8_t release_held;    /* a key release was left in the queue for one poll, for the press that may follow it */
    uint8_t close_requested; /* the window's close button */
    uint8_t connected;
    uint8_t running;
    uint8_t _pad0[9];
} display_x11_t;
static_assert(sizeof(display_x11_t) == 448);

/* Connects to the server named by DISPLAY over its local Unix socket, makes the window and its shared pixels, and shows
 * it. display_name is DISPLAY's value and auth_path is the Xauthority file, which may be nullptr. On failure logs why and
 * leaves nothing behind. The arena must hold display_x11_arena_bytes(). */
[[nodiscard]] bool display_x11_init(display_x11_t *x11, arena_t *arena, const display_x11_config_t *config,
                                    const char *display_name, const char *auth_path);
void display_x11_deinit(display_x11_t *x11);
[[nodiscard]] uint64_t display_x11_arena_bytes(void);

/* Reads what has come, never waiting, and keeps the input for the next poll. False once the window is closed or the
 * connection failed: see close_requested and failed_call. */
[[nodiscard]] bool display_x11_service(display_x11_t *x11);

/* Hands the window's input since the last poll to sink. Takes and lets go of the pointer as the window gets and loses focus, and
 * counts each loss in focus_losses, for whoever pauses the game then. Never waits long. */
void display_x11_poll(display_x11_t *x11, const display_x11_sink_t *sink);

/* Plan 008 phase 02: which of buffers to draw the next frame into, never waiting while a segment is free. A segment the server has finished
 * with, the one not put last if both are; or DISPLAY_X11_SPARE when both are still with it, counted in puts_skipped: draw the frame
 * there and do not present it. False as for service, and when neither has come back for five seconds. */
[[nodiscard]] bool display_x11_free_buffer(display_x11_t *x11, uint32_t *out_index);

/* The segments' bookkeeping, apart from the socket so a test can drive it: a put from segment index was sent at now_ns; the server's
 * ShmCompletion for segment (its id on the server) was read at now_ns, which frees that one alone. */
void display_x11_put_note(display_x11_t *x11, uint32_t index, int64_t now_ns);
void display_x11_completion_take(display_x11_t *x11, uint32_t segment, int64_t now_ns);

/* Shows segment index, which display_x11_free_buffer gave. It is not drawn on again until that gives it again. */
[[nodiscard]] bool display_x11_present(display_x11_t *x11, uint32_t index);

#endif
