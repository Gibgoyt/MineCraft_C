/* See run/src/display/headless/demo/README.md. */
#ifndef MC_RUN_DISPLAY_HEADLESS_DEMO_INDEX_H
#define MC_RUN_DISPLAY_HEADLESS_DEMO_INDEX_H

#include "display/headless/index.h"

#include <stdint.h>

enum : uint32_t {
    DISPLAY_DEMO_MARGIN = 8u,
    DISPLAY_DEMO_CELL = 12u,      /* one lamp's side */
    DISPLAY_DEMO_CELL_STEP = 14u, /* from one lamp to the next */
    DISPLAY_DEMO_KEY_COLUMNS = 32u,
    DISPLAY_DEMO_KEY_ROWS = 8u, /* so the codes below 256: the keyboard. Buttons show on the cursor */
    DISPLAY_DEMO_TICK_BITS = 32u,
    DISPLAY_DEMO_LAMP_OFF = 0x00202020u,
    DISPLAY_DEMO_KEY_ON = 0x00ffff00u,
    DISPLAY_DEMO_TICK_ON = 0x0000ffffu,
    DISPLAY_DEMO_CURSOR_IDLE = 0x00ffffffu,
    DISPLAY_DEMO_CURSOR_LEFT = 0x00ff0000u, /* held buttons add up: left and right together are yellow */
    DISPLAY_DEMO_CURSOR_RIGHT = 0x0000ff00u,
    DISPLAY_DEMO_CURSOR_MIDDLE = 0x000000ffu,
    DISPLAY_DEMO_CURSOR_SIDE = 32u, /* with the wheel at 0 */
    DISPLAY_DEMO_CURSOR_SIDE_MIN = 8u,
    DISPLAY_DEMO_CURSOR_SIDE_MAX = 128u,
    DISPLAY_DEMO_CURSOR_WHEEL_STEP = 4u, /* pixels of side per wheel step */
};

/* Where the last frame's cursor was, to repaint it. Zeroed is the start of a run. */
typedef struct display_headless_demo_t {
    uint32_t x_start;
    uint32_t y_start;
    uint32_t x_end;
    uint32_t y_end;
    uint8_t started;
    uint8_t _pad0[3];
} display_headless_demo_t;
static_assert(sizeof(display_headless_demo_t) == 20);

/* The test pattern, a cursor the script moves, a lamp for each key and the tick number in lamps. Fills in a scene whose
 * context is demo. */
void display_headless_demo_scene(display_headless_demo_t *demo, display_headless_scene_t *out);

/* The top-left corner of a key's lamp (code below 256), and of a bit of the tick number's. */
void display_headless_demo_key_lamp(uint32_t code, uint32_t *out_x, uint32_t *out_y);
void display_headless_demo_tick_lamp(uint32_t bit, uint32_t *out_x, uint32_t *out_y);

#endif
