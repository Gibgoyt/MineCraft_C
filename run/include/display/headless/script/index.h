/* See run/src/display/headless/script/README.md. */
#ifndef MC_RUN_DISPLAY_HEADLESS_SCRIPT_INDEX_H
#define MC_RUN_DISPLAY_HEADLESS_SCRIPT_INDEX_H

#include <linux/input-event-codes.h>
#include <stdint.h>

enum : uint32_t {
    DISPLAY_SCRIPT_MAX_BYTES = 1u << 20u,
    DISPLAY_SCRIPT_MAX_EVENTS = 65536u,
    DISPLAY_SCRIPT_MAX_SCREENSHOTS = 1024u,
    DISPLAY_SCRIPT_MAX_TICK = 0x7fffffffu,
    DISPLAY_SCRIPT_NAME_BYTES = 128u,    /* a screenshot's file name, with its NUL */
    DISPLAY_SCRIPT_CODE_COUNT = KEY_CNT, /* evdev's KEY_ and BTN_ codes share one number space */
    DISPLAY_SCRIPT_MESSAGE_BYTES = 120u,
};

typedef enum display_script_kind_t : uint8_t {
    DISPLAY_SCRIPT_KEY = 0,        /* a key or a mouse button: code, pressed */
    DISPLAY_SCRIPT_MOVE = 1,       /* relative mouse motion: dx, dy */
    DISPLAY_SCRIPT_WHEEL = 2,      /* wheel steps: dx */
    DISPLAY_SCRIPT_SCREENSHOT = 3, /* the frame of this tick: name_offset, name_length */
} display_script_kind_t;

/* One line of a script. code is evdev's KEY_* or BTN_*, so what a scene gets here is what input would see. */
typedef struct display_script_event_t {
    uint32_t tick;        /* 0 */
    uint32_t line;        /* 4: where it was in the script, for messages */
    int32_t dx;           /* 8 */
    int32_t dy;           /* 12 */
    uint32_t name_offset; /* 16: into the script's text */
    uint16_t name_length; /* 20: 0 for the default name */
    uint16_t code;        /* 22 */
    uint8_t kind;         /* 24: display_script_kind_t */
    uint8_t pressed;      /* 25 */
    uint8_t _pad0[2];     /* 26 */
} display_script_event_t;
static_assert(sizeof(display_script_event_t) == 28);

typedef struct display_script_t {
    const char *borrowed_text;
    const display_script_event_t *borrowed_events; /* in tick order, and file order within a tick */
    uint32_t event_count;
    uint32_t last_tick; /* of the last line, or of `end` */
    uint32_t screenshot_count;
    uint8_t has_end;
    uint8_t _pad0[3];
} display_script_t;
static_assert(sizeof(display_script_t) == 32);

typedef struct display_script_error_t {
    uint32_t line; /* from 1; 0 for a fault in the whole file */
    char message[DISPLAY_SCRIPT_MESSAGE_BYTES];
    uint8_t _pad0[4];
} display_script_error_t;
static_assert(sizeof(display_script_error_t) == 128);

/* What a script has done to the keyboard and mouse so far. Zeroed is nothing held, and the mouse at (0, 0). */
typedef struct display_script_state_t {
    uint64_t down[DISPLAY_SCRIPT_CODE_COUNT / 64u];
    int64_t mouse_x; /* every move so far, added up */
    int64_t mouse_y;
    int64_t wheel;
    int64_t tick_dx; /* this tick's alone */
    int64_t tick_dy;
    int64_t tick_wheel;
    uint32_t tick;
    uint8_t _pad0[4];
} display_script_state_t;
static_assert(sizeof(display_script_state_t) == 152);

/* Parses a script, whose grammar is in the README, into the caller's events. text must outlive out, whose screenshots
 * point into it, and tick_hz is what turns "500ms" into ticks. On false, error says which line and why, and out is empty. */
bool display_script_parse(const char *text, uint64_t length, uint32_t tick_hz, display_script_event_t *events,
                          uint32_t capacity, display_script_t *out, display_script_error_t *error);

/* The file name a screenshot event writes: its own, or tick_<8 digits>.png. out holds DISPLAY_SCRIPT_NAME_BYTES. */
void display_script_screenshot_name(const display_script_t *script, const display_script_event_t *event, char *out);

/* Starts a tick: clears what counts only for one. */
void display_script_state_begin_tick(display_script_state_t *state, uint32_t tick);

/* Takes in one event. A screenshot changes nothing. */
void display_script_state_apply(display_script_state_t *state, const display_script_event_t *event);

/* Is this KEY_* or BTN_* held? A code past the table is not. */
bool display_script_state_is_down(const display_script_state_t *state, uint32_t code);

#endif
