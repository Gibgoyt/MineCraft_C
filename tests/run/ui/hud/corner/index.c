/* ctest run_ui_hud_corner. See README.md. */
#include "harness/index.h"

#include "calendar/index.h"
#include "climate/index.h"
#include "climate/label/index.h"
#include "climate/warning/index.h"
#include "climate/weather/index.h"
#include "climate/heat/index.h"
#include "climate/systems/name/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/corner/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "player/index.h"
#include "png/index.h"
#include "render/index.h"
#include "settings/persist/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GUARD = 8u, /* pixels kept on every side of a canvas, to catch a write outside it */
    GUARD_PIXEL = 0xA5C3E17Bu,
    GOLD_WIDTH = 640u, /* scale 2, so the corner's scale is 1 */
    GOLD_HEIGHT = 480u,
    SMALL_WIDTH = 320u, /* scale 1, and the corner's 1 too */
    SMALL_HEIGHT = 240u,
    LARGE_WIDTH = 1920u, /* scale 4, the corner's 2 */
    LARGE_HEIGHT = 1080u,
    GAME_WIDTH = 320u,
    GAME_HEIGHT = 240u,
};

static const float32_t CORNER_PI = 3.14159265358979323846f32;

/* ---- Sheets: a canvas with a guard band, as tests/run/ui/hud has them ------------------------------------------------ */

typedef struct sheet_t {
    uint32_t *pixels;  /* 0: with the guard */
    uint32_t *visible; /* 8: the canvas's first pixel */
    uint32_t *row;     /* 16 */
    uint64_t pitch;    /* 24: of the canvas, in bytes */
    uint64_t total;    /* 32: pixels with the guard */
    uint32_t width;    /* 40 */
    uint32_t height;   /* 44 */
} sheet_t;
static_assert(sizeof(sheet_t) == 48);

static sheet_t sheet_make(test_context_t *t, uint32_t width, uint32_t height) {
    const uint64_t stride = (uint64_t)width + 2u * GUARD;
    const uint64_t total = stride * ((uint64_t)height + 2u * GUARD);
    uint32_t *const pixels = test_alloc(t, total * sizeof(uint32_t));
    uint32_t *const row = test_alloc(t, UI_CANVAS_ROW_BYTES);
    return (sheet_t){pixels, pixels + stride * GUARD + GUARD, row, stride * sizeof(uint32_t), total, width, height};
}

/* The same gradient tests/run/ui/hud draws its golden pictures over. */
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

static uint32_t sheet_at(const sheet_t *sheet, int32_t x, int32_t y) {
    return sheet->visible[(uint64_t)y * (sheet->pitch / sizeof(uint32_t)) + (uint64_t)x];
}

static bool sheet_canvas(const sheet_t *sheet, ui_canvas_t *canvas) {
    return ui_canvas_start(canvas, (uint8_t *)sheet->visible, sheet->pitch, sheet->width, sheet->height, sheet->row);
}

static uint64_t sheet_guard_changed(const sheet_t *sheet) {
    const uint64_t stride = sheet->pitch / sizeof(uint32_t);
    uint64_t changed = 0u;
    for (uint64_t i = 0u; i < sheet->total; i++) {
        const uint64_t y = i / stride, x = i % stride;
        const bool inside =
            x >= GUARD && x < GUARD + (uint64_t)sheet->width && y >= GUARD && y < GUARD + (uint64_t)sheet->height;
        changed += !inside && sheet->pixels[i] != GUARD_PIXEL ? 1u : 0u;
    }
    return changed;
}

/* The smallest box round every pixel that differs, inclusive; left > right if none. */
typedef struct box_t {
    int32_t left;   /* 0 */
    int32_t top;    /* 4 */
    int32_t right;  /* 8 */
    int32_t bottom; /* 12 */
} box_t;
static_assert(sizeof(box_t) == 16);

static box_t sheets_box(const sheet_t *a, const sheet_t *b) {
    box_t box = {(int32_t)a->width, (int32_t)a->height, -1, -1};
    for (int32_t y = 0; y < (int32_t)a->height; y++) {
        for (int32_t x = 0; x < (int32_t)a->width; x++) {
            if (sheet_at(a, x, y) != sheet_at(b, x, y)) {
                box.left = x < box.left ? x : box.left;
                box.right = x > box.right ? x : box.right;
                box.top = y < box.top ? y : box.top;
                box.bottom = y > box.bottom ? y : box.bottom;
            }
        }
    }
    return box;
}

/* Pixels that differ outside a rect. */
static uint64_t sheets_differ_outside(const sheet_t *a, const sheet_t *b, ui_rect_t rect) {
    uint64_t wrong = 0u;
    for (int32_t y = 0; y < (int32_t)a->height; y++) {
        for (int32_t x = 0; x < (int32_t)a->width; x++) {
            wrong += !ui_rect_contains(rect, x, y) && sheet_at(a, x, y) != sheet_at(b, x, y) ? 1u : 0u;
        }
    }
    return wrong;
}

