/* ctest run_ui_hud. See README.md. */
#include "harness/index.h"

#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "main/index.h"
#include "player/index.h"
#include "png/index.h"
#include "render/index.h"
#include "simd/reference/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    GUARD = 8u, /* pixels kept on every side of a canvas, to catch a write outside it */
    GUARD_PIXEL = 0xA5C3E17Bu,
    GOLD_WIDTH = 640u, /* scale 2 */
    GOLD_HEIGHT = 480u,
    SMALL_WIDTH = 320u, /* scale 1 */
    SMALL_HEIGHT = 240u,
    GAME_WIDTH = 320u,
    GAME_HEIGHT = 240u,
    PROGRAM_TIMEOUT_MS = 120000u,
};

/* A canvas with a guard band around it, in the test arena. */
typedef struct sheet_t {
    uint32_t *pixels;  /* with the guard */
    uint32_t *visible; /* the canvas's first pixel */
    uint32_t *row;
    uint64_t pitch; /* of the canvas, in bytes */
    uint64_t total; /* pixels with the guard */
    uint32_t width;
    uint32_t height;
} sheet_t;
static_assert(sizeof(sheet_t) == 48);

static sheet_t sheet_make(test_context_t *t, uint32_t width, uint32_t height) {
    const uint64_t stride = (uint64_t)width + 2u * GUARD;
    const uint64_t total = stride * ((uint64_t)height + 2u * GUARD);
    uint32_t *const pixels = test_alloc(t, total * sizeof(uint32_t));
    uint32_t *const row = test_alloc(t, UI_CANVAS_ROW_BYTES);
    return (sheet_t){pixels, pixels + stride * GUARD + GUARD, row, stride * sizeof(uint32_t), total, width, height};
}

/* A gradient: flat colours would hide an image put in the wrong place by less than its flat part. */
static void sheet_clear(sheet_t *sheet) {
    for (uint64_t i = 0u; i < sheet->total; i++) {
        sheet->pixels[i] = GUARD_PIXEL;
    }
    const uint64_t stride = sheet->pitch / sizeof(uint32_t);
    for (uint32_t y = 0u; y < sheet->height; y++) {
        for (uint32_t x = 0u; x < sheet->width; x++) {
            const uint32_t r = 40u + 120u * x / sheet->width;
            const uint32_t g = 80u + 120u * y / sheet->height;
            sheet->visible[stride * y + x] = 0xFF000000u | r << 16 | g << 8 | 0x9Cu;
        }
    }
}

static uint32_t *sheet_ref(sheet_t *sheet, int32_t x, int32_t y) {
    return sheet->visible + (uint64_t)y * (sheet->pitch / sizeof(uint32_t)) + (uint64_t)x;
}

static uint32_t sheet_at(const sheet_t *sheet, int32_t x, int32_t y) {
    return sheet->visible[(uint64_t)y * (sheet->pitch / sizeof(uint32_t)) + (uint64_t)x];
}

static bool sheet_canvas(const sheet_t *sheet, ui_canvas_t *canvas) {
    return ui_canvas_start(canvas, (uint8_t *)sheet->visible, sheet->pitch, sheet->width, sheet->height, sheet->row);
}

static void sheet_guard_check(test_context_t *t, const sheet_t *sheet) {
    const uint64_t stride = sheet->pitch / sizeof(uint32_t);
    uint64_t changed = 0u;
    for (uint64_t i = 0u; i < sheet->total; i++) {
        const uint64_t y = i / stride, x = i % stride;
        const bool inside =
            x >= GUARD && x < GUARD + (uint64_t)sheet->width && y >= GUARD && y < GUARD + (uint64_t)sheet->height;
        changed += !inside && sheet->pixels[i] != GUARD_PIXEL ? 1u : 0u;
    }
    CHECK_EQ_U64(changed, 0u);
}

/* The first differing pixel, or -1. Canvas and guard. */
static int64_t sheets_differ(const sheet_t *a, const sheet_t *b) {
    for (uint64_t i = 0u; i < a->total; i++) {
        if (a->pixels[i] != b->pixels[i]) {
            return (int64_t)i;
        }
    }
    return -1;
}

typedef struct fixture_t {
    ui_t ui;
    texture_t texture;
} fixture_t;
static_assert(sizeof(fixture_t) == 1344);

static fixture_t *fixture_start(test_context_t *t) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, sizeof(fixture_t) + alignof(fixture_t));
    fixture_t *const f = (fixture_t *)((raw + alignof(fixture_t) - 1u) & ~((uintptr_t)alignof(fixture_t) - 1u));
    if (!texture_init(&f->texture) || !ui_init(&f->ui, &f->texture)) {
        return nullptr;
    }
    return f;
}

static void fixture_stop(fixture_t *f) {
    ui_deinit(&f->ui);
    texture_deinit(&f->texture);
}

static uint64_t rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ---- The model: its own numbers, a pixel at a time ------------------------------------------------------------------------ */

static void model_blend_at(sheet_t *sheet, int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < (int32_t)sheet->width && y < (int32_t)sheet->height) {
        simd_reference_blend_over(sheet_ref(sheet, x, y), &color, 1u);
    }
}

static void model_image(sheet_t *sheet, const ui_image_t *image, ui_rect_t source, ui_rect_t rect) {
    for (int32_t dy = 0; dy < rect.height; dy++) {
        for (int32_t dx = 0; dx < rect.width; dx++) {
            const int64_t sx = source.x + (int64_t)dx * source.width / rect.width;
            const int64_t sy = source.y + (int64_t)dy * source.height / rect.height;
            model_blend_at(sheet, rect.x + dx, rect.y + dy, image->borrowed_pixels[sy * (int64_t)image->width + sx]);
        }
    }
}

static void model_image_whole(sheet_t *sheet, const ui_image_t *image, ui_rect_t rect) {
    model_image(sheet, image, (ui_rect_t){0, 0, (int32_t)image->width, (int32_t)image->height}, rect);
}

static void model_fill(sheet_t *sheet, ui_rect_t rect, uint32_t color) {
    for (int32_t y = rect.y; y < rect.y + rect.height; y++) {
        for (int32_t x = rect.x; x < rect.x + rect.width; x++) {
            model_blend_at(sheet, x, y, color);
        }
    }
}

/* Numbers from README.md's table, by hand, and not from the header. */
static void model_item(sheet_t *sheet, const ui_hud_t *hud, const ui_font_t *font, const item_stack_t *stack, int32_t x,
                       int32_t y, int32_t scale) {
    if (stack->count == 0u) {
        return;
    }
    model_image_whole(sheet, &hud->borrowed_icons[stack->item], (ui_rect_t){x, y, 16 * scale, 16 * scale});
    const uint32_t durability = item_defs[stack->item].durability;
    if (durability != 0u && stack->wear != 0u) {
        const uint32_t left_exact = (durability - stack->wear) * 13u / durability;
        const uint32_t left = left_exact == 0u ? 1u : left_exact;
        const uint32_t red = (uint32_t)stack->wear * 255u / durability;
        model_fill(sheet, (ui_rect_t){x + 2 * scale, y + 13 * scale, 13 * scale, 2 * scale}, 0xFF000000u);
        model_fill(sheet, (ui_rect_t){x + 2 * scale, y + 13 * scale, (int32_t)left * scale, scale},
                   0xFF000000u | red << 16 | (255u - red) << 8);
    }
    if (stack->count > 1u) {
        char text[16];
        snprintf(text, sizeof(text), "%" PRIu32, (uint32_t)stack->count);
        const ui_text_size_t size = ui_font_measure(font, text, (uint32_t)scale);
        /* The font has its own tests: this is only where it goes. */
        ui_canvas_t canvas;
        if (sheet_canvas(sheet, &canvas)) {
            ui_font_draw(font, &canvas, x + 17 * scale - (int32_t)size.width, y + 9 * scale, text,
                         (ui_text_style_t){.color = 0xFFFFFFFFu, .shadow = 0xFF3F3F3Fu, .scale = (uint32_t)scale});
        }
    }
}

