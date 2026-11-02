/* Replay data for ctest run_input: what the kernel's evdev would hand a reader, event by event. Written by hand from the
 * kernel's event-codes documentation, and not captured from a device, since no test or agent may read one: a keyboard
 * sends EV_MSC scan codes around each key, each group ends in SYN_REPORT, and a held key repeats with value 2. Included
 * once, by ../index.c. */
#ifndef MC_TESTS_RUN_INPUT_FIXTURES_REPLAY_H
#define MC_TESTS_RUN_INPUT_FIXTURES_REPLAY_H

#include <linux/input.h>
#include <stdint.h>

#define REPLAY_EVENT(event_type, event_code, event_value)                                                              \
    {.time = {.tv_sec = 0, .tv_usec = 0}, .type = (event_type), .code = (event_code), .value = (event_value)}
#define REPLAY_SYN REPLAY_EVENT(EV_SYN, SYN_REPORT, 0)
#define REPLAY_SCAN(scan) REPLAY_EVENT(EV_MSC, MSC_SCAN, (scan))
#define REPLAY_COUNT(events) ((uint32_t)(sizeof(events) / sizeof((events)[0])))

/* A drop, with W pressed and let go behind it, all read at once. Whatever EVIOCGKEY says next is newer than all of these. */
static const struct input_event replay_drop_then_stale_release[] = {
    REPLAY_EVENT(EV_SYN, SYN_DROPPED, 0), REPLAY_SYN, REPLAY_EVENT(EV_KEY, KEY_W, 1), REPLAY_SYN,
    REPLAY_EVENT(EV_KEY, KEY_W, 0),       REPLAY_SYN,
};

/* W pressed, held long enough to repeat twice. */
static const struct input_event replay_w_held[] = {
    REPLAY_SCAN(0x70000 + 0x1a),
    REPLAY_EVENT(EV_KEY, KEY_W, 1),
    REPLAY_SYN,
    REPLAY_EVENT(EV_KEY, KEY_W, 2),
    REPLAY_SYN,
    REPLAY_EVENT(EV_KEY, KEY_W, 2),
    REPLAY_SYN,
};

/* W let go. */
static const struct input_event replay_w_up[] = {
    REPLAY_SCAN(0x70000 + 0x1a),
    REPLAY_EVENT(EV_KEY, KEY_W, 0),
    REPLAY_SYN,
};

/* Space tapped faster than a frame, with an LED event of the kind a keyboard also reports. */
static const struct input_event replay_space_tap[] = {
    REPLAY_EVENT(EV_KEY, KEY_SPACE, 1), REPLAY_SYN, REPLAY_EVENT(EV_LED, LED_CAPSL, 1), REPLAY_SYN,
    REPLAY_EVENT(EV_KEY, KEY_SPACE, 0), REPLAY_SYN,
};

/* A mouse moving, in the groups a real one reports, with the hi-res wheel code beside the plain one. */
static const struct input_event replay_mouse_moves[] = {
    REPLAY_EVENT(EV_REL, REL_X, 5),
    REPLAY_EVENT(EV_REL, REL_Y, -3),
    REPLAY_SYN,
    REPLAY_EVENT(EV_REL, REL_X, 7),
    REPLAY_SYN,
    REPLAY_EVENT(EV_REL, REL_Y, 11),
    REPLAY_SYN,
    REPLAY_EVENT(EV_REL, REL_WHEEL, 1),
    REPLAY_EVENT(EV_REL, REL_WHEEL_HI_RES, 120),
    REPLAY_SYN,
    REPLAY_EVENT(EV_REL, REL_HWHEEL, -2),
    REPLAY_EVENT(EV_REL, REL_HWHEEL_HI_RES, -240),
    REPLAY_SYN,
};

/* The left button, as a mouse reports it: its usage code, then the button. */
static const struct input_event replay_left_down[] = {
    REPLAY_SCAN(0x90001),
    REPLAY_EVENT(EV_KEY, BTN_LEFT, 1),
    REPLAY_SYN,
};
static const struct input_event replay_left_up[] = {
    REPLAY_SCAN(0x90001),
    REPLAY_EVENT(EV_KEY, BTN_LEFT, 0),
    REPLAY_SYN,
};

/* Nothing a real device should send: codes past the table, and values with no meaning. */
static const struct input_event replay_garbage[] = {
    REPLAY_EVENT(EV_KEY, KEY_CNT, 1),   REPLAY_EVENT(EV_KEY, 0xffff, 1),        REPLAY_EVENT(EV_KEY, KEY_A, 7),
    REPLAY_EVENT(EV_KEY, KEY_A, -1),    REPLAY_EVENT(EV_KEY, KEY_B, 0),         REPLAY_EVENT(EV_KEY, KEY_C, 2),
    REPLAY_EVENT(EV_REL, REL_MISC, 99), REPLAY_EVENT(EV_REL, 0xffff, 99),       REPLAY_EVENT(EV_ABS, 0, 5),
    REPLAY_EVENT(0xffff, 0xffff, -1),   REPLAY_EVENT(EV_SYN, SYN_MT_REPORT, 0), REPLAY_SYN,
};

/* The queue overflowed: A was pressed before the drop. What sits between the drop and the report must count for nothing,
 * whatever it says, and what follows the report is normal again. */
static const struct input_event replay_drop_at_start[] = {
    REPLAY_EVENT(EV_KEY, KEY_A, 1),    REPLAY_SYN, REPLAY_EVENT(EV_SYN, SYN_DROPPED, 0), REPLAY_EVENT(EV_KEY, KEY_Z, 1),
    REPLAY_EVENT(EV_REL, REL_X, 1000),
};
static const struct input_event replay_drop_at_end[] = {
    REPLAY_EVENT(EV_REL, REL_Y, 1000),
    REPLAY_SYN,
    REPLAY_EVENT(EV_REL, REL_X, 4),
    REPLAY_SYN,
};

#endif