typedef struct fixture_t {
    ui_t ui;           /* 0 */
    texture_t texture; /* 1152 */
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

/* The golden HUD of tests/run/ui/hud: full bars, one of each kind of thing in the slots, the first slot in hand. */
static void state_golden_full(ui_hud_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->health = 20u;
    state->food = 20u;
    state->selected = 0u;
    state->hotbar[0] = item_stack_make(ITEM_ID_DIRT, 64u);
    state->hotbar[1] = item_stack_make(ITEM_ID_COBBLESTONE, 12u);
    state->hotbar[2] = item_stack_make(ITEM_ID_APPLE, 1u);
    state->hotbar[3] = item_stack_make(ITEM_ID_BOW, 1u);
    state->hotbar[4] = item_stack_make(ITEM_ID_DIAMOND_HELMET, 1u);
    state->hotbar[4].wear = (uint16_t)(item_defs[ITEM_ID_DIAMOND_HELMET].durability / 2u);
    state->hotbar[5] = item_stack_make(ITEM_ID_DIAMOND_SHOVEL, 1u);
    state->hotbar[5].wear = (uint16_t)(item_defs[ITEM_ID_DIAMOND_SHOVEL].durability - 1u);
    state->hotbar[8] = item_stack_make(ITEM_ID_COAL, 9u);
}

static const int32_t GOLDEN_BLOCK[3] = {-1234, 567, -89};
/* "31.2 °C  wind 6.1 m/s from SW": a south-westerly blows toward the north-east, and the eye faces north. */
static const float32_t GOLDEN_YAW = 0.5f32 * 3.14159265358979323846f32;

/* The phase 10 file's corner: its date line, its climate line with the arrow, and a warning if asked. */
static void corner_golden(ui_hud_corner_t *corner, bool warning) {
    ui_hud_corner_clear(corner);
    snprintf(corner->lines[UI_HUD_CORNER_DATE], UI_HUD_CORNER_BYTES,
             "Year 2 \xC2\xB7 Day 214 \xC2\xB7 Mid summer \xC2\xB7 Hurricane season");
    const uint32_t wind = game_corner_wind_pack(6.1f32 * 0.70710678f32, 6.1f32 * 0.70710678f32);
    game_corner_climate_text(312, wind, corner->lines[UI_HUD_CORNER_CLIMATE], UI_HUD_CORNER_BYTES);
    float32_t speed = 0.0f32, toward = 0.0f32;
    game_corner_wind_unpack(wind, &speed, &toward);
    game_corner_arrow(toward, GOLDEN_YAW, corner->arrow);
    corner->arrow_line = (uint8_t)UI_HUD_CORNER_CLIMATE;
    if (warning) {
        snprintf(corner->lines[UI_HUD_CORNER_WARNING], UI_HUD_CORNER_BYTES, "Storm warning: Hurricane Tavela");
        corner->warning_line = (uint8_t)UI_HUD_CORNER_WARNING;
    }
}

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

/* ---- The words, by hand -------------------------------------------------------------------------------------------- */

TEST(the_words_say_what_was_worked_out) {
    char text[GAME_CORNER_TEXT_BYTES];
    /* Master plan 6.5's trade wind at 6 blocks a second, toward (-0.928, -0.371): from E. */
    const uint32_t trade = game_corner_wind_pack(-5.57f32, -2.23f32);
    game_corner_climate_text(140, trade, text, sizeof(text));
    CHECK_STR_EQ(text, "14.0 \xC2\xB0"
                       "C  wind 6.0 m/s from E");
    float32_t speed = 0.0f32, toward = 0.0f32;
    game_corner_wind_unpack(trade, &speed, &toward);
    CHECK_NEAR(speed, 6.0f32, 0.05f32);
    CHECK_NEAR(toward, atan2f(-2.23f32, -5.57f32) + 2.0f32 * CORNER_PI, 0.001f32);
    /* Each of the 8 points, from the middle of its 45 degrees. */
    static const char *const points[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (uint32_t i = 0u; i < 8u; i++) {
        const float32_t from = (float32_t)i * 0.25f32 * CORNER_PI; /* clockwise from north */
        /* Blowing from there is blowing toward the opposite: (u, v) = -(sin from, cos from). */
        const float32_t blow = atan2f(-cosf(from), -sinf(from));
        CHECK_STR_EQ(game_corner_compass(blow), points[i]);
        CHECK_STR_EQ(game_corner_compass(blow + 0.36f32), points[i]);             /* 20.6 degrees on: the same */
        CHECK_STR_EQ(game_corner_compass(blow + 0.42f32), points[(i + 7u) % 8u]); /* 24.1: the point anticlockwise */
    }
    /* Calm, and nothing is ever -0.0. */
    game_corner_climate_text(-0, game_corner_wind_pack(0.3f32, 0.0f32), text, sizeof(text));
    CHECK_STR_EQ(text, "0.0 \xC2\xB0"
                       "C  wind calm");
    game_corner_climate_text(-5, game_corner_wind_pack(0.0f32, 0.0f32), text, sizeof(text));
    CHECK_STR_EQ(text, "-0.5 \xC2\xB0"
                       "C  wind calm");
    game_corner_climate_text(-123, game_corner_wind_pack(0.0f32, -12.0f32), text, sizeof(text));
    CHECK_STR_EQ(text, "-12.3 \xC2\xB0"
                       "C  wind 12.0 m/s from N");
    /* The warning's word and its text. */
    climate_warning_t warning = {.system_id = 99u,
                                 .chance = 0.6f64,
                                 .name_seed = 12345u,
                                 .kind = CLIMATE_WARNING_NAMED,
                                 .hurricane = 1u,
                                 ._pad0 = {0}};
    char name[CLIMATE_SYSTEM_NAME_BYTES];
    climate_system_name(12345u, name);
    char want[GAME_CORNER_TEXT_BYTES];
    snprintf(want, sizeof(want), "Storm warning: Hurricane %s", name);
    game_corner_warning_text(game_corner_warning_pack(&warning), text, sizeof(text));
    CHECK_STR_EQ(text, want);
    warning.hurricane = 0u;
    snprintf(want, sizeof(want), "Storm warning: Tropical storm %s", name);
    game_corner_warning_text(game_corner_warning_pack(&warning), text, sizeof(text));
    CHECK_STR_EQ(text, want);
    warning.kind = CLIMATE_WARNING_HURRICANE_LIKELY;
    game_corner_warning_text(game_corner_warning_pack(&warning), text, sizeof(text));
    CHECK_STR_EQ(text, "Storm warning: hurricane likely");
    warning.kind = CLIMATE_WARNING_STORM;
    game_corner_warning_text(game_corner_warning_pack(&warning), text, sizeof(text));
    CHECK_STR_EQ(text, "Storm warning: storm");
    warning.kind = CLIMATE_WARNING_NONE;
    CHECK_EQ_U64(game_corner_warning_pack(&warning), 0u);
    game_corner_warning_text(0u, text, sizeof(text));
    CHECK_STR_EQ(text, "");
}

/* ---- The corner, from plain values ------------------------------------------------------------------------------------ */

/* The panels of a drawn corner, top to bottom, found from its pixels against the background: a row of the corner's column
 * that changed. */
TEST(the_corner_sits_top_right_as_wide_as_its_text) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    static const uint32_t sizes[3][3] = {
        {SMALL_WIDTH, SMALL_HEIGHT, 1u}, {GOLD_WIDTH, GOLD_HEIGHT, 1u}, {LARGE_WIDTH, LARGE_HEIGHT, 2u}};
    for (uint32_t z = 0u; z < 3u; z++) {
        const uint32_t width = sizes[z][0], height = sizes[z][1], s = sizes[z][2];
        const int32_t m = 2 * (int32_t)s;
        sheet_t clean = sheet_make(t, width, height);
        sheet_t drawn = sheet_make(t, width, height);
        sheet_clear(&clean);
        ui_hud_corner_t corner;
        corner_golden(&corner, true);
        int32_t top = m;
        for (uint32_t line = UI_HUD_CORNER_DATE; line < UI_HUD_CORNER_LINES; line++) {
            /* Each panel alone: right edge at width - m, as wide as its text and 2 m (and the arrow's room). */
            ui_hud_corner_t one;
            ui_hud_corner_clear(&one);
            memcpy(one.lines[line], corner.lines[line], UI_HUD_CORNER_BYTES);
            one.arrow_line = corner.arrow_line == line ? corner.arrow_line : (uint8_t)UI_HUD_CORNER_NONE;
            memcpy(one.arrow, corner.arrow, sizeof(one.arrow));
            one.warning_line = corner.warning_line == line ? corner.warning_line : (uint8_t)UI_HUD_CORNER_NONE;
            sheet_clear(&drawn);
            ui_canvas_t canvas;
            REQUIRE(sheet_canvas(&drawn, &canvas));
            const ui_rect_t rect = ui_hud_corner_draw(&canvas, &f->ui.font, &one, (ui_rect_t){0, 0, 0, 0}, false);
            const ui_text_size_t size = ui_font_measure(&f->ui.font, corner.lines[line], s);
            const int32_t extra = line == UI_HUD_CORNER_CLIMATE ? (int32_t)((3u + 1u + 9u) * s) : 0;
            const box_t box = sheets_box(&clean, &drawn);
            const bool good = box.right == (int32_t)width - m - 1 &&
                              box.left == (int32_t)width - m - ((int32_t)size.width + 2 * m + extra) && box.top == m &&
                              box.bottom == m + (int32_t)size.height + 2 * m - 1 && rect.x == box.left &&
                              rect.width == box.right - box.left + 1 && sheet_guard_changed(&drawn) == 0u;
            if (!good) {
                printf("line %" PRIu32 " at %" PRIu32 " by %" PRIu32 ": box %d %d %d %d\n", line, width, height,
                       box.left, box.top, box.right, box.bottom);
            }
            CHECK(good);
        }
        /* All three: touching, each under the last, no gap. */
        sheet_clear(&drawn);
        ui_canvas_t canvas;
        REQUIRE(sheet_canvas(&drawn, &canvas));
        const ui_rect_t whole = ui_hud_corner_draw(&canvas, &f->ui.font, &corner, (ui_rect_t){0, 0, 0, 0}, false);
        const int32_t panel = (int32_t)ui_font_measure(&f->ui.font, "x", s).height + 2 * m;
        CHECK_EQ_I64(whole.y, top);
        CHECK_EQ_I64(whole.height, 3 * panel);
        CHECK_EQ_I64(whole.x + whole.width, (int32_t)width - m);
        /* The rows between panels are covered at the right edge all the way down. */
        for (int32_t y = top; y < top + 3 * panel; y++) {
            CHECK(sheet_at(&drawn, (int32_t)width - m - 1, y) != sheet_at(&clean, (int32_t)width - m - 1, y));
        }
        CHECK_EQ_U64(sheet_at(&drawn, (int32_t)width - m - 1, top + 3 * panel),
                     sheet_at(&clean, (int32_t)width - m - 1, top + 3 * panel));
        /* The warning's panel is red, the others dark. */
        const uint32_t warning_pixel = sheet_at(&drawn, (int32_t)width - m - 2, top + 2 * panel + 1);
        const uint32_t date_pixel = sheet_at(&drawn, (int32_t)width - m - 2, top + 1);
        CHECK(((warning_pixel >> 16) & 0xFFu) > ((warning_pixel >> 8) & 0xFFu) + 40u);
        CHECK(((date_pixel >> 16) & 0xFFu) < ((sheet_at(&clean, (int32_t)width - m - 2, top + 1) >> 16) & 0xFFu));
        top = m;
    }
    fixture_stop(f);
}

TEST(an_empty_line_takes_no_room_and_slot_0_is_never_drawn) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    sheet_t clean = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t a = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t b = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_clear(&clean);
    ui_canvas_t canvas;
    ui_hud_corner_t corner;
    corner_golden(&corner, false);
    /* Only the climate line: it is at the top. */
    ui_hud_corner_t only;
    ui_hud_corner_clear(&only);
    memcpy(only.lines[UI_HUD_CORNER_CLIMATE], corner.lines[UI_HUD_CORNER_CLIMATE], UI_HUD_CORNER_BYTES);
    memcpy(only.arrow, corner.arrow, sizeof(only.arrow));
    only.arrow_line = (uint8_t)UI_HUD_CORNER_CLIMATE;
    sheet_clear(&a);
    REQUIRE(sheet_canvas(&a, &canvas));
    const ui_rect_t rect = ui_hud_corner_draw(&canvas, &f->ui.font, &only, (ui_rect_t){0, 0, 0, 0}, false);
    CHECK_EQ_I64(rect.y, 2);
    CHECK_EQ_I64(sheets_box(&clean, &a).top, 2);
    /* Slot 0 set to a town's name changes not one pixel in this plan. */
    snprintf(only.lines[UI_HUD_CORNER_SETTLEMENT], UI_HUD_CORNER_BYTES, "Testville");
    sheet_clear(&b);
    REQUIRE(sheet_canvas(&b, &canvas));
    const ui_rect_t again = ui_hud_corner_draw(&canvas, &f->ui.font, &only, (ui_rect_t){0, 0, 0, 0}, false);
    CHECK_EQ_I64(sheets_box(&a, &b).right, -1);
    CHECK(memcmp(&rect, &again, sizeof(rect)) == 0);
    /* Nothing at all: nothing drawn, an empty rect. */
    ui_hud_corner_t none;
    ui_hud_corner_clear(&none);
    snprintf(none.lines[UI_HUD_CORNER_SETTLEMENT], UI_HUD_CORNER_BYTES, "Testville");
    sheet_clear(&b);
    REQUIRE(sheet_canvas(&b, &canvas));
    const ui_rect_t empty = ui_hud_corner_draw(&canvas, &f->ui.font, &none, (ui_rect_t){0, 0, 0, 0}, false);
    CHECK_EQ_I64(empty.width, 0);
    CHECK_EQ_I64(sheets_box(&clean, &b).right, -1);
    fixture_stop(f);
}