static void model_row(sheet_t *sheet, const ui_image_t *full, const ui_image_t *empty, int32_t left, int32_t right,
                      int32_t y, int32_t scale, uint32_t units, bool from_right) {
    for (int32_t i = 0; i < 10; i++) {
        const int32_t x = from_right ? right - (i + 1) * 9 * scale : left + i * 9 * scale;
        const ui_rect_t rect = {x, y, 9 * scale, 9 * scale};
        model_image_whole(sheet, empty, rect);
        const uint32_t needed = (uint32_t)(i + 1) * 2u;
        if (units >= needed) {
            model_image_whole(sheet, full, rect);
        } else if (units + 1u == needed) {
            const int32_t half = 9 * scale / 2;
            if (from_right) {
                model_image(sheet, full, (ui_rect_t){9, 0, 9, 18},
                            (ui_rect_t){x + 9 * scale - half, y, half, 9 * scale});
            } else {
                model_image(sheet, full, (ui_rect_t){0, 0, 9, 18}, (ui_rect_t){x, y, half, 9 * scale});
            }
        }
    }
}

/* All of ui_hud_draw, in its order. */
static void model_hud(sheet_t *sheet, const ui_hud_t *hud, const ui_font_t *font, const ui_hud_state_t *state,
                      int32_t scale) {
    const int32_t width = (int32_t)sheet->width, height = (int32_t)sheet->height;
    const int32_t bar_x = (width - 182 * scale) / 2, bar_y = height - 22 * scale;
    const int32_t cross = 6 * scale;
    model_image_whole(sheet, &hud->crosshair, (ui_rect_t){(width - cross) / 2, (height - cross) / 2, cross, cross});
    /* The texture's eight, then its last again, then its last two columns. */
    for (int32_t slot = 0; slot < 9; slot++) {
        model_image(sheet, &hud->hotbar, (ui_rect_t){(slot < 8 ? slot : 7) * 20, 0, 20, 22},
                    (ui_rect_t){bar_x + slot * 20 * scale, bar_y, 20 * scale, 22 * scale});
    }
    model_image(sheet, &hud->hotbar, (ui_rect_t){160, 0, 2, 22},
                (ui_rect_t){bar_x + 180 * scale, bar_y, 2 * scale, 22 * scale});
    if (state->selected < 9u) {
        model_image_whole(
            sheet, &hud->selected,
            (ui_rect_t){bar_x + (int32_t)state->selected * 20 * scale - scale, bar_y - scale, 24 * scale, 24 * scale});
    }
    for (int32_t slot = 0; slot < 9; slot++) {
        model_item(sheet, hud, font, &state->hotbar[slot], bar_x + (slot * 20 + 3) * scale, bar_y + 3 * scale, scale);
    }
    const int32_t row_y = bar_y - 10 * scale;
    model_row(sheet, &hud->health, &hud->health_empty, bar_x, bar_x + 182 * scale, row_y, scale, state->health, false);
    model_row(sheet, &hud->food, &hud->food_empty, bar_x, bar_x + 182 * scale, row_y, scale, state->food, true);
}

/* A real item that wears, and one that stacks. */
static uint16_t item_that_wears(uint32_t nth) {
    uint32_t seen = 0u;
    for (uint32_t i = 1u; i < ITEM_COUNT; i++) {
        if (item_defs[i].durability != 0u && seen++ == nth) {
            return (uint16_t)i;
        }
    }
    return 0u;
}

static uint16_t item_that_stacks(uint32_t nth) {
    uint32_t seen = 0u;
    for (uint32_t i = 1u; i < ITEM_COUNT; i++) {
        if (item_defs[i].max_stack > 1u && seen++ == nth) {
            return (uint16_t)i;
        }
    }
    return 0u;
}

static void state_random(ui_hud_state_t *state, uint64_t *rng) {
    memset(state, 0, sizeof(*state));
    state->selected = (uint8_t)(rng_next(rng) % 11u); /* past the end marks none */
    state->health = (uint8_t)(rng_next(rng) % 21u);
    state->food = (uint8_t)(rng_next(rng) % 21u);
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        const uint64_t kind = rng_next(rng) % 4u;
        if (kind == 0u) {
            continue;
        }
        const uint16_t item = (uint16_t)(1u + rng_next(rng) % (ITEM_COUNT - 1u));
        const uint32_t room = item_defs[item].max_stack;
        state->hotbar[slot] = item_stack_make(item, 1u + (uint32_t)(rng_next(rng) % room));
        if (item_defs[item].durability != 0u) {
            state->hotbar[slot].wear = (uint16_t)(rng_next(rng) % item_defs[item].durability);
        }
    }
}

/* ---- Numbers by hand ------------------------------------------------------------------------------------------------------ */

TEST(half_units_go_up_to_the_next_half_and_stop_at_the_ends) {
    CHECK_EQ_U64(ui_hud_half_units(0.0f32, 20u), 0u);
    CHECK_EQ_U64(ui_hud_half_units(-3.0f32, 20u), 0u);
    CHECK_EQ_U64(ui_hud_half_units(0.01f32, 20u), 1u);
    CHECK_EQ_U64(ui_hud_half_units(1.0f32, 20u), 1u);
    CHECK_EQ_U64(ui_hud_half_units(1.5f32, 20u), 2u);
    CHECK_EQ_U64(ui_hud_half_units(19.0f32, 20u), 19u);
    CHECK_EQ_U64(ui_hud_half_units(19.2f32, 20u), 20u);
    CHECK_EQ_U64(ui_hud_half_units(20.0f32, 20u), 20u);
    CHECK_EQ_U64(ui_hud_half_units(500.0f32, 20u), 20u);
    const uint32_t bits = 0x7FC00000u; /* a NaN */
    float32_t nan_value = 0.0f32;
    memcpy(&nan_value, &bits, sizeof(nan_value));
    CHECK_EQ_U64(ui_hud_half_units(nan_value, 20u), 0u);
}

TEST(things_go_where_the_readme_says_at_three_sizes) {
    ui_hud_layout_t layout = ui_hud_layout(320u, 240u);
    CHECK_EQ_U64(layout.scale, 1u);
    CHECK_EQ_I64(layout.hotbar.x, 69);
    CHECK_EQ_I64(layout.hotbar.y, 218);
    CHECK_EQ_I64(layout.hotbar.width, 182);
    CHECK_EQ_I64(layout.hotbar.height, 22);
    CHECK_EQ_I64(layout.crosshair.x, 157);
    CHECK_EQ_I64(layout.crosshair.y, 117);
    CHECK_EQ_I64(layout.row_y, 208);

    layout = ui_hud_layout(640u, 480u);
    CHECK_EQ_U64(layout.scale, 2u);
    CHECK_EQ_I64(layout.hotbar.x, 138);
    CHECK_EQ_I64(layout.hotbar.y, 436);
    CHECK_EQ_I64(layout.crosshair.width, 12);
    CHECK_EQ_I64(layout.row_y, 416);

    layout = ui_hud_layout(1920u, 1080u);
    CHECK_EQ_U64(layout.scale, 4u);
    CHECK_EQ_I64(layout.hotbar.x, 596);
    CHECK_EQ_I64(layout.hotbar.y, 992);
    CHECK_EQ_I64(layout.hotbar.width, 728);
    CHECK_EQ_I64(layout.crosshair.x, 948);
    CHECK_EQ_I64(layout.crosshair.y, 528);
    CHECK_EQ_I64(layout.row_y, 952);
}

