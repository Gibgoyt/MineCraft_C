/* See README.md. Everything is clipped to the frame, so any frame size works, down to one pixel. */
#include "display/headless/demo/index.h"

#include <string.h>

typedef struct demo_rect_t {
    uint32_t x_start;
    uint32_t y_start;
    uint32_t x_end; /* one past */
    uint32_t y_end;
} demo_rect_t;
static_assert(sizeof(demo_rect_t) == 16);

static uint32_t demo_clip(int64_t value, uint32_t limit) {
    if (value < 0) {
        return 0u;
    }
    return value > (int64_t)limit ? limit : (uint32_t)value;
}

static uint32_t *demo_row(display_buffer_t *frame, uint32_t y) {
    return (uint32_t *)(void *)(frame->owned_map + (uint64_t)y * frame->pitch);
}

static void demo_fill(display_buffer_t *frame, uint32_t x, uint32_t y, uint32_t side, uint32_t colour) {
    const uint32_t x_end = demo_clip((int64_t)x + side, frame->width);
    const uint32_t y_end = demo_clip((int64_t)y + side, frame->height);
    for (uint32_t row_y = y; row_y < y_end; row_y++) {
        uint32_t *const row = demo_row(frame, row_y);
        for (uint32_t column = x; column < x_end; column++) {
            row[column] = colour;
        }
    }
}

void display_headless_demo_key_lamp(uint32_t code, uint32_t *out_x, uint32_t *out_y) {
    *out_x = DISPLAY_DEMO_MARGIN + code % DISPLAY_DEMO_KEY_COLUMNS * DISPLAY_DEMO_CELL_STEP;
    *out_y = DISPLAY_DEMO_MARGIN + code / DISPLAY_DEMO_KEY_COLUMNS * DISPLAY_DEMO_CELL_STEP;
}

void display_headless_demo_tick_lamp(uint32_t bit, uint32_t *out_x, uint32_t *out_y) {
    *out_x = DISPLAY_DEMO_MARGIN + bit * DISPLAY_DEMO_CELL_STEP;
    *out_y = DISPLAY_DEMO_MARGIN + DISPLAY_DEMO_KEY_ROWS * DISPLAY_DEMO_CELL_STEP + DISPLAY_DEMO_CELL_STEP;
}

static void demo_draw_lamps(display_buffer_t *frame, const display_headless_tick_t *tick) {
    uint32_t x = 0;
    uint32_t y = 0;
    for (uint32_t code = 0; code < DISPLAY_DEMO_KEY_COLUMNS * DISPLAY_DEMO_KEY_ROWS; code++) {
        display_headless_demo_key_lamp(code, &x, &y);
        demo_fill(frame, x, y, DISPLAY_DEMO_CELL,
                  display_script_state_is_down(tick->state, code) ? DISPLAY_DEMO_KEY_ON : DISPLAY_DEMO_LAMP_OFF);
    }
    for (uint32_t bit = 0; bit < DISPLAY_DEMO_TICK_BITS; bit++) {
        display_headless_demo_tick_lamp(bit, &x, &y);
        demo_fill(frame, x, y, DISPLAY_DEMO_CELL,
                  (tick->tick >> bit & 1u) != 0u ? DISPLAY_DEMO_TICK_ON : DISPLAY_DEMO_LAMP_OFF);
    }
}

static uint32_t demo_cursor_colour(const display_script_state_t *state) {
    uint32_t colour = 0u;
    colour |= display_script_state_is_down(state, BTN_LEFT) ? DISPLAY_DEMO_CURSOR_LEFT : 0u;
    colour |= display_script_state_is_down(state, BTN_RIGHT) ? DISPLAY_DEMO_CURSOR_RIGHT : 0u;
    colour |= display_script_state_is_down(state, BTN_MIDDLE) ? DISPLAY_DEMO_CURSOR_MIDDLE : 0u;
    return colour != 0u ? colour : DISPLAY_DEMO_CURSOR_IDLE;
}

static demo_rect_t demo_cursor_rect(const display_buffer_t *frame, const display_script_state_t *state) {
    /* The wheel is bounded by clamping first, so a huge sum cannot overflow when scaled. */
    const int64_t wheel = state->wheel > 1000 ? 1000 : (state->wheel < -1000 ? -1000 : state->wheel);
    int64_t side = (int64_t)DISPLAY_DEMO_CURSOR_SIDE + wheel * DISPLAY_DEMO_CURSOR_WHEEL_STEP;
    side = side < DISPLAY_DEMO_CURSOR_SIDE_MIN ? DISPLAY_DEMO_CURSOR_SIDE_MIN : side;
    side = side > DISPLAY_DEMO_CURSOR_SIDE_MAX ? DISPLAY_DEMO_CURSOR_SIDE_MAX : side;
    const int64_t x = (int64_t)demo_clip((int64_t)frame->width / 2 + state->mouse_x, frame->width);
    const int64_t y = (int64_t)demo_clip((int64_t)frame->height / 2 + state->mouse_y, frame->height);
    return (demo_rect_t){.x_start = demo_clip(x - side / 2, frame->width),
                         .y_start = demo_clip(y - side / 2, frame->height),
                         .x_end = demo_clip(x + side / 2, frame->width),
                         .y_end = demo_clip(y + side / 2, frame->height)};
}

static void demo_repaint_pattern(display_buffer_t *frame, const display_headless_demo_t *demo) {
    for (uint32_t y = demo->y_start; y < demo->y_end; y++) {
        uint32_t *const row = demo_row(frame, y);
        for (uint32_t x = demo->x_start; x < demo->x_end; x++) {
            row[x] = display_flip_pattern_pixel(x, y, frame->width, frame->height);
        }
    }
}

static bool demo_draw(void *context, const display_headless_tick_t *tick, display_buffer_t *frame) {
    display_headless_demo_t *const demo = context;
    if (demo->started == 0u) {
        display_flip_pattern_fill(frame);
        demo->started = 1u;
    } else {
        demo_repaint_pattern(frame, demo);
    }
    demo_draw_lamps(frame, tick);

    const demo_rect_t cursor = demo_cursor_rect(frame, tick->state);
    const uint32_t colour = demo_cursor_colour(tick->state);
    for (uint32_t y = cursor.y_start; y < cursor.y_end; y++) {
        uint32_t *const row = demo_row(frame, y);
        for (uint32_t x = cursor.x_start; x < cursor.x_end; x++) {
            row[x] = colour;
        }
    }
    demo->x_start = cursor.x_start;
    demo->y_start = cursor.y_start;
    demo->x_end = cursor.x_end;
    demo->y_end = cursor.y_end;
    return true;
}

void display_headless_demo_scene(display_headless_demo_t *demo, display_headless_scene_t *out) {
    memset(demo, 0, sizeof(*demo));
    *out = (display_headless_scene_t){.borrowed_context = demo, .tick = nullptr, .draw = demo_draw};
}