/* Whether any pixel inside both rects changed from clean in both a and b. */
static bool rects_meet(ui_rect_t a, ui_rect_t b) {
    const ui_rect_t meet = ui_rect_intersect(a, b);
    return meet.width > 0 && meet.height > 0;
}

TEST(the_corner_goes_below_a_notice_and_below_a_top_left_panel_it_would_cross) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    ui_hud_corner_t corner;
    corner_golden(&corner, true);
    ui_canvas_t canvas;
    /* With a notice: the first panel's top is m and the band's height below where it would be. */
    sheet_t clean = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t band = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t below = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_clear(&clean);
    sheet_clear(&band);
    REQUIRE(sheet_canvas(&band, &canvas));
    ui_hud_notice_draw(&canvas, &f->ui.font, "You died.");
    const box_t notice = sheets_box(&clean, &band);
    sheet_clear(&below);
    REQUIRE(sheet_canvas(&below, &canvas));
    const ui_rect_t moved = ui_hud_corner_draw(&canvas, &f->ui.font, &corner, (ui_rect_t){0, 0, 0, 0}, true);
    CHECK_EQ_I64(moved.y, notice.bottom + 1);
    CHECK_EQ_I64(sheets_box(&clean, &below).top, notice.bottom + 1);

    /* At 320 by 240 the date line reaches across to the coordinates and to F3: the corner goes a margin below either. At 640
     * by 480 it does not, and stays at the top. */
    static const uint32_t sizes[2][2] = {{SMALL_WIDTH, SMALL_HEIGHT}, {GOLD_WIDTH, GOLD_HEIGHT}};
    for (uint32_t z = 0u; z < 2u; z++) {
        const uint32_t width = sizes[z][0], height = sizes[z][1];
        sheet_t sheet = sheet_make(t, width, height);
        for (uint32_t which = 0u; which < 2u; which++) {
            sheet_clear(&sheet);
            REQUIRE(sheet_canvas(&sheet, &canvas));
            ui_rect_t avoid;
            if (which == 0u) {
                avoid = ui_hud_coordinates_panel(&canvas, &f->ui.font, GOLDEN_BLOCK, false);
            } else {
                avoid = ui_hud_debug_panel(width, height);
            }
            const ui_rect_t free_rect =
                ui_hud_corner_draw(&canvas, &f->ui.font, &corner, (ui_rect_t){0, 0, 0, 0}, false);
            sheet_clear(&sheet);
            REQUIRE(sheet_canvas(&sheet, &canvas));
            const ui_rect_t rect = ui_hud_corner_draw(&canvas, &f->ui.font, &corner, avoid, false);
            const bool crosses = rects_meet(free_rect, avoid);
            printf("%" PRIu32 " by %" PRIu32 ", %s: the corner %s, top %d\n", width, height,
                   which == 0u ? "coordinates" : "F3", crosses ? "would cross" : "is clear", rect.y);
            CHECK_EQ_U64(crosses ? 1u : 0u, z == 0u ? 1u : 0u);
            if (crosses) {
                CHECK_EQ_I64(rect.y, avoid.y + avoid.height + 2);
            } else {
                CHECK_EQ_I64(rect.y, 2);
            }
            CHECK(!rects_meet(rect, avoid));
            CHECK_EQ_U64(sheet_guard_changed(&sheet), 0u);
        }
    }
    fixture_stop(f);
}