TEST(every_item_has_an_icon_and_the_textures_are_the_sizes_this_draws_them_at) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    const ui_hud_t *const hud = &f->ui.hud;
    CHECK(hud->borrowed_icons[ITEM_ID_NONE].borrowed_pixels == nullptr);
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        CHECK(hud->borrowed_icons[item].borrowed_pixels != nullptr);
        CHECK(hud->borrowed_icons[item].width > 0u && hud->borrowed_icons[item].height > 0u);
    }
    CHECK_EQ_U64(hud->hotbar.width, 162u);
    CHECK_EQ_U64(hud->hotbar.height, UI_HUD_HOTBAR_HEIGHT);
    CHECK_EQ_U64(hud->selected.width, UI_HUD_SELECTED_SIZE);
    CHECK_EQ_U64(hud->crosshair.width, UI_HUD_CROSSHAIR_SIZE);
    CHECK_EQ_U64(hud->health.width, 18u);
    CHECK_EQ_U64(hud->food.width, 18u);
    /* What the budget in run/src/ui/index.c says about the tables. */
    CHECK(f->ui.arena_tables.used < 1024u * 1024u);
    CHECK_EQ_U64(f->ui.arena_tables.failed_count, 0u);
    fixture_stop(f);
}

/* ---- Against the model ---------------------------------------------------------------------------------------------------- */

static void against_model(test_context_t *t, const fixture_t *f, sheet_t *drawn, sheet_t *model,
                          const ui_hud_state_t *state) {
    sheet_clear(drawn);
    sheet_clear(model);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(drawn, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, state);
    const ui_hud_layout_t layout = ui_hud_layout(drawn->width, drawn->height);
    model_hud(model, &f->ui.hud, &f->ui.font, state, (int32_t)layout.scale);
    const int64_t first = sheets_differ(drawn, model);
    if (first >= 0) {
        printf("  at %" PRIu32 " by %" PRIu32 ": pixel %" PRId64 ": %08x, and the model says %08x\n", drawn->width,
               drawn->height, first, drawn->pixels[first], model->pixels[first]);
    }
    CHECK_EQ_I64(first, -1);
    sheet_guard_check(t, drawn);
}

TEST(random_states_match_the_model_at_any_size_and_nothing_lands_outside) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    static const uint32_t sizes[][2] = {
        {1u, 1u},     {17u, 9u},    {100u, 50u},  {181u, 300u}, {182u, 22u},    {320u, 240u},
        {321u, 241u}, {640u, 480u}, {800u, 300u}, {960u, 720u}, {1920u, 1080u},
    };
    uint64_t rng = 0x5EEDFACE1234567Bull;
    for (uint32_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        sheet_t drawn = sheet_make(t, sizes[s][0], sizes[s][1]);
        sheet_t model = sheet_make(t, sizes[s][0], sizes[s][1]);
        for (uint32_t round = 0u; round < 4u; round++) {
            ui_hud_state_t state;
            state_random(&state, &rng);
            against_model(t, f, &drawn, &model, &state);
        }
    }
    fixture_stop(f);
}

TEST(a_half_shows_the_side_nearest_the_full_ones) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    const ui_hud_t *const hud = &f->ui.hud;
    sheet_t half = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t none = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t full = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    const ui_hud_layout_t layout = ui_hud_layout(GOLD_WIDTH, GOLD_HEIGHT);
    ui_hud_state_t state;
    memset(&state, 0, sizeof(state));
    state.selected = 200u;
    const sheet_t *const sheets[3] = {&none, &half, &full};
    for (uint32_t i = 0u; i < 3u; i++) {
        sheet_clear((sheet_t *)sheets[i]);
        ui_canvas_t canvas;
        REQUIRE(sheet_canvas(sheets[i], &canvas));
        state.health = (uint8_t)i;
        state.food = (uint8_t)i;
        ui_hud_draw(hud, &canvas, &f->ui.font, &state);
    }
    const int32_t icon = 18; /* at scale 2 */
    const int32_t bar_x = layout.hotbar.x, bar_right = bar_x + layout.hotbar.width;
    /* The first heart's left half is the full one's, and its right half the empty one's. */
    for (int32_t y = layout.row_y; y < layout.row_y + icon; y++) {
        for (int32_t x = 0; x < icon / 2; x++) {
            CHECK_EQ_U64(sheet_at(&half, bar_x + x, y), sheet_at(&full, bar_x + x, y));
            CHECK_EQ_U64(sheet_at(&half, bar_x + icon / 2 + x, y), sheet_at(&none, bar_x + icon / 2 + x, y));
            /* The last drumstick's right half is the full one's, and its left half the empty one's. */
            CHECK_EQ_U64(sheet_at(&half, bar_right - icon / 2 + x, y), sheet_at(&full, bar_right - icon / 2 + x, y));
            CHECK_EQ_U64(sheet_at(&half, bar_right - icon + x, y), sheet_at(&none, bar_right - icon + x, y));
        }
    }
    /* And the two really do look different. */
    CHECK(sheets_differ(&full, &none) >= 0);
    fixture_stop(f);
}

/* The bounding box of what differs. */
typedef struct box_t {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} box_t;
static_assert(sizeof(box_t) == 16);

static box_t sheets_box(const sheet_t *a, const sheet_t *b) {
    box_t box = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
    for (int32_t y = 0; y < (int32_t)a->height; y++) {
        for (int32_t x = 0; x < (int32_t)a->width; x++) {
            if (sheet_at(a, x, y) != sheet_at(b, x, y)) {
                box.left = x < box.left ? x : box.left;
                box.top = y < box.top ? y : box.top;
                box.right = x > box.right ? x : box.right;
                box.bottom = y > box.bottom ? y : box.bottom;
            }
        }
    }
    return box;
}