TEST(the_arrow_points_as_worked_by_hand) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    /* Facing north with the trade wind: (-0.928, 0.371), left and a little down. */
    float32_t arrow[2];
    const float32_t trade = atan2f(-0.371f32, -0.928f32);
    game_corner_arrow(trade, 0.5f32 * CORNER_PI, arrow);
    CHECK_NEAR(arrow[0], -0.928f32, 0.001f32);
    CHECK_NEAR(arrow[1], 0.371f32, 0.001f32);
    /* Facing east with a wind blowing east: straight up. */
    float32_t up[2];
    game_corner_arrow(0.0f32, 0.0f32, up);
    CHECK_NEAR(up[0], 0.0f32, 1.0e-6f32);
    CHECK_NEAR(up[1], -1.0f32, 1.0e-6f32);
    /* Its ends on the screen, at both corner scales: the tip left of and below the tail, within a pixel of the vector times
     * the length; and the tip's pixel drawn white. */
    for (uint32_t s = 1u; s <= 2u; s++) {
        const int32_t side = (int32_t)(UI_FONT_LINE * s);
        const ui_rect_t square = {100, 50, side, side};
        int32_t tail[2], tip[2];
        ui_hud_corner_arrow_ends(square, arrow, s, tail, tip);
        const float32_t length = (float32_t)side - 2.0f32 * (float32_t)s;
        printf("scale %" PRIu32 ": tail %d %d, tip %d %d\n", s, tail[0], tail[1], tip[0], tip[1]);
        CHECK(tip[0] < tail[0] && tip[1] > tail[1]);
        CHECK(fabsf((float32_t)(tip[0] - tail[0]) - arrow[0] * length) <= 1.0f32);
        CHECK(fabsf((float32_t)(tip[1] - tail[1]) - arrow[1] * length) <= 1.0f32);
    }
    /* Drawn: on a corner with only that line, the arrow's tip is white. */
    sheet_t sheet = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_clear(&sheet);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    ui_hud_corner_t corner;
    ui_hud_corner_clear(&corner);
    snprintf(corner.lines[UI_HUD_CORNER_CLIMATE], UI_HUD_CORNER_BYTES,
             "14.0 \xC2\xB0"
             "C  wind 6.0 m/s from E");
    memcpy(corner.arrow, arrow, sizeof(arrow));
    corner.arrow_line = (uint8_t)UI_HUD_CORNER_CLIMATE;
    const ui_rect_t rect = ui_hud_corner_draw(&canvas, &f->ui.font, &corner, (ui_rect_t){0, 0, 0, 0}, false);
    const int32_t side = (int32_t)UI_FONT_LINE;
    const ui_rect_t square = {rect.x + rect.width - 2 - side, rect.y + (rect.height - side) / 2, side, side};
    int32_t tail[2], tip[2];
    ui_hud_corner_arrow_ends(square, arrow, 1u, tail, tip);
    CHECK_EQ_U64(sheet_at(&sheet, tip[0], tip[1]) & 0xFFFFFFu, 0xFFFFFFu);
    CHECK_EQ_U64(sheet_at(&sheet, tail[0], tail[1]) & 0xFFFFFFu, 0xFFFFFFu);
    fixture_stop(f);
}