TEST(what_is_in_a_slot_stays_in_that_slot_and_a_count_or_wear_changes_only_itself) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    const ui_hud_t *const hud = &f->ui.hud;
    const ui_hud_layout_t layout = ui_hud_layout(GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t empty = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t one = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t many = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    ui_hud_state_t state;
    memset(&state, 0, sizeof(state));
    state.selected = 200u;
    sheet_clear(&empty);
    sheet_clear(&one);
    sheet_clear(&many);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&empty, &canvas));
    ui_hud_draw(hud, &canvas, &f->ui.font, &state);

    const uint16_t stone = item_that_stacks(3u);
    const uint32_t slot = 4u;
    state.hotbar[slot] = item_stack_make(stone, 1u);
    REQUIRE(sheet_canvas(&one, &canvas));
    ui_hud_draw(hud, &canvas, &f->ui.font, &state);
    const box_t icon = sheets_box(&empty, &one);
    const int32_t icon_left = layout.hotbar.x + (int32_t)slot * 40 + 6;
    const int32_t icon_top = layout.hotbar.y + 6;
    CHECK(icon.left >= icon_left && icon.right < icon_left + 32);
    CHECK(icon.top >= icon_top && icon.bottom < icon_top + 32);

    /* A count of one says nothing, and more says it at the bottom right, in white. */
    state.hotbar[slot].count = 7u;
    REQUIRE(sheet_canvas(&many, &canvas));
    ui_hud_draw(hud, &canvas, &f->ui.font, &state);
    const box_t count = sheets_box(&one, &many);
    CHECK(count.left > icon_left + 16);
    CHECK(count.top >= icon_top + 18);
    bool white = false;
    for (int32_t y = count.top; y <= count.bottom; y++) {
        for (int32_t x = count.left; x <= count.right; x++) {
            white = white || (sheet_at(&many, x, y) & 0xFFFFFFu) == 0xFFFFFFu;
        }
    }
    CHECK(white);

    /* Wear puts a bar on what wears: its green part shrinks as it goes, and a new one has none. */
    const uint16_t pick = item_that_wears(0u);
    REQUIRE(pick != 0u);
    const uint32_t durability = item_defs[pick].durability;
    state.hotbar[slot] = item_stack_make(pick, 1u);
    sheet_clear(&one);
    REQUIRE(sheet_canvas(&one, &canvas));
    ui_hud_draw(hud, &canvas, &f->ui.font, &state);
    state.hotbar[slot].wear = (uint16_t)(durability / 2u);
    sheet_clear(&many);
    REQUIRE(sheet_canvas(&many, &canvas));
    ui_hud_draw(hud, &canvas, &f->ui.font, &state);
    const box_t bar = sheets_box(&one, &many);
    CHECK_EQ_I64(bar.left, icon_left + 4);
    CHECK_EQ_I64(bar.top, icon_top + 26);
    CHECK_EQ_I64(bar.right, icon_left + 4 + 26 - 1);
    CHECK_EQ_I64(bar.bottom, icon_top + 26 + 4 - 1);
    fixture_stop(f);
}

TEST(a_nearly_gone_tool_still_shows_a_pixel_and_what_wears_past_its_end_is_safe) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    const uint16_t pick = item_that_wears(0u);
    REQUIRE(pick != 0u);
    sheet_t sheet = sheet_make(t, SMALL_WIDTH, SMALL_HEIGHT);
    item_stack_t stack = item_stack_make(pick, 1u);
    stack.wear = (uint16_t)(item_defs[pick].durability - 1u);
    sheet_clear(&sheet);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    ui_hud_item_draw(&f->ui.hud, &canvas, &f->ui.font, &stack, 10, 10, 1u);
    /* A pixel of the bar's colour, nearly all red, at the start of the bar. */
    const uint32_t lit = sheet_at(&sheet, 12, 23);
    CHECK(((lit >> 16) & 0xFFu) > 250u);
    /* Past the end, which only a bad save could give: all red, and no crash. No item past the table draws at all. */
    stack.wear = 0xFFFFu;
    ui_hud_item_draw(&f->ui.hud, &canvas, &f->ui.font, &stack, 40, 10, 1u);
    CHECK_EQ_U64(sheet_at(&sheet, 42, 23) & 0xFFFFFFu, 0xFF0000u);
    const uint32_t kept = sheet_at(&sheet, 100, 100);
    stack.item = (uint16_t)ITEM_COUNT;
    ui_hud_item_draw(&f->ui.hud, &canvas, &f->ui.font, &stack, 100, 100, 1u);
    CHECK_EQ_U64(sheet_at(&sheet, 100, 100), kept);
    fixture_stop(f);
}

TEST(the_overlays_numbers_change_only_their_own_line_and_it_stays_on_its_panel) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    sheet_t a = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t b = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t clean = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    ui_hud_debug_t debug;
    memset(&debug, 0, sizeof(debug));
    debug.fps = 60.0f32;
    for (uint32_t i = 0u; i < UI_HUD_STAGE_COUNT; i++) {
        debug.stage_ms[i] = 1.0f32 + (float32_t)i;
    }
    sheet_clear(&clean);
    sheet_clear(&a);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&a, &canvas));
    ui_hud_debug_draw(&canvas, &f->ui.font, &debug);
    const ui_rect_t panel = ui_hud_debug_panel(GOLD_WIDTH, GOLD_HEIGHT);
    /* At scale 2 the text is scale 1: margin 2, and lines 9 apart. */
    CHECK_EQ_I64(panel.x, 2);
    CHECK_EQ_I64(panel.y, 2);
    CHECK_EQ_I64(panel.width, 108);
    CHECK_EQ_I64(panel.height, 94);
    const box_t all = sheets_box(&clean, &a);
    CHECK_EQ_I64(all.left, panel.x);
    CHECK_EQ_I64(all.top, panel.y);
    CHECK_EQ_I64(all.right, panel.x + panel.width - 1);
    CHECK_EQ_I64(all.bottom, panel.y + panel.height - 1);

    for (uint32_t i = 0u; i <= UI_HUD_STAGE_COUNT; i++) {
        ui_hud_debug_t other = debug;
        if (i == 0u) {
            other.fps = 144.5f32;
        } else {
            other.stage_ms[i - 1u] = 7.77f32;
        }
        sheet_clear(&b);
        REQUIRE(sheet_canvas(&b, &canvas));
        ui_hud_debug_draw(&canvas, &f->ui.font, &other);
        const box_t line = sheets_box(&a, &b);
        const int32_t top = panel.y + 2 + (int32_t)i * 9;
        CHECK(line.left <= line.right);
        CHECK(line.top >= top);
        CHECK(line.bottom < top + 9);
    }

    /* Times no one should see stay on the panel. */
    for (uint32_t i = 0u; i < UI_HUD_STAGE_COUNT; i++) {
        debug.stage_ms[i] = 3.0e38f32;
    }
    debug.fps = 3.0e38f32;
    sheet_clear(&b);
    REQUIRE(sheet_canvas(&b, &canvas));
    ui_hud_debug_draw(&canvas, &f->ui.font, &debug);
    const box_t wild = sheets_box(&clean, &b);
    CHECK_EQ_I64(wild.left, panel.x);
    CHECK_EQ_I64(wild.right, panel.x + panel.width - 1);
    sheet_guard_check(t, &b);
    /* And the clip it found is the clip it leaves. */
    CHECK_EQ_I64(canvas.clip.width, (int32_t)GOLD_WIDTH);
    CHECK_EQ_I64(canvas.clip.height, (int32_t)GOLD_HEIGHT);

    /* On a canvas too small for it, nothing lands outside. */
    sheet_t tiny = sheet_make(t, 40u, 30u);
    sheet_clear(&tiny);
    REQUIRE(sheet_canvas(&tiny, &canvas));
    ui_hud_debug_draw(&canvas, &f->ui.font, &debug);
    sheet_guard_check(t, &tiny);
    fixture_stop(f);
}