TEST(golden_pictures_match) {
    fixture_t *const f = fixture_start(t);
    REQUIRE(f != nullptr);
    ui_hud_state_t state;
    state_golden_full(&state);
    ui_canvas_t canvas;
    ui_hud_corner_t corner;
    sheet_t large = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);

    /* On: the date, the climate line with its arrow, and the coordinates, as game_hud_compose draws them. */
    corner_golden(&corner, false);
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    ui_hud_coordinates_draw(&canvas, &f->ui.font, GOLDEN_BLOCK, false);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner,
                             ui_hud_coordinates_panel(&canvas, &f->ui.font, GOLDEN_BLOCK, false), false);
    golden_compare(t, "tests/run/ui/hud/fixtures/corner_on_640x480.png", &large);

    corner_golden(&corner, true);
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    ui_hud_coordinates_draw(&canvas, &f->ui.font, GOLDEN_BLOCK, false);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner,
                             ui_hud_coordinates_panel(&canvas, &f->ui.font, GOLDEN_BLOCK, false), false);
    golden_compare(t, "tests/run/ui/hud/fixtures/corner_warning_640x480.png", &large);

    /* All three at 320 by 240, below the coordinates. */
    sheet_t small = sheet_make(t, SMALL_WIDTH, SMALL_HEIGHT);
    sheet_clear(&small);
    REQUIRE(sheet_canvas(&small, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    ui_hud_coordinates_draw(&canvas, &f->ui.font, GOLDEN_BLOCK, false);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner,
                             ui_hud_coordinates_panel(&canvas, &f->ui.font, GOLDEN_BLOCK, false), false);
    golden_compare(t, "tests/run/ui/hud/fixtures/corner_320x240.png", &small);

    /* With F3 up: the overlay's panel instead of the coordinates, drawn after the corner. */
    ui_hud_debug_t debug;
    memset(&debug, 0, sizeof(debug));
    debug.fps = 143.9f32;
    memcpy(debug.block, GOLDEN_BLOCK, sizeof(debug.block));
    debug.latitude_deg = 1.33f32;
    debug.sun_elevation_deg = 30.0f32;
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner, ui_hud_debug_panel(GOLD_WIDTH, GOLD_HEIGHT), false);
    ui_hud_debug_draw(&canvas, &f->ui.font, &debug);
    golden_compare(t, "tests/run/ui/hud/fixtures/corner_debug_640x480.png", &large);

    /* Off: no climate line and no warning is phase 01's date line, byte for byte, and with the date off too, the coordinates'
     * golden. */
    ui_hud_corner_clear(&corner);
    snprintf(corner.lines[UI_HUD_CORNER_DATE], UI_HUD_CORNER_BYTES, "Year 1, Day 80, Early spring");
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner, (ui_rect_t){0, 0, 0, 0}, false);
    golden_compare(t, "tests/run/ui/hud/fixtures/date_640x480.png", &large);
    ui_hud_corner_clear(&corner);
    sheet_clear(&large);
    REQUIRE(sheet_canvas(&large, &canvas));
    ui_hud_draw(&f->ui.hud, &canvas, &f->ui.font, &state);
    ui_hud_coordinates_draw(&canvas, &f->ui.font, GOLDEN_BLOCK, false);
    (void)ui_hud_corner_draw(&canvas, &f->ui.font, &corner,
                             ui_hud_coordinates_panel(&canvas, &f->ui.font, GOLDEN_BLOCK, false), false);
    golden_compare(t, "tests/run/ui/hud/fixtures/coordinates_640x480.png", &large);
    fixture_stop(f);
}

/* ---- The real game, one thread --------------------------------------------------------------------------------------- */

static const uint64_t CORNER_SEED = 2198175988018515243ull;

static void *world_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

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
    climate_t *climate;          /* 96 */
    arena_t *arena;              /* 104: the test's own scratch for the climate's answers */
} rig_t;
static_assert(sizeof(rig_t) == 112);

enum : uint64_t { RIG_ARENA_BYTES = 32ull << 20 };
/* climate_weather_at reads the tiles a thunderstorm cell could come from, 4.2 km round (climate_systems_reach of a cell); a game
 * keeps 2 km past its view, and a census 6 km (Issues found in the phase 10 file). The rig builds what a census would. */
#define RIG_WEATHER_REACH 4400
#define RIG_START_X 8
#define RIG_START_Y 8

static bool rig_begin(test_context_t *t, rig_t *rig) {
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
    rig->climate = world_aligned(t, sizeof(climate_t));
    rig->arena = world_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "test_corner", world_aligned(t, RIG_ARENA_BYTES), RIG_ARENA_BYTES);
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, CORNER_SEED)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(item_init(rig->item)) || !CHECK(ui_init(rig->ui, rig->texture)) || !CHECK(climate_init(rig->climate)) ||
        !CHECK(climate_open(rig->climate, rig->terrain)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    /* Round where the world starts looking for its body's place (GAME_WORLD_MIDDLE), out past where it can find one. */
    climate_prepare_area(rig->climate, rig->job, RIG_START_X - RIG_WEATHER_REACH - 512,
                         RIG_START_Y - RIG_WEATHER_REACH - 512, RIG_START_X + RIG_WEATHER_REACH + 512,
                         RIG_START_Y + RIG_WEATHER_REACH + 512, nullptr, nullptr);
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    game_item_set(rig->game, rig->item);
    game_ui_set(rig->game, rig->ui);
    game_climate_set(rig->game, rig->climate);
    return true;
}