TEST(memory_is_given_back_and_a_frame_takes_none_from_the_heap) {
    heap_counters_t start;
    heap_counters_read(&start);
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    uint32_t *const picture = test_alloc(t, (uint64_t)GOLD_WIDTH * GOLD_HEIGHT * sizeof(uint32_t));
    ui_hud_state_t state;
    uint64_t rng = 99u;
    state_random(&state, &rng);
    ui_hud_debug_t debug;
    memset(&debug, 0, sizeof(debug));
    REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)picture, GOLD_WIDTH, GOLD_HEIGHT));
    ui_hud_draw(&f->ui.hud, &f->ui.canvas, &f->ui.font, &state);
    (void)ui_frame_end(&f->ui, false);
    heap_counters_t first;
    heap_counters_read(&first);
    const uint64_t tables_allocs = f->ui.arena_tables.alloc_count;
    const uint64_t frame_allocs = f->ui.arena_frame.alloc_count;
    for (uint32_t frame = 0u; frame < 200u; frame++) {
        state_random(&state, &rng);
        REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)picture, GOLD_WIDTH, GOLD_HEIGHT));
        ui_hud_draw(&f->ui.hud, &f->ui.canvas, &f->ui.font, &state);
        ui_hud_debug_draw(&f->ui.canvas, &f->ui.font, &debug);
        (void)ui_frame_end(&f->ui, false);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, first.reserve_calls);
    CHECK_EQ_U64(after.release_calls, first.release_calls);
    CHECK_EQ_U64(f->ui.arena_tables.alloc_count, tables_allocs);
    CHECK_EQ_U64(f->ui.arena_frame.alloc_count, frame_allocs);
    fixture_stop(f);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, start.live_bytes);
    CHECK_EQ_U64(after.live_regions, start.live_regions);
}

/* ---- Golden pictures ------------------------------------------------------------------------------------------------------ */