static void rig_end(test_context_t *t, rig_t *rig) {
    arena_deinit(rig->arena); /* the sanitize build poisons what it holds, and the test arena outlives this */
    game_deinit(rig->game);
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
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

static const game_config_t rig_game = {.render_distance = 4u,
                                       .width = GAME_WIDTH,
                                       .height = GAME_HEIGHT,
                                       .climate_hud = SETTINGS_CLIMATE_HUD_ALL,
                                       .show_coordinates = 1u,
                                       .show_date = 1u};

/* Opens the world and works out its climate round the start, as a loading screen does. */
static bool rig_open(test_context_t *t, rig_t *rig) {
    if (!CHECK(game_open(rig->game, &rig_game))) {
        return false;
    }
    while (!game_open_climate_step(rig->game)) {
        time_sleep_ns(1000000);
    }
    return true;
}

/* A place in hurricane season, from phase 05's normals: a cell of the square round (x0, y0) whose stored season holds on day d. */
static bool rig_find_hurricane_season(rig_t *rig, float64_t x0, float64_t y0, float32_t d, float64_t out[2]) {
    for (int32_t j = -8; j <= 8; j++) {
        for (int32_t i = -8; i <= 8; i++) {
            const float64_t x = x0 + 128.0f64 * (float64_t)i + 32.0f64, y = y0 + 128.0f64 * (float64_t)j + 32.0f64;
            climate_normals_at_t normals;
            if (climate_normals_at(rig->climate, x, y, nanf(""), d, &normals) &&
                (normals.hurricane_season != 0u || normals.hurricane_s > CLIMATE_HURRICANE_SEASON_S)) {
                out[0] = x;
                out[1] = y;
                return true;
            }
        }
    }
    return false;
}

/* T_HUD by the test's own hand, from phase 07's weather now and phase 03's ground at the place, at the feet's height. */
static float64_t rig_expected_temperature(rig_t *rig, const float64_t feet[3]) {
    const game_t *const game = rig->game;
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const calendar_date_t date = calendar_date_of(game->day_zero, clock);
    const float64_t t = climate_weather_time_of_clock(game->day_zero, clock);
    const climate_week_t *const last_week = climate_week_store_cell(
        climate_week_store(rig->climate), (int32_t)floor(feet[0] / (float64_t)CLIMATE_CELL_BLOCKS),
        (int32_t)floor(feet[1] / (float64_t)CLIMATE_CELL_BLOCKS));
    climate_now_t now;
    climate_normals_at_t normals;
    arena_reset(rig->arena);
    if (!climate_weather_at(rig->climate, feet[0], feet[1], (float64_t)nanf(""), t, last_week, rig->arena, &now) ||
        !climate_normals_at(rig->climate, feet[0], feet[1], nanf(""), (float32_t)date.day_of_year, &normals)) {
        return nan("");
    }
    return (float64_t)now.temperature_c - 0.012f64 * (feet[2] - (float64_t)MC_WORLD_Z_SEA - (float64_t)normals.z);
}

/* The temperature the climate line says, in tenths. */
static int32_t rig_shown_temperature(const ui_hud_corner_t *corner) {
    const char *const text = corner->lines[UI_HUD_CORNER_CLIMATE];
    const bool below = text[0] == '-';
    uint32_t whole = 0u, tenth = 0u;
    if (sscanf(below ? text + 1 : text, "%" SCNu32 ".%" SCNu32, &whole, &tenth) != 2) {
        return INT32_MIN;
    }
    const int32_t tenths = (int32_t)(whole * 10u + tenth);
    return below ? -tenths : tenths;
}

TEST(in_the_real_game_the_corner_shows_what_the_sim_worked_out) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(rig_open(t, &rig));
    /* Before any tick, the first picture already has the words: game_open_climate_step published them at the start. */
    ui_hud_corner_t corner;
    game_hud_corner_read(game, &corner);
    printf("at the start: '%s' / '%s'\n", corner.lines[UI_HUD_CORNER_DATE], corner.lines[UI_HUD_CORNER_CLIMATE]);
    CHECK(corner.lines[UI_HUD_CORNER_CLIMATE][0] != '\0');
    CHECK_CONTAINS(corner.lines[UI_HUD_CORNER_DATE], "Year 1 \xC2\xB7 Day 80 \xC2\xB7 ");

    /* Day 250 of the year (clock day 171 from a new world's day 79), on a warm sea 15 degrees north. */
    game_clock_set(game, 171u, 0.5f64);
    const float64_t y0 = 6383.0f64, x0 = 0.0f64;
    climate_prepare_area(rig.climate, rig.job, (int32_t)x0 - 1100, (int32_t)y0 - 1100, (int32_t)x0 + 1100,
                         (int32_t)y0 + 1100, nullptr, nullptr);
    float64_t place[2] = {0.0f64, 0.0f64}; /* set, so that tsan's -Werror=maybe-uninitialized sees it */
    REQUIRE(rig_find_hurricane_season(&rig, x0, y0, 250.0f32, place));
    climate_prepare_area(rig.climate, rig.job, (int32_t)place[0] - RIG_WEATHER_REACH,
                         (int32_t)place[1] - RIG_WEATHER_REACH, (int32_t)place[0] + RIG_WEATHER_REACH,
                         (int32_t)place[1] + RIG_WEATHER_REACH, nullptr, nullptr);
    printf("hurricane season at %.0f, %.0f\n", (libc_double_t)place[0], (libc_double_t)place[1]);
    player_spawn(rig.player, place[0], place[1], 40.0f64);
    /* The opening worked it out as tick 0 did, so the next is 20 ticks on, the 21st tick to run. */
    for (uint32_t i = 0u; i <= GAME_CORNER_CLIMATE_TICKS; i++) {
        game_sim_tick(game, (int64_t)(game->tick_count + 1u) * 50000000);
    }
    game_hud_corner_read(game, &corner);
    printf("after %u ticks: '%s' / '%s' / '%s'\n", GAME_CORNER_CLIMATE_TICKS + 1u, corner.lines[UI_HUD_CORNER_DATE],
           corner.lines[UI_HUD_CORNER_CLIMATE], corner.lines[UI_HUD_CORNER_WARNING]);
    CHECK_CONTAINS(corner.lines[UI_HUD_CORNER_DATE], "Day 251"); /* d = 250, shown d + 1 */
    CHECK_CONTAINS(corner.lines[UI_HUD_CORNER_DATE], "Hurricane season");
    /* The temperature the sim worked out, against the test's own hand, at the same feet and moment. */
    float64_t feet[3];
    memcpy(feet, rig.player->movement.position, sizeof(feet));
    const float64_t expected = rig_expected_temperature(&rig, feet);
    const int32_t shown = rig_shown_temperature(&corner);
    printf("shown %d tenths, worked out by hand %.3f\n", shown, (libc_double_t)expected);
    CHECK(fabs((float64_t)shown / 10.0f64 - expected) <= 0.051f64);
    /* Up 500 blocks: 6.0 degrees colder, within a tenth. */
    player_spawn(rig.player, feet[0], feet[1], feet[2] + 500.0f64);
    game_hud_climate_publish(game, true);
    game_hud_corner_read(game, &corner);
    const int32_t high = rig_shown_temperature(&corner);
    printf("500 blocks up: '%s'\n", corner.lines[UI_HUD_CORNER_CLIMATE]);
    CHECK(abs((shown - high) - 60) <= 1);
    /* The arrow follows main's eye, not the sim: turning round turns it over. */
    if (corner.arrow_line == UI_HUD_CORNER_CLIMATE) {
        const float32_t before[2] = {corner.arrow[0], corner.arrow[1]};
        game->camera.yaw += CORNER_PI;
        game_hud_corner_read(game, &corner);
        CHECK_NEAR(corner.arrow[0], -before[0], 1.0e-4f32);
        CHECK_NEAR(corner.arrow[1], -before[1], 1.0e-4f32);
    }
    game_close(game);
    rig_end(t, &rig);
}