static void golden_compare(test_context_t *t, const char *path, const sheet_t *sheet) {
    const uint64_t capacity = display_png_bytes_needed(sheet->width, sheet->height);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode((const uint8_t *)sheet->visible, (uint32_t)sheet->pitch, sheet->width, sheet->height,
                                   file, capacity, &written));
        REQUIRE(file_write_atomic(path, file, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    REQUIRE(info.width == sheet->width && info.height == sheet->height);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u;
    for (int32_t y = 0; y < (int32_t)sheet->height; y++) {
        for (int32_t x = 0; x < (int32_t)sheet->width; x++) {
            const uint8_t *const p = &rgba[((uint64_t)y * sheet->width + (uint64_t)x) * 4u];
            const uint32_t golden = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[2];
            wrong += (sheet_at(sheet, x, y) & 0xFFFFFFu) == golden ? 0u : 1u;
        }
    }
    printf("  %s: %" PRIu64 " pixels differ\n", path, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* Full bars, one of each kind of thing in the slots, the first slot in hand. */
static void state_golden_full(ui_hud_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->health = 20u;
    state->food = 20u;
    state->selected = 0u;
    state->hotbar[0] = item_stack_make(ITEM_ID_DIRT, 64u);
    state->hotbar[1] = item_stack_make(ITEM_ID_COBBLESTONE, 12u);
    state->hotbar[2] = item_stack_make(ITEM_ID_APPLE, 1u);
    state->hotbar[3] = item_stack_make(item_that_wears(0u), 1u);
    state->hotbar[4] = item_stack_make(item_that_wears(4u), 1u);
    state->hotbar[4].wear = (uint16_t)(item_defs[item_that_wears(4u)].durability / 2u);
    state->hotbar[5] = item_stack_make(item_that_wears(8u), 1u);
    state->hotbar[5].wear = (uint16_t)(item_defs[item_that_wears(8u)].durability - 1u);
    state->hotbar[8] = item_stack_make(ITEM_ID_COAL, 9u);
}

/* Halves in both rows and nearly empty, and the last slot in hand. */
static void state_golden_low(ui_hud_state_t *state) {
    state_golden_full(state);
    state->health = 7u;
    state->food = 3u;
    state->selected = 8u;
}

static void debug_golden(ui_hud_debug_t *debug) {
    memset(debug, 0, sizeof(*debug));
    debug->fps = 143.9f32;
    debug->stage_ms[UI_HUD_STAGE_COARSE] = 0.42f32;
    debug->stage_ms[UI_HUD_STAGE_PRIMARY] = 2.31f32;
    debug->stage_ms[UI_HUD_STAGE_SHADE] = 2.87f32;
    debug->stage_ms[UI_HUD_STAGE_POST] = 0.76f32;
    debug->stage_ms[UI_HUD_STAGE_READBACK] = 0.39f32;
    debug->stage_ms[UI_HUD_STAGE_GPU_TOTAL] = 6.94f32;
    debug->stage_ms[UI_HUD_STAGE_SIM_TICK] = 0.18f32;
    debug->stage_ms[UI_HUD_STAGE_HUD] = 0.65f32;
    debug->stage_ms[UI_HUD_STAGE_PRESENT] = 1.02f32;
}

TEST(golden_pictures_match) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    ui_hud_state_t state;
    ui_hud_debug_t debug;
    ui_canvas_t canvas;

    sheet_t large = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    state_golden_full(&state);
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    golden_compare(t, "tests/run/ui/hud/fixtures/full_640x480.png", &large);

    sheet_t small = sheet_make(t, SMALL_WIDTH, SMALL_HEIGHT);
    state_golden_low(&state);
    sheet_clear(&small);
    REQUIRE(sheet_canvas(&small, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    golden_compare(t, "tests/run/ui/hud/fixtures/low_320x240.png", &small);

    debug_golden(&debug);
    state_golden_low(&state);
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    ui_hud_debug_draw(&canvas, &f->ui.font, &debug);
    golden_compare(t, "tests/run/ui/hud/fixtures/debug_640x480.png", &large);
    fixture_stop(f);
}

/* ---- The real game -------------------------------------------------------------------------------------------------------- */

static void *world_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* Everything a real run has, on memory of its own. */
typedef struct rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
    item_t *item;                /* 80 */
    ui_t *ui;                    /* 88 */
} rig_t;
static_assert(sizeof(rig_t) == 96);

static bool rig_begin(test_context_t *t, rig_t *rig, bool walking) {
    rig->bridge = world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = world_aligned(t, sizeof(job_system_t));
    rig->world = world_aligned(t, sizeof(world_t));
    rig->terrain = world_aligned(t, sizeof(terrain_t));
    rig->render = world_aligned(t, sizeof(render_t));
    rig->texture = world_aligned(t, sizeof(texture_t));
    rig->game = world_aligned(t, sizeof(game_t));
    rig->physics = world_aligned(t, sizeof(physics_t));
    rig->player = world_aligned(t, sizeof(player_t));
    rig->item = world_aligned(t, sizeof(item_t));
    rig->ui = world_aligned(t, sizeof(ui_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(item_init(rig->item)) || !CHECK(ui_init(rig->ui, rig->texture)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    if (walking) {
        game_player_set(rig->game, rig->player);
        game_item_set(rig->game, rig->item);
    }
    return true;
}

static void rig_end(test_context_t *t, rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    ui_deinit(rig->ui);
    item_deinit(rig->item);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const game_config_t rig_small_game = {
    .render_distance = 4u, .width = GAME_WIDTH, .height = GAME_HEIGHT, ._pad0 = {0}};

/* A display's buffer in plain memory. */
static display_buffer_t rig_target(test_context_t *t) {
    const uint64_t bytes = post_xrgb_bytes(GAME_WIDTH, GAME_HEIGHT);
    return (display_buffer_t){.owned_map = world_aligned(t, bytes),
                              .map_bytes = bytes,
                              .pitch = GAME_WIDTH * 4u,
                              .width = GAME_WIDTH,
                              .height = GAME_HEIGHT};
}

/* One frame as --headless makes it, with no script behind it. */
static bool rig_scene_frame(test_context_t *t, display_headless_scene_t *scene, display_buffer_t *target) {
    display_headless_tick_t tick;
    memset(&tick, 0, sizeof(tick));
    return CHECK(scene->draw(scene->borrowed_context, &tick, target));
}

/* The picture as the card left it, and what should be on the screen: the model drawn over it. */
static void rig_expect(const rig_t *rig, const uint8_t *picture, sheet_t *model) {
    const uint64_t stride = model->pitch / sizeof(uint32_t);
    for (uint64_t i = 0u; i < model->total; i++) {
        model->pixels[i] = GUARD_PIXEL;
    }
    for (uint32_t y = 0u; y < GAME_HEIGHT; y++) {
        memcpy(&model->visible[stride * y], picture + (uint64_t)y * GAME_WIDTH * 4u, GAME_WIDTH * 4u);
    }
    ui_hud_state_t state;
    game_hud_state_read(rig->game, &state);
    const ui_hud_layout_t layout = ui_hud_layout(GAME_WIDTH, GAME_HEIGHT);
    model_hud(model, &rig->ui->hud, &rig->ui->font, &state, (int32_t)layout.scale);
}

static uint64_t target_differs(const display_buffer_t *target, const sheet_t *model) {
    uint64_t wrong = 0u;
    const uint32_t *const pixels = (const uint32_t *)(const void *)target->owned_map;
    for (uint32_t y = 0u; y < GAME_HEIGHT; y++) {
        for (uint32_t x = 0u; x < GAME_WIDTH; x++) {
            wrong += (pixels[(uint64_t)y * GAME_WIDTH + x] & 0xFFFFFFu) !=
                             (sheet_at(model, (int32_t)x, (int32_t)y) & 0xFFFFFFu)
                         ? 1u
                         : 0u;
        }
    }
    return wrong;
}

TEST(in_the_real_game_the_screen_is_the_hud_over_the_picture_and_the_picture_is_left_alone) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    REQUIRE(game_open(game, &rig_small_game));
    REQUIRE(game_settle(game));
    /* What a player has, through the tick that tells main. */
    inventory_add(&rig.item->inventory, ITEM_ID_DIRT, 23u);
    inventory_add(&rig.item->inventory, ITEM_ID_APPLE, 1u);
    inventory_select(&rig.item->inventory, 2u);
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_script_state_t *const script = test_alloc(t, sizeof(*script));
    memset(script, 0, sizeof(*script));
    display_headless_tick_t tick;
    memset(&tick, 0, sizeof(tick));
    tick.state = script;
    REQUIRE(scene.tick(scene.borrowed_context, &tick));

    ui_hud_state_t state;
    game_hud_state_read(game, &state);
    CHECK_EQ_U64(state.hotbar[0].item, ITEM_ID_DIRT);
    CHECK_EQ_U64(state.hotbar[0].count, 23u);
    CHECK_EQ_U64(state.selected, 2u);
    CHECK_EQ_U64(state.health, 20u);
    CHECK_EQ_U64(state.food, 20u);

    display_buffer_t target = rig_target(t);
    sheet_t model = sheet_make(t, GAME_WIDTH, GAME_HEIGHT);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    const render_frame_t *const frame = &rig.render->frame;
    const uint8_t *const picture = frame->borrowed_pictures[frame->pictures.front];
    rig_expect(&rig, picture, &model);
    CHECK_EQ_U64(target_differs(&target, &model), 0u);
    /* The picture has nothing of it: the HUD's solid middle pixel of a slot's frame is not in the picture, and the two differ. */
    CHECK(memcmp(picture, target.owned_map, (uint64_t)GAME_WIDTH * GAME_HEIGHT * 4u) != 0);

    /* The body hurts, and the screen knows by the next tick. */
    player_survival_hurt(&rig.player->survival, 5.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    game_hud_state_read(game, &state);
    CHECK(state.health < 20u && state.health >= 14u);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    rig_expect(&rig, frame->borrowed_pictures[frame->pictures.front], &model);
    CHECK_EQ_U64(target_differs(&target, &model), 0u);

    /* F3 comes up over it, staying on its panel; a held key is one press, and a second press takes it away. */
    display_buffer_t with_overlay = rig_target(t);
    CHECK_EQ_U64(game->hud_debug, 0u);
    game_debug_send(game, true);
    CHECK_EQ_U64(game->hud_debug, 1u);
    game_debug_send(game, true);
    CHECK_EQ_U64(game->hud_debug, 1u);
    REQUIRE(rig_scene_frame(t, &scene, &with_overlay));
    rig_expect(&rig, frame->borrowed_pictures[frame->pictures.front], &model);
    const ui_rect_t panel = ui_hud_debug_panel(GAME_WIDTH, GAME_HEIGHT);
    uint64_t outside = 0u, inside = 0u;
    const uint32_t *const shown = (const uint32_t *)(const void *)with_overlay.owned_map;
    for (int32_t y = 0; y < (int32_t)GAME_HEIGHT; y++) {
        for (int32_t x = 0; x < (int32_t)GAME_WIDTH; x++) {
            const bool same =
                (shown[(uint64_t)y * GAME_WIDTH + (uint64_t)x] & 0xFFFFFFu) == (sheet_at(&model, x, y) & 0xFFFFFFu);
            if (ui_rect_contains(panel, x, y)) {
                inside += same ? 0u : 1u;
            } else {
                outside += same ? 0u : 1u;
            }
        }
    }
    CHECK_EQ_U64(outside, 0u);
    CHECK(inside > 1000u);
    /* The card's clock reached it, with one thread as with three. */
    ui_hud_debug_t debug;
    game_hud_debug_read(game, &debug);
    CHECK(debug.stage_ms[UI_HUD_STAGE_GPU_TOTAL] > 0.0f32);
    CHECK(debug.stage_ms[UI_HUD_STAGE_PRIMARY] > 0.0f32);
    game_debug_send(game, false);
    game_debug_send(game, true);
    CHECK_EQ_U64(game->hud_debug, 0u);
    game_close(game);
    rig_end(t, &rig);
}

TEST(flying_with_no_overlay_is_the_picture_itself_byte_for_byte_and_f3_works_there_too) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    REQUIRE(game_open(game, &rig_small_game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_buffer_t target = rig_target(t);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    const render_frame_t *const frame = &rig.render->frame;
    CHECK_MEM_EQ(frame->borrowed_pictures[frame->pictures.front], target.owned_map,
                 (uint64_t)GAME_WIDTH * GAME_HEIGHT * 4u);
    /* Nothing was reserved for a copy that was never needed. */
    CHECK_EQ_U64(rig.ui->owned_composite.bytes, 0u);
    game_debug_send(game, true);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    CHECK(memcmp(frame->borrowed_pictures[frame->pictures.front], target.owned_map,
                 (uint64_t)GAME_WIDTH * GAME_HEIGHT * 4u) != 0);
    /* The HUD itself stays away: no hotbar at the bottom with no one to hold it. */
    const uint8_t *const picture = frame->borrowed_pictures[frame->pictures.front];
    CHECK_MEM_EQ(picture + (uint64_t)(GAME_HEIGHT - 30u) * GAME_WIDTH * 4u,
                 target.owned_map + (uint64_t)(GAME_HEIGHT - 30u) * GAME_WIDTH * 4u, (uint64_t)GAME_WIDTH * 30u * 4u);
    game_close(game);
    rig_end(t, &rig);
}

TEST(with_no_ui_the_game_is_as_it_always_was) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &rig_small_game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_buffer_t target = rig_target(t);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    const render_frame_t *const frame = &rig.render->frame;
    CHECK_MEM_EQ(frame->borrowed_pictures[frame->pictures.front], target.owned_map,
                 (uint64_t)GAME_WIDTH * GAME_HEIGHT * 4u);
    game_close(game);
    rig_end(t, &rig);
}

TEST(on_the_real_threads_the_hotbar_reaches_the_screen_the_numbers_are_real_and_a_frame_takes_no_heap) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    REQUIRE(game_open(game, &rig_small_game));
    inventory_add(&rig.item->inventory, ITEM_ID_COBBLESTONE, 40u);
    display_buffer_t target = rig_target(t);
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    REQUIRE(game_loop_start(game, nullptr));
    game_debug_send(game, true);

    /* Frames until a picture has gone out with the sim's word on it and the card's clock in the overlay. */
    const int64_t give_up = time_now_ns() + 60000000000;
    int64_t last = time_now_ns();
    ui_hud_state_t state;
    memset(&state, 0, sizeof(state));
    ui_hud_debug_t debug;
    memset(&debug, 0, sizeof(debug));
    while (time_now_ns() < give_up &&
           (state.hotbar[0].count != 40u || debug.fps <= 0.0f32 || debug.stage_ms[UI_HUD_STAGE_SIM_TICK] <= 0.0f32 ||
            debug.stage_ms[UI_HUD_STAGE_GPU_TOTAL] <= 0.0f32 || debug.stage_ms[UI_HUD_STAGE_PRESENT] <= 0.0f32)) {
        const int64_t now = time_now_ns();
        game_walk_send(game, &none, false, false, now);
        CHECK(game_loop_frame(game, &none, (float32_t)time_ns_to_seconds(now - last), &target));
        last = now;
        time_sleep_ns(2000000);
        game_hud_state_read(game, &state);
        game_hud_debug_read(game, &debug);
    }
    CHECK_EQ_U64(state.hotbar[0].item, ITEM_ID_COBBLESTONE);
    CHECK_EQ_U64(state.hotbar[0].count, 40u);
    CHECK(debug.fps > 0.0f32);
    CHECK(debug.stage_ms[UI_HUD_STAGE_SIM_TICK] > 0.0f32);
    CHECK(debug.stage_ms[UI_HUD_STAGE_GPU_TOTAL] > 0.0f32);
    CHECK(debug.stage_ms[UI_HUD_STAGE_HUD] > 0.0f32);
    CHECK(debug.stage_ms[UI_HUD_STAGE_PRESENT] > 0.0f32);

    /* Once running, a frame reserves nothing. */
    heap_counters_t before;
    heap_counters_read(&before);
    for (uint32_t frame = 0u; frame < 60u; frame++) {
        const int64_t now = time_now_ns();
        game_walk_send(game, &none, false, false, now);
        CHECK(game_loop_frame(game, &none, (float32_t)time_ns_to_seconds(now - last), &target));
        last = now;
        time_sleep_ns(2000000);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);

    /* What is on the screen, in a place the overlay and the world do not reach: the first slot's icon. */
    CHECK(game_loop_stop(game));
    const int32_t size = (int32_t)ui_hud_layout(GAME_WIDTH, GAME_HEIGHT).scale;
    const ui_hud_layout_t layout = ui_hud_layout(GAME_WIDTH, GAME_HEIGHT);
    const ui_image_t *const icon = &rig.ui->hud.borrowed_icons[ITEM_ID_COBBLESTONE];
    uint64_t checked = 0u, wrong = 0u;
    const uint32_t *const shown = (const uint32_t *)(const void *)target.owned_map;
    for (int32_t y = 0; y < 8 * size; y++) { /* the top half: the count is at the bottom */
        for (int32_t x = 0; x < 16 * size; x++) {
            const uint32_t source =
                icon->borrowed_pixels[(uint64_t)(y * (int32_t)icon->height / (16 * size)) * icon->width +
                                      (uint64_t)(x * (int32_t)icon->width / (16 * size))];
            if (source >> 24 != 0xFFu) {
                continue;
            }
            const uint64_t at =
                (uint64_t)(layout.hotbar.y + 3 * size + y) * GAME_WIDTH + (uint64_t)(layout.hotbar.x + 3 * size + x);
            wrong += (shown[at] & 0xFFFFFFu) == (source & 0xFFFFFFu) ? 0u : 1u;
            checked++;
        }
    }
    CHECK(checked > 0u);
    CHECK_EQ_U64(wrong, 0u);
    game_close(game);
    rig_end(t, &rig);
}

TEST(f3_on_a_keyboard_through_the_source_turns_the_overlay_on_and_off_once_a_press_and_counts_real_pictures) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    REQUIRE(game_open(game, &rig_small_game));
    /* The fixtures' other helpers, which only their own tests need. */
    (void)fake_record;
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_caps_mouse;
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = world_aligned(t, sizeof(input_t));
    REQUIRE(input_init(input, FAKE_DIRECTORY, &fake_ops, fake));
    game_loop_source_begin(game, input);
    display_buffer_t target = rig_target(t);
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->hud_debug, 0u);

    static const struct input_event press[] = {REPLAY_EVENT(EV_KEY, KEY_F3, 1), REPLAY_SYN};
    static const struct input_event release[] = {REPLAY_EVENT(EV_KEY, KEY_F3, 0), REPLAY_SYN};
    fake_replay(keyboard, press, REPLAY_COUNT(press));
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->hud_debug, 1u);
    /* Held, over many polls, it is still one press. */
    ui_hud_debug_t debug;
    memset(&debug, 0, sizeof(debug));
    const int64_t give_up = time_now_ns() + 60000000000;
    while (time_now_ns() < give_up && debug.fps <= 0.0f32) {
        CHECK(game_loop_source(game, &target));
        time_sleep_ns(2000000);
        game_hud_debug_read(game, &debug);
    }
    CHECK_EQ_U64(game->hud_debug, 1u);
    CHECK(debug.fps > 0.0f32);
    fake_replay(keyboard, release, REPLAY_COUNT(release));
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->hud_debug, 1u);

    /* A tap over inside one poll counts, and turns it off. */
    fake_replay(keyboard, press, REPLAY_COUNT(press));
    fake_replay(keyboard, release, REPLAY_COUNT(release));
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->hud_debug, 0u);

    CHECK(game_loop_stop(game));
    input_deinit(input);
    game_close(game);
    rig_end(t, &rig);
}

/* ---- The built program ---------------------------------------------------------------------------------------------------- */

typedef struct temp_dir_t {
    char path[64];
} temp_dir_t;
static_assert(sizeof(temp_dir_t) == 64);

static bool temp_dir_make(temp_dir_t *dir) {
    snprintf(dir->path, sizeof(dir->path), "/tmp/mc_ui_hud_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void temp_dir_remove(const temp_dir_t *dir, const char *const *names, uint32_t count) {
    char path[256];
    for (uint32_t i = 0u; i < count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir->path, names[i]);
        unlink(path);
    }
    rmdir(dir->path);
}

static void program_run(test_context_t *t, const temp_dir_t *dir, const char *script, test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"320",
                          (char *)"--height",
                          (char *)"240",
                          nullptr};
    test_process_run(t, argv, PROGRAM_TIMEOUT_MS, out);
}

/* A screenshot as RGBA, or nullptr. */
static uint8_t *screenshot_read(test_context_t *t, const temp_dir_t *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = display_png_bytes_needed(GAME_WIDTH, GAME_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        return nullptr;
    }
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(file, length, &info, &error) == PNG_OK) || !CHECK(info.width == GAME_WIDTH)) {
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return nullptr;
    }
    return rgba;
}

/* Wherever an image of ours is solid, the screenshot must be just that colour. Returns how many were not, and adds to checked. */
static uint64_t solid_pixels_differ(const uint8_t *rgba, const ui_image_t *image, ui_rect_t source, ui_rect_t rect,
                                    uint64_t *checked) {
    uint64_t wrong = 0u;
    for (int32_t dy = 0; dy < rect.height; dy++) {
        for (int32_t dx = 0; dx < rect.width; dx++) {
            const int64_t sx = source.x + (int64_t)dx * source.width / rect.width;
            const int64_t sy = source.y + (int64_t)dy * source.height / rect.height;
            const uint32_t color = image->borrowed_pixels[sy * (int64_t)image->width + sx];
            if (color >> 24 != 0xFFu) {
                continue;
            }
            const uint8_t *const p = &rgba[((uint64_t)(rect.y + dy) * GAME_WIDTH + (uint64_t)(rect.x + dx)) * 4u];
            const uint32_t seen = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[2];
            wrong += seen == (color & 0xFFFFFFu) ? 0u : 1u;
            (*checked)++;
        }
    }
    return wrong;
}