/* The corner as the game would draw it, over the golden HUD, with the config's bits. */
static void rig_corner_sheet(const rig_t *rig, sheet_t *sheet, uint8_t bits, ui_rect_t *out_rect) {
    game_t *const game = rig->game;
    game->config.climate_hud = bits;
    ui_hud_corner_t corner;
    game_hud_corner_read(game, &corner);
    ui_hud_state_t state;
    state_golden_full(&state);
    sheet_clear(sheet);
    ui_canvas_t canvas;
    (void)sheet_canvas(sheet, &canvas);
    ui_hud_draw(&rig->ui->hud, &canvas, &rig->ui->font, &state);
    ui_hud_coordinates_draw(&canvas, &rig->ui->font, GOLDEN_BLOCK, false);
    *out_rect = ui_hud_corner_draw(&canvas, &rig->ui->font, &corner,
                                   ui_hud_coordinates_panel(&canvas, &rig->ui->font, GOLDEN_BLOCK, false), false);
}

TEST(the_settings_turn_each_line_off_and_on_and_nothing_else_moves) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(rig_open(t, &rig));
    /* A warning to show, as the sim would have published it. */
    const climate_warning_t named = {.system_id = 5u,
                                     .chance = 0.6f64,
                                     .name_seed = 777u,
                                     .kind = CLIMATE_WARNING_NAMED,
                                     .hurricane = 1u,
                                     ._pad0 = {0}};
    atomic_store_explicit(&game_corner_of(game)->hud_warning, game_corner_warning_pack(&named), memory_order_relaxed);
    sheet_t all = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    sheet_t some = sheet_make(t, GOLD_WIDTH, GOLD_HEIGHT);
    ui_rect_t all_rect, some_rect;
    rig_corner_sheet(&rig, &all, SETTINGS_CLIMATE_HUD_ALL, &all_rect);
    /* The warning off: its line goes, and outside the corner nothing changes. */
    rig_corner_sheet(&rig, &some, SETTINGS_CLIMATE_HUD_LINE, &some_rect);
    CHECK_EQ_U64(sheets_differ_outside(&all, &some, all_rect), 0u);
    CHECK(some_rect.height < all_rect.height);
    /* The climate line off: the warning moves up under the date, and still nothing outside the corner. */
    rig_corner_sheet(&rig, &some, SETTINGS_CLIMATE_HUD_WARNINGS, &some_rect);
    CHECK_EQ_U64(sheets_differ_outside(&all, &some, all_rect), 0u);
    CHECK(some_rect.height < all_rect.height);
    /* Both off: only the date line. */
    rig_corner_sheet(&rig, &some, 0u, &some_rect);
    CHECK_EQ_U64(sheets_differ_outside(&all, &some, all_rect), 0u);
    CHECK_EQ_I64(some_rect.height, all_rect.height / 3);
    /* And back on: the same pixels as before. */
    rig_corner_sheet(&rig, &some, SETTINGS_CLIMATE_HUD_ALL, &some_rect);
    CHECK_EQ_I64(sheets_box(&all, &some).right, -1);
    /* The date off too: nothing at all in the corner. */
    game->config.show_date = 0u;
    rig_corner_sheet(&rig, &some, 0u, &some_rect);
    CHECK_EQ_I64(some_rect.width, 0);
    game->config.show_date = 1u;
    game_close(game);
    rig_end(t, &rig);
}

TEST(no_heap_in_200_frames_with_the_corner_and_a_warning) {
    rig_t rig;
    REQUIRE(rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(rig_open(t, &rig));
    REQUIRE(game_settle(game));
    const climate_warning_t named = {.system_id = 5u,
                                     .chance = 0.6f64,
                                     .name_seed = 777u,
                                     .kind = CLIMATE_WARNING_NAMED,
                                     .hurricane = 1u,
                                     ._pad0 = {0}};
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_script_state_t *const script = test_alloc(t, sizeof(*script));
    memset(script, 0, sizeof(*script));
    display_headless_tick_t tick;
    memset(&tick, 0, sizeof(tick));
    tick.state = script;
    const uint64_t bytes = post_xrgb_bytes(GAME_WIDTH, GAME_HEIGHT);
    display_buffer_t target = {.owned_map = world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = GAME_WIDTH * 4u,
                               .width = GAME_WIDTH,
                               .height = GAME_HEIGHT};
    /* A first frame, then 200 with the corner's three lines up, ticking each. */
    REQUIRE(scene.tick(scene.borrowed_context, &tick));
    atomic_store_explicit(&game_corner_of(game)->hud_warning, game_corner_warning_pack(&named), memory_order_relaxed);
    REQUIRE(scene.draw(scene.borrowed_context, &tick, &target));
    heap_counters_t first;
    heap_counters_read(&first);
    for (uint32_t frame = 0u; frame < 200u; frame++) {
        REQUIRE(scene.tick(scene.borrowed_context, &tick));
        atomic_store_explicit(&game_corner_of(game)->hud_warning, game_corner_warning_pack(&named),
                              memory_order_relaxed);
        REQUIRE(scene.draw(scene.borrowed_context, &tick, &target));
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, first.reserve_calls);
    CHECK_EQ_U64(after.release_calls, first.release_calls);
    /* The corner was up: its lines are there to read. */
    ui_hud_corner_t corner;
    game_hud_corner_read(game, &corner);
    CHECK(corner.lines[UI_HUD_CORNER_CLIMATE][0] != '\0');
    CHECK_CONTAINS(corner.lines[UI_HUD_CORNER_WARNING], "Storm warning: Hurricane ");
    game_close(game);
    rig_end(t, &rig);
}

TEST_MAIN()