TEST(the_program_shows_the_hud_and_f3_changes_only_the_panel) {
    REQUIRE(t->arg_count >= 1);
    temp_dir_t plain, overlay;
    REQUIRE(temp_dir_make(&plain));
    REQUIRE(temp_dir_make(&overlay));
    static const char script_plain[] = "4 screenshot shot.png\n"
                                       "6 end\n";
    static const char script_overlay[] = "1 key f3 down\n"
                                         "2 key f3 up\n"
                                         "4 screenshot shot.png\n"
                                         "6 end\n";
    char path_plain[128], path_overlay[128];
    snprintf(path_plain, sizeof(path_plain), "%s/run.script", plain.path);
    snprintf(path_overlay, sizeof(path_overlay), "%s/run.script", overlay.path);
    REQUIRE(file_write_atomic(path_plain, script_plain, sizeof(script_plain) - 1u) == FILE_OK);
    REQUIRE(file_write_atomic(path_overlay, script_overlay, sizeof(script_overlay) - 1u) == FILE_OK);
    test_process_t run;
    program_run(t, &plain, path_plain, &run);
    REQUIRE(run.started != 0u);
    CHECK(run.timed_out == 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, " 0 bytes live");
    program_run(t, &overlay, path_overlay, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, " 0 bytes live");

    uint8_t *const shot_plain = screenshot_read(t, &plain, "shot.png");
    uint8_t *const shot_overlay = screenshot_read(t, &overlay, "shot.png");
    REQUIRE(shot_plain != nullptr && shot_overlay != nullptr);

    /* The HUD's solid pixels, from the textures, where the README puts them, over whatever the world is there. */
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    const ui_hud_t *const hud = &f->ui.hud;
    const ui_hud_layout_t layout = ui_hud_layout(GAME_WIDTH, GAME_HEIGHT);
    uint64_t checked = 0u, wrong = 0u;
    wrong += solid_pixels_differ(shot_plain, &hud->crosshair, (ui_rect_t){0, 0, 6, 6}, layout.crosshair, &checked);
    const uint64_t crosshair_checked = checked;
    for (int32_t slot = 1; slot < 8; slot++) { /* the slot in hand has another frame over it */
        wrong += solid_pixels_differ(shot_plain, &hud->hotbar, (ui_rect_t){slot * 20 + 3, 0, 14, 2},
                                     (ui_rect_t){layout.hotbar.x + slot * 20 + 3, layout.hotbar.y, 14, 2}, &checked);
    }
    const uint64_t hotbar_checked = checked - crosshair_checked;
    /* Full hearts and drumsticks: where the icon is solid. */
    wrong += solid_pixels_differ(shot_plain, &hud->health, (ui_rect_t){0, 0, 18, 18},
                                 (ui_rect_t){layout.hotbar.x, layout.row_y, 9, 9}, &checked);
    wrong += solid_pixels_differ(shot_plain, &hud->food, (ui_rect_t){0, 0, 18, 18},
                                 (ui_rect_t){layout.hotbar.x + 182 - 9, layout.row_y, 9, 9}, &checked);
    CHECK(crosshair_checked > 0u);
    CHECK(hotbar_checked > 0u);
    CHECK_EQ_U64(wrong, 0u);

    /* The same world and the same HUD, and the overlay only on its panel. */
    const ui_rect_t panel = ui_hud_debug_panel(GAME_WIDTH, GAME_HEIGHT);
    uint64_t outside = 0u, inside = 0u;
    for (int32_t y = 0; y < (int32_t)GAME_HEIGHT; y++) {
        for (int32_t x = 0; x < (int32_t)GAME_WIDTH; x++) {
            const uint64_t at = ((uint64_t)y * GAME_WIDTH + (uint64_t)x) * 4u;
            const bool same = memcmp(&shot_plain[at], &shot_overlay[at], 3u) == 0;
            if (ui_rect_contains(panel, x, y)) {
                inside += same ? 0u : 1u;
            } else {
                outside += same ? 0u : 1u;
            }
        }
    }
    CHECK_EQ_U64(outside, 0u);
    CHECK(inside > 1000u);
    fixture_stop(f);

    static const char *const names[] = {"shot.png", "run.script"};
    temp_dir_remove(&plain, names, 2u);
    temp_dir_remove(&overlay, names, 2u);
}

/* The view on the ground says nothing of how to get up. A red band across the top does, and only while it is so. */
TEST(a_dead_player_is_told_how_to_come_back) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    REQUIRE(game_open(game, &rig_small_game));
    REQUIRE(game_settle(game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_script_state_t *const script = test_alloc(t, sizeof(*script));
    memset(script, 0, sizeof(*script));
    display_headless_tick_t tick;
    memset(&tick, 0, sizeof(tick));
    tick.state = script;
    display_buffer_t target = rig_target(t);
    const uint32_t *const shown = (const uint32_t *)(const void *)target.owned_map;
    /* Inside the band, whatever the scale, and left of any words. */
    const uint64_t band_pixel = (uint64_t)8u * GAME_WIDTH + 1u;
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    REQUIRE(rig_scene_frame(t, &scene, &target));
    CHECK(((shown[band_pixel] >> 16u) & 0xFFu) < 2u * (shown[band_pixel] & 0xFFu));

    player_survival_hurt(&rig.player->survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    CHECK_EQ_U64(player_vitals_read(rig.player).state, PLAYER_SURVIVAL_DEAD);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    CHECK(((shown[band_pixel] >> 16u) & 0xFFu) >= 2u * (shown[band_pixel] & 0xFFu));

    /* Past the wait, a press: up again, and the band has gone. */
    for (uint32_t i = 0u; i <= PLAYER_SURVIVAL_RESPAWN_WAIT_TICKS; i++) {
        REQUIRE(scene.tick(scene.borrowed_context, &tick));
    }
    script->down[BTN_LEFT / 64u] |= 1ull << (BTN_LEFT % 64u);
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    script->down[BTN_LEFT / 64u] &= ~(1ull << (BTN_LEFT % 64u));
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    CHECK_EQ_U64(rig.player->survival.respawned_count, 1u);
    REQUIRE(rig_scene_frame(t, &scene, &target));
    CHECK(((shown[band_pixel] >> 16u) & 0xFFu) < 2u * (shown[band_pixel] & 0xFFu));

    game_close(game);
    rig_end(t, &rig);
}

TEST_MAIN()
