/* ctest run_ui_font. See README.md. */
#include "harness/index.h"

#include "display/headless/png/index.h"
#include "heap/index.h"
#include "simd/reference/index.h"
#include "texture/index.h"
#include "ui/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GUARD = 8u, /* pixels kept on every side of a canvas, to catch a write outside it */
    GUARD_PIXEL = 0xA5C3E17Bu,
    SHEET_WIDTH = 640u, /* scale 2: 640 / 320 and 480 / 240 */
    SHEET_HEIGHT = 480u,
    SHEET_SCALE = 2u,
};

/* A canvas with a guard band around it, in the test arena. */
typedef struct test_sheet_t {
    uint32_t *pixels;  /* with the guard */
    uint32_t *visible; /* the canvas's first pixel */
    uint32_t *row;
    uint64_t pitch; /* of the canvas, in bytes */
    uint64_t total; /* pixels with the guard */
    uint32_t width;
    uint32_t height;
} test_sheet_t;
static_assert(sizeof(test_sheet_t) == 48);

static test_sheet_t sheet_make(test_context_t *t, uint32_t width, uint32_t height, uint32_t fill) {
    const uint64_t stride = (uint64_t)width + 2u * GUARD;
    const uint64_t total = stride * ((uint64_t)height + 2u * GUARD);
    uint32_t *const pixels = test_alloc(t, total * sizeof(uint32_t));
    for (uint64_t i = 0u; i < total; i++) {
        pixels[i] = GUARD_PIXEL;
    }
    uint32_t *const visible = pixels + stride * GUARD + GUARD;
    for (uint32_t y = 0u; y < height; y++) {
        for (uint32_t x = 0u; x < width; x++) {
            visible[stride * y + x] = fill;
        }
    }
    uint32_t *const row = test_alloc(t, UI_CANVAS_ROW_BYTES);
    return (test_sheet_t){pixels, visible, row, stride * sizeof(uint32_t), total, width, height};
}

static uint32_t sheet_at(const test_sheet_t *sheet, int32_t x, int32_t y) {
    return *(const uint32_t *)(const void *)((const uint8_t *)sheet->visible + (uint64_t)y * sheet->pitch +
                                             (uint64_t)x * sizeof(uint32_t));
}

static uint32_t *sheet_ref(test_sheet_t *sheet, int32_t x, int32_t y) {
    return (uint32_t *)(void *)((uint8_t *)sheet->visible + (uint64_t)y * sheet->pitch +
                                (uint64_t)x * sizeof(uint32_t));
}

static bool sheet_canvas(const test_sheet_t *sheet, ui_canvas_t *canvas) {
    return ui_canvas_start(canvas, (uint8_t *)sheet->visible, sheet->pitch, sheet->width, sheet->height, sheet->row);
}

static uint64_t test_rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static int32_t test_rng_range(uint64_t *state, int32_t low, int32_t high) {
    return low + (int32_t)(test_rng_next(state) % (uint64_t)((int64_t)high - low + 1));
}

/* One pixel by the scalar reference, which asm/src/blend must match. */
static void model_blend(uint32_t *dst, uint32_t src) {
    simd_reference_blend_over(dst, &src, 1u);
}

static bool glyph_pixel(const uint8_t *atlas, uint32_t character, uint32_t x, uint32_t y) {
    const uint32_t index = character - UI_FONT_FIRST;
    const uint32_t px = index % 16u * UI_FONT_CELL + x;
    const uint32_t py = index / 16u * UI_FONT_CELL + y;
    return atlas[((uint64_t)py * UI_FONT_ATLAS_WIDTH + px) * 4u + 3u] >= 128u;
}

/* From the atlas, not the table under test. */
static uint32_t model_advance(const uint8_t *atlas, uint32_t character) {
    uint32_t ink = 0u;
    for (uint32_t y = 0u; y < UI_FONT_GLYPH_HEIGHT; y++) {
        for (uint32_t x = 0u; x < UI_FONT_GLYPH_WIDTH; x++) {
            if (glyph_pixel(atlas, character, x, y) && x + 1u > ink) {
                ink = x + 1u;
            }
        }
    }
    return (ink == 0u ? UI_FONT_SPACE_WIDTH : ink) + UI_FONT_SPACING;
}

/* Printable ASCII and newlines, one font pixel at a time, one screen pixel at a time. */
static void model_text(test_sheet_t *sheet, ui_rect_t clip, const uint8_t *atlas, int64_t x, int64_t y,
                       const char *text, uint32_t color, uint32_t scale) {
    int64_t pen = x;
    int64_t top = y;
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '\n') {
            pen = x;
            top += (int64_t)((uint64_t)UI_FONT_LINE * scale);
            continue;
        }
        const uint32_t character = (uint8_t)*c;
        for (uint32_t gy = 0u; gy < UI_FONT_GLYPH_HEIGHT; gy++) {
            for (uint32_t gx = 0u; gx < UI_FONT_GLYPH_WIDTH; gx++) {
                if (!glyph_pixel(atlas, character, gx, gy)) {
                    continue;
                }
                for (int64_t sy = 0; sy < (int64_t)scale; sy++) {
                    for (int64_t sx = 0; sx < (int64_t)scale; sx++) {
                        const int64_t px = pen + (int64_t)(gx * scale) + sx;
                        const int64_t py = top + (int64_t)(gy * scale) + sy;
                        if (px >= clip.x && py >= clip.y && px < (int64_t)clip.x + clip.width &&
                            py < (int64_t)clip.y + clip.height) {
                            model_blend(sheet_ref(sheet, (int32_t)px, (int32_t)py), color);
                        }
                    }
                }
            }
        }
        pen += (int64_t)((uint64_t)model_advance(atlas, character) * scale);
    }
}

static void test_input_set(input_keys_t *keys, uint32_t code) {
    keys->words[code / 64u] |= 1ull << (code % 64u);
}

/* What ui_widget_input_feed reads, for a pointer sent to (x, y). */
static input_frame_t pointer_frame(const ui_widget_t *widget, int32_t x, int32_t y) {
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = (int64_t)x - widget->input.pointer_x;
    frame.mouse_dy = (int64_t)y - widget->input.pointer_y;
    return frame;
}

typedef struct test_ui_t {
    ui_t ui;
    texture_t texture;
    test_sheet_t sheet;
    uint8_t _pad0[16];
} test_ui_t;
static_assert(sizeof(test_ui_t) == 1408);
static_assert(alignof(test_ui_t) == 64);

/* The test arena lines up on 16 bytes, and a ui_t wants 64. */
static test_ui_t *test_ui_alloc(test_context_t *t) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, sizeof(test_ui_t) + alignof(test_ui_t));
    return (test_ui_t *)((raw + alignof(test_ui_t) - 1u) & ~((uintptr_t)alignof(test_ui_t) - 1u));
}

static bool test_ui_start(test_context_t *t, test_ui_t *f) {
    f->sheet = sheet_make(t, SHEET_WIDTH, SHEET_HEIGHT, 0xFF204060u);
    return texture_init(&f->texture) && ui_init(&f->ui, &f->texture);
}

static void test_ui_stop(test_ui_t *f) {
    ui_deinit(&f->ui);
    texture_deinit(&f->texture);
}

static void test_frame_begin(test_context_t *t, test_ui_t *f) {
    CHECK(ui_frame_begin(&f->ui, (uint8_t *)f->sheet.visible, f->sheet.pitch, f->sheet.width, f->sheet.height));
}

/* A frame with the pointer at (x, y) and the left button doing this. */
static void test_pointer(test_ui_t *f, int32_t x, int32_t y, bool down, bool pressed, bool released) {
    input_frame_t frame = pointer_frame(&f->ui.widget, x, y);
    if (down) {
        test_input_set(&frame.down, BTN_LEFT);
    }
    if (pressed) {
        test_input_set(&frame.pressed, BTN_LEFT);
    }
    if (released) {
        test_input_set(&frame.released, BTN_LEFT);
    }
    ui_widget_input_feed(&f->ui.widget, &frame);
}

/* Starts the pointer's canvas, so that a first move is not overridden by being put in the middle. */
static void test_first_frame(test_context_t *t, test_ui_t *f) {
    test_frame_begin(t, f);
    (void)ui_frame_end(&f->ui, false);
}

/* ---- The font ---------------------------------------------------------------------------------------------------- */

TEST(the_table_is_the_yaml_read_by_hand) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    /* From textures/ui/font/index.yaml, cell by cell. */
    static const uint8_t letter_a[UI_FONT_GLYPH_HEIGHT] = {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
    static const uint8_t exclamation[UI_FONT_GLYPH_HEIGHT] = {0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x00u, 0x04u};
    CHECK_MEM_EQ(ui_font_glyph(&ui.font, 'A')->rows, letter_a, UI_FONT_GLYPH_HEIGHT);
    CHECK_MEM_EQ(ui_font_glyph(&ui.font, '!')->rows, exclamation, UI_FONT_GLYPH_HEIGHT);
    CHECK_EQ_U64(ui_font_glyph(&ui.font, 'A')->advance, 6u);
    CHECK_EQ_U64(ui_font_glyph(&ui.font, '!')->advance, 4u);
    CHECK_EQ_U64(ui_font_glyph(&ui.font, ' ')->advance, 4u);
    /* What has no picture shows '?'. */
    CHECK(ui_font_glyph(&ui.font, 0x7Fu) == ui_font_glyph(&ui.font, '?'));
    CHECK(ui_font_glyph(&ui.font, 0x1Fu) == ui_font_glyph(&ui.font, '?'));
    CHECK(ui_font_glyph(&ui.font, 0xFFu) == ui_font_glyph(&ui.font, '?'));
    /* Every printable but space has ink, and no two letters look alike. */
    for (uint32_t c = 33u; c < 127u; c++) {
        uint32_t ink = 0u;
        for (uint32_t y = 0u; y < UI_FONT_GLYPH_HEIGHT; y++) {
            ink |= ui_font_glyph(&ui.font, (uint8_t)c)->rows[y];
        }
        CHECK(ink != 0u);
        for (uint32_t other = 33u; other < c; other++) {
            CHECK(memcmp(ui_font_glyph(&ui.font, (uint8_t)c)->rows, ui_font_glyph(&ui.font, (uint8_t)other)->rows,
                         UI_FONT_GLYPH_HEIGHT) != 0);
        }
    }
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(a_bad_atlas_is_refused) {
    uint8_t *const memory = test_alloc(t, 1u << 16);
    arena_t arena;
    arena_init(&arena, "test", memory, 1u << 16);
    const uint64_t bytes = (uint64_t)UI_FONT_ATLAS_WIDTH * UI_FONT_ATLAS_HEIGHT * 4u;
    uint8_t *const atlas = test_alloc(t, bytes);
    ui_font_t font;
    CHECK(ui_font_load(&font, &arena, atlas, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT));
    CHECK(font.borrowed_glyphs != nullptr);
    CHECK(!ui_font_load(&font, &arena, atlas, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT - 1u));
    CHECK(font.borrowed_glyphs == nullptr);
    /* Column 5 of a cell: past a glyph's five. */
    atlas[(5ull + 0ull) * 4u + 3u] = 255u;
    CHECK(!ui_font_load(&font, &arena, atlas, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT));
    atlas[(5ull + 0ull) * 4u + 3u] = 0u;
    /* Row 7 of a cell: past a glyph's seven. */
    atlas[((uint64_t)UI_FONT_ATLAS_WIDTH * 7u) * 4u + 3u] = 255u;
    CHECK(!ui_font_load(&font, &arena, atlas, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT));
    atlas[((uint64_t)UI_FONT_ATLAS_WIDTH * 7u) * 4u + 3u] = 0u;
    /* A glyph's last column and row are its own. */
    atlas[((uint64_t)UI_FONT_ATLAS_WIDTH * 6u + 4u) * 4u + 3u] = 255u;
    CHECK(ui_font_load(&font, &arena, atlas, UI_FONT_ATLAS_WIDTH, UI_FONT_ATLAS_HEIGHT));
    CHECK_EQ_U64(font.borrowed_glyphs[0].rows[6], 0x10u);
    CHECK_EQ_U64(font.borrowed_glyphs[0].advance, 6u);
    arena_deinit(&arena);
}

TEST(text_is_measured_by_hand) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    const ui_font_t *const font = &ui.font;
    ui_text_size_t size = ui_font_measure(font, "", 1u);
    CHECK_EQ_U64(size.width, 0u);
    CHECK_EQ_U64(size.height, 0u);
    size = ui_font_measure(font, "A", 1u); /* 5, with the spacing after it left out */
    CHECK_EQ_U64(size.width, 5u);
    CHECK_EQ_U64(size.height, 7u);
    size = ui_font_measure(font, "A!", 1u); /* 6 + 4 - 1 */
    CHECK_EQ_U64(size.width, 9u);
    size = ui_font_measure(font, "A!", 3u);
    CHECK_EQ_U64(size.width, 27u);
    CHECK_EQ_U64(size.height, 21u);
    size = ui_font_measure(font, "A\nAA", 1u); /* widest is 6 + 6 - 1, and 9 + 7 high */
    CHECK_EQ_U64(size.width, 11u);
    CHECK_EQ_U64(size.height, 16u);
    size = ui_font_measure(font, "A\n", 2u); /* a second, empty line */
    CHECK_EQ_U64(size.width, 10u);
    CHECK_EQ_U64(size.height, 32u);
    /* One '?' whatever a character's length, and a tab is a space. */
    const uint64_t question = ui_font_measure(font, "?", 1u).width;
    CHECK_EQ_U64(ui_font_measure(font, "\xC3\xA9", 1u).width, question);         /* two bytes */
    CHECK_EQ_U64(ui_font_measure(font, "\xE2\x82\xAC", 1u).width, question);     /* three */
    CHECK_EQ_U64(ui_font_measure(font, "\xF0\x9F\x98\x80", 1u).width, question); /* four */
    CHECK_EQ_U64(ui_font_measure(font, "\x01", 1u).width, question);
    CHECK_EQ_U64(ui_font_measure(font, "\t", 1u).width, ui_font_measure(font, " ", 1u).width);
    /* Bytes that continue nothing, and text ending partway through a character, stop where the NUL is. */
    CHECK_EQ_U64(ui_font_measure(font, "\xE2\x82", 1u).width, question);
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(a_letter_lands_on_its_pixels) {
    test_sheet_t sheet = sheet_make(t, 16u, 18u, 0xFF000000u);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    ui_font_draw(&ui.font, &canvas, 2, 1, "A", (ui_text_style_t){0xFFFFFFFFu, 0u, 2u});
    static const char *const expected[7] = {".www.", "w...w", "w...w", "wwwww", "w...w", "w...w", "w...w"};
    for (int32_t y = 0; y < 18; y++) {
        for (int32_t x = 0; x < 16; x++) {
            bool ink = false;
            if (x >= 2 && x < 12 && y >= 1 && y < 15) {
                ink = expected[(y - 1) / 2][(x - 2) / 2] == 'w';
            }
            CHECK_EQ_U64(sheet_at(&sheet, x, y), ink ? 0xFFFFFFFFu : 0xFF000000u);
        }
    }
    /* One call of the kernel per scanline, whole. */
    CHECK_EQ_U64(canvas.blend_calls, 14u);
    CHECK_EQ_U64(canvas.blended_pixels, 14u * 10u);
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(a_shadow_goes_under_the_text_one_font_pixel_away) {
    test_sheet_t sheet = sheet_make(t, 16u, 12u, 0xFF000000u);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    ui_font_draw(&ui.font, &canvas, 0, 0, "!", (ui_text_style_t){0xFFFFFFFFu, 0xFF404040u, 1u});
    /* '!' is one column at x = 2, rows 0 to 4 and 6. Its shadow is at x = 3, rows 1 to 5 and 7. */
    for (int32_t y = 0; y < 8; y++) {
        CHECK_EQ_U64(sheet_at(&sheet, 2, y), y == 5 || y == 7 ? 0xFF000000u : 0xFFFFFFFFu);
        CHECK_EQ_U64(sheet_at(&sheet, 3, y), y == 0 || y == 6 ? 0xFF000000u : 0xFF404040u);
    }
    /* Clear text with no shadow, and a scale of 0, draw nothing. */
    const uint64_t calls = canvas.blend_calls;
    ui_font_draw(&ui.font, &canvas, 0, 0, "!", (ui_text_style_t){0u, 0u, 1u});
    ui_font_draw(&ui.font, &canvas, 0, 0, "!", (ui_text_style_t){0xFFFFFFFFu, 0xFF000000u, 0u});
    CHECK_EQ_U64(canvas.blend_calls, calls);
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(random_text_matches_a_pixel_by_pixel_model_anywhere_and_clipped) {
    enum : uint32_t { WIDTH = 96u, HEIGHT = 64u, ROUNDS = 4000u, TEXT_MAX = 24u };
    test_sheet_t real = sheet_make(t, WIDTH, HEIGHT, 0xAB102030u);
    test_sheet_t model = sheet_make(t, WIDTH, HEIGHT, 0xAB102030u);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    uint32_t id = 0u;
    texture_mip_t atlas;
    REQUIRE(texture_find(&texture, "ui/font", &id) && texture_mip_get(&texture, id, 0u, 0u, &atlas));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&real, &canvas));
    uint64_t rng = 0x123456789ABCDEFull;
    for (uint32_t round = 0u; round < ROUNDS; round++) {
        char text[TEXT_MAX];
        const uint32_t length = (uint32_t)test_rng_range(&rng, 1, (int32_t)TEXT_MAX - 1);
        for (uint32_t i = 0u; i < length; i++) {
            text[i] = test_rng_range(&rng, 0, 9) == 0 ? '\n' : (char)test_rng_range(&rng, 32, 126);
        }
        text[length] = '\0';
        const ui_rect_t clip_asked = {test_rng_range(&rng, -20, 90), test_rng_range(&rng, -20, 60),
                                      test_rng_range(&rng, 0, 120), test_rng_range(&rng, 0, 90)};
        (void)ui_canvas_clip_set(&canvas, clip_asked);
        const ui_rect_t clip = canvas.clip;
        const int32_t x = test_rng_range(&rng, -60, 100);
        const int32_t y = test_rng_range(&rng, -60, 70);
        const uint32_t scale = (uint32_t)test_rng_range(&rng, 1, 4);
        const uint32_t color = (uint32_t)test_rng_next(&rng);
        const uint32_t shadow = test_rng_range(&rng, 0, 1) == 0 ? 0u : (uint32_t)test_rng_next(&rng);
        ui_font_draw(&ui.font, &canvas, x, y, text, (ui_text_style_t){color, shadow, scale});
        if (shadow >> 24 != 0u) {
            model_text(&model, clip, atlas.borrowed_pixels, (int64_t)x + scale, (int64_t)y + scale, text, shadow,
                       scale);
        }
        if (color >> 24 != 0u) {
            model_text(&model, clip, atlas.borrowed_pixels, x, y, text, color, scale);
        }
    }
    CHECK_MEM_EQ(real.pixels, model.pixels, real.total * sizeof(uint32_t));
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(text_at_the_ends_of_the_coordinates_is_clipped_without_overflow) {
    test_sheet_t sheet = sheet_make(t, 32u, 32u, 0xFF000000u);
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    const ui_text_style_t style = {0xFFFFFFFFu, 0xFF404040u, 4u};
    ui_font_draw(&ui.font, &canvas, INT32_MAX, INT32_MAX, "AAA\nAAA", style);
    ui_font_draw(&ui.font, &canvas, INT32_MIN, INT32_MIN, "AAA\nAAA", style);
    ui_font_draw(&ui.font, &canvas, INT32_MAX, 0, "A", style);
    ui_font_draw(&ui.font, &canvas, 0, INT32_MIN, "A", style);
    CHECK_EQ_U64(canvas.blend_calls, 0u);
    ui_deinit(&ui);
    texture_deinit(&texture);
}

/* ---- Composition ------------------------------------------------------------------------------------------------- */

TEST(rects_by_hand) {
    const ui_rect_t a = {0, 0, 10, 10};
    ui_rect_t r = ui_rect_intersect(a, (ui_rect_t){5, 6, 10, 10});
    CHECK_EQ_I64(r.x, 5);
    CHECK_EQ_I64(r.y, 6);
    CHECK_EQ_I64(r.width, 5);
    CHECK_EQ_I64(r.height, 4);
    r = ui_rect_intersect(a, (ui_rect_t){10, 0, 5, 5}); /* touching is not sharing */
    CHECK_EQ_I64(r.width, 0);
    r = ui_rect_intersect((ui_rect_t){INT32_MAX - 1, INT32_MAX - 1, INT32_MAX, INT32_MAX},
                          (ui_rect_t){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX});
    CHECK_EQ_I64(r.width, 0);
    CHECK(ui_rect_contains(a, 0, 0));
    CHECK(ui_rect_contains(a, 9, 9));
    CHECK(!ui_rect_contains(a, 10, 9));
    CHECK(!ui_rect_contains(a, -1, 0));
    CHECK(!ui_rect_contains((ui_rect_t){0, 0, 0, 5}, 0, 0));
    r = ui_rect_center((ui_rect_t){10, 20, 100, 50}, 21, 10);
    CHECK_EQ_I64(r.x, 49);
    CHECK_EQ_I64(r.y, 40);
    CHECK_EQ_I64(r.width, 21);
    CHECK_EQ_I64(r.height, 10);
}

TEST(a_canvas_it_cannot_use_is_refused) {
    uint32_t pixels[16];
    uint32_t row[4];
    ui_canvas_t canvas;
    uint8_t *const bytes = (uint8_t *)pixels;
    CHECK(ui_canvas_start(&canvas, bytes, 16u, 4u, 4u, row));
    CHECK(!ui_canvas_start(&canvas, bytes, 16u, 0u, 4u, row));
    CHECK(!ui_canvas_start(&canvas, bytes, 16u, 4u, 0u, row));
    CHECK(!ui_canvas_start(&canvas, bytes, 15u, 4u, 4u, row)); /* cannot hold a row */
    CHECK(!ui_canvas_start(&canvas, bytes, 18u, 4u, 4u, row)); /* no whole number of pixels */
    CHECK(!ui_canvas_start(&canvas, bytes + 1u, 16u, 4u, 4u, row));
    CHECK(
        !ui_canvas_start(&canvas, bytes, (uint64_t)(UI_CANVAS_MAX_WIDTH + 1u) * 4u, UI_CANVAS_MAX_WIDTH + 1u, 1u, row));
}

TEST(fills_match_the_reference_and_keep_the_top_byte) {
    enum : uint32_t { WIDTH = 40u, HEIGHT = 30u, ROUNDS = 5000u };
    test_sheet_t real = sheet_make(t, WIDTH, HEIGHT, 0x7F336699u);
    test_sheet_t model = sheet_make(t, WIDTH, HEIGHT, 0x7F336699u);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&real, &canvas));
    uint64_t rng = 0xFEEDFACECAFEBEEFull;
    for (uint32_t round = 0u; round < ROUNDS; round++) {
        (void)ui_canvas_clip_set(&canvas, (ui_rect_t){test_rng_range(&rng, -10, 40), test_rng_range(&rng, -10, 30),
                                                      test_rng_range(&rng, 0, 60), test_rng_range(&rng, 0, 50)});
        const ui_rect_t rect = {test_rng_range(&rng, -20, 50), test_rng_range(&rng, -20, 40),
                                test_rng_range(&rng, -2, 60), test_rng_range(&rng, -2, 50)};
        const uint32_t color = (uint32_t)test_rng_next(&rng);
        ui_compose_fill(&canvas, rect, color);
        for (int32_t y = canvas.clip.y; y < canvas.clip.y + canvas.clip.height; y++) {
            for (int32_t x = canvas.clip.x; x < canvas.clip.x + canvas.clip.width; x++) {
                if (rect.width > 0 && rect.height > 0 && ui_rect_contains(rect, x, y)) {
                    model_blend(sheet_ref(&model, x, y), color);
                }
            }
        }
    }
    CHECK_MEM_EQ(real.pixels, model.pixels, real.total * sizeof(uint32_t));
    /* Nothing above ever changed a top byte, as a scanout frame's is unused. */
    for (int32_t y = 0; y < (int32_t)HEIGHT; y++) {
        for (int32_t x = 0; x < (int32_t)WIDTH; x++) {
            CHECK_EQ_U64(sheet_at(&real, x, y) >> 24, 0x7Fu);
        }
    }
}

TEST(a_texture_loads_as_argb) {
    texture_t texture;
    REQUIRE(texture_init(&texture));
    uint8_t *const memory = test_alloc(t, 1u << 20);
    arena_t arena;
    arena_init(&arena, "test", memory, 1u << 20);
    ui_image_t image;
    REQUIRE(ui_image_load(&image, &arena, &texture, "ui/tooltip"));
    CHECK_EQ_U64(image.width, 12u);
    CHECK_EQ_U64(image.height, 12u);
    /* From its recipe: an outline of #3d1f70, a second of #28104a, and a fill of #180a20e8. */
    CHECK_EQ_U64(image.borrowed_pixels[0], 0xFF3D1F70u);
    CHECK_EQ_U64(image.borrowed_pixels[12u + 1u], 0xFF28104Au);
    CHECK_EQ_U64(image.borrowed_pixels[12u * 5u + 5u], 0xE8180A20u);
    CHECK(!ui_image_load(&image, &arena, &texture, "ui/nothing_by_this_name"));
    arena_deinit(&arena);
    texture_deinit(&texture);
}

TEST(random_images_match_a_nearest_pixel_model_anywhere_and_clipped) {
    enum : uint32_t { WIDTH = 64u, HEIGHT = 48u, ROUNDS = 3000u, IMAGE_W = 23u, IMAGE_H = 17u };
    test_sheet_t real = sheet_make(t, WIDTH, HEIGHT, 0xFF808080u);
    test_sheet_t model = sheet_make(t, WIDTH, HEIGHT, 0xFF808080u);
    uint32_t *const pixels = test_alloc(t, (uint64_t)IMAGE_W * IMAGE_H * sizeof(uint32_t));
    uint64_t rng = 0xC0FFEE1234567ull;
    for (uint32_t i = 0u; i < IMAGE_W * IMAGE_H; i++) {
        pixels[i] = (uint32_t)test_rng_next(&rng);
    }
    const ui_image_t image = {pixels, IMAGE_W, IMAGE_H};
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&real, &canvas));
    for (uint32_t round = 0u; round < ROUNDS; round++) {
        (void)ui_canvas_clip_set(&canvas, (ui_rect_t){test_rng_range(&rng, -10, 60), test_rng_range(&rng, -10, 40),
                                                      test_rng_range(&rng, 0, 80), test_rng_range(&rng, 0, 70)});
        const int32_t sx = test_rng_range(&rng, 0, (int32_t)IMAGE_W - 1);
        const int32_t sy = test_rng_range(&rng, 0, (int32_t)IMAGE_H - 1);
        const ui_rect_t source = {sx, sy, test_rng_range(&rng, 1, (int32_t)IMAGE_W - sx),
                                  test_rng_range(&rng, 1, (int32_t)IMAGE_H - sy)};
        /* Every so often exactly the source's width, which takes the path that builds no row. */
        const int32_t rect_w = test_rng_range(&rng, 0, 2) == 0 ? source.width : test_rng_range(&rng, 1, 70);
        const ui_rect_t rect = {test_rng_range(&rng, -30, 70), test_rng_range(&rng, -30, 50), rect_w,
                                test_rng_range(&rng, 1, 60)};
        ui_compose_image(&canvas, &image, source, rect);
        for (int32_t y = canvas.clip.y; y < canvas.clip.y + canvas.clip.height; y++) {
            for (int32_t x = canvas.clip.x; x < canvas.clip.x + canvas.clip.width; x++) {
                if (!ui_rect_contains(rect, x, y)) {
                    continue;
                }
                const int64_t px = source.x + ((int64_t)x - rect.x) * source.width / rect.width;
                const int64_t py = source.y + ((int64_t)y - rect.y) * source.height / rect.height;
                model_blend(sheet_ref(&model, x, y), pixels[py * (int64_t)IMAGE_W + px]);
            }
        }
    }
    CHECK_MEM_EQ(real.pixels, model.pixels, real.total * sizeof(uint32_t));
}

TEST(nine_parts_by_hand) {
    /* Each source pixel says where it came from: 0xFF00XXYY. */
    uint32_t pixels[5u * 5u];
    for (uint32_t y = 0u; y < 5u; y++) {
        for (uint32_t x = 0u; x < 5u; x++) {
            pixels[y * 5u + x] = 0xFF000000u | x << 8 | y;
        }
    }
    const ui_image_t image = {pixels, 5u, 5u};
    test_sheet_t sheet = sheet_make(t, 12u, 10u, 0xFF777777u);
    ui_canvas_t canvas;
    REQUIRE(sheet_canvas(&sheet, &canvas));
    /* Border 1 at scale 2: columns are 2, 6 and 2 wide and rows 2, 4 and 2 high, from (1, 1). The middle part's three source
     * pixels are stretched over 6 and over 4. */
    ui_compose_nine(&canvas, &image, 1u, 2u, (ui_rect_t){1, 1, 10, 8});
    static const uint32_t source_x[10] = {0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u};
    static const uint32_t source_y[8] = {0u, 0u, 1u, 1u, 2u, 3u, 4u, 4u};
    for (int32_t y = 0; y < 10; y++) {
        for (int32_t x = 0; x < 12; x++) {
            if (x < 1 || x > 10 || y < 1 || y > 8) {
                CHECK_EQ_U64(sheet_at(&sheet, x, y), 0xFF777777u);
                continue;
            }
            CHECK_EQ_U64(sheet_at(&sheet, x, y), 0xFF000000u | source_x[x - 1] << 8 | source_y[y - 1]);
        }
    }
    /* Too small for two corners: each takes half, the odd pixel going to the middle. */
    test_sheet_t small = sheet_make(t, 3u, 3u, 0xFF777777u);
    REQUIRE(sheet_canvas(&small, &canvas));
    ui_compose_nine(&canvas, &image, 2u - 1u, 4u, (ui_rect_t){0, 0, 3, 3});
    CHECK_EQ_U64(sheet_at(&small, 0, 0), 0xFF000000u);
    CHECK_EQ_U64(sheet_at(&small, 2, 2), 0xFF000404u);
    CHECK_EQ_U64(sheet_at(&small, 1, 1), 0xFF000101u);
}

/* ---- Widgets ----------------------------------------------------------------------------------------------------- */

/* The canvas's pixel compared with an opaque image's, as a frame holds no alpha. */
static bool pixel_is(const test_sheet_t *sheet, int32_t x, int32_t y, const ui_image_t *image, uint32_t ix,
                     uint32_t iy) {
    const uint32_t want = image->borrowed_pixels[(uint64_t)iy * image->width + ix];
    return ((sheet_at(sheet, x, y) ^ want) & 0x00FFFFFFu) == 0u;
}

TEST(the_scale_leaves_room_for_320_by_240) {
    CHECK_EQ_U64(ui_widget_scale_for(100u, 100u), 1u);
    CHECK_EQ_U64(ui_widget_scale_for(639u, 480u), 1u);
    CHECK_EQ_U64(ui_widget_scale_for(640u, 480u), 2u);
    CHECK_EQ_U64(ui_widget_scale_for(1920u, 1080u), 4u);
    CHECK_EQ_U64(ui_widget_scale_for(3840u, 2160u), 9u);
}

TEST(a_button_clicks_only_when_pressed_and_released_on_it) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const uint32_t id = 7u;
    const ui_rect_t rect = {100, 100, 200, 40};
    const int32_t inside_x = 110, inside_y = 110, outside_x = 50, outside_y = 50;

    /* Press and release in one frame's input. */
    test_pointer(f, inside_x, inside_y, false, true, true);
    test_frame_begin(t, f);
    CHECK(ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);

    /* Over several frames: nothing until the release. */
    test_pointer(f, inside_x, inside_y, true, true, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    CHECK_EQ_U64(w->active_id, id);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, inside_x, inside_y, true, false, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, inside_x, inside_y, false, false, true);
    test_frame_begin(t, f);
    CHECK(ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);
    CHECK_EQ_U64(w->active_id, 0u);

    /* Pressed on it, released off it. */
    test_pointer(f, inside_x, inside_y, true, true, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, outside_x, outside_y, false, false, true);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);

    /* Pressed off it, released on it. */
    test_pointer(f, outside_x, outside_y, true, true, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, inside_x, inside_y, false, false, true);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);

    /* Pressed on another, dragged over this one, released here. */
    const uint32_t other_id = 8u;
    const ui_rect_t other = {100, 200, 200, 40};
    test_pointer(f, 110, 210, true, true, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    CHECK(!ui_widget_button(w, other_id, other, "Other", true));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, inside_x, inside_y, false, false, true);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    CHECK(!ui_widget_button(w, other_id, other, "Other", true));
    (void)ui_frame_end(&f->ui, false);

    /* Disabled: a click does nothing. */
    test_pointer(f, inside_x, inside_y, false, true, true);
    test_frame_begin(t, f);
    CHECK(!ui_widget_button(w, id, rect, "Go", false));
    CHECK_EQ_U64(w->active_id, 0u);
    (void)ui_frame_end(&f->ui, false);

    /* Clipped away: a click on what cannot be seen does nothing. */
    test_pointer(f, inside_x, inside_y, false, true, true);
    test_frame_begin(t, f);
    (void)ui_canvas_clip_set(&f->ui.canvas, (ui_rect_t){0, 0, 10, 10});
    CHECK(!ui_widget_button(w, id, rect, "Go", true));
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

TEST(a_button_looks_as_it_is) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const ui_rect_t rect = {100, 100, 200, 40};
    /* Its corner, at scale 2: (1, 1) of the texture is at rect + 2. It is opaque in all three. */
    test_pointer(f, 10, 10, false, false, false);
    test_frame_begin(t, f);
    (void)ui_widget_button(w, 1u, rect, "", true);
    CHECK(pixel_is(&f->sheet, 102, 102, &w->skin.button, 1u, 1u));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 150, 110, false, false, false);
    test_frame_begin(t, f);
    (void)ui_widget_button(w, 1u, rect, "", true);
    CHECK(pixel_is(&f->sheet, 102, 102, &w->skin.button_hover, 1u, 1u));
    (void)ui_frame_end(&f->ui, false);
    test_frame_begin(t, f);
    (void)ui_widget_button(w, 1u, rect, "", false);
    CHECK(pixel_is(&f->sheet, 102, 102, &w->skin.button_disabled, 1u, 1u));
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

TEST(a_checkbox_flips_on_a_click) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const ui_rect_t rect = {40, 40, 200, 40};
    bool value = false;
    test_pointer(f, 200, 60, true, true, false); /* on the label: all of rect counts */
    test_frame_begin(t, f);
    CHECK(!ui_widget_checkbox(w, 3u, rect, "Shadows", &value));
    CHECK(!value);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 200, 60, false, false, true);
    test_frame_begin(t, f);
    CHECK(ui_widget_checkbox(w, 3u, rect, "Shadows", &value));
    CHECK(value);
    /* Ticked: the box, 24 square at scale 2, in the middle of rect's 40, is the ticked texture. */
    CHECK(pixel_is(&f->sheet, 40 + 6 * 2, 40 + 8 + 6 * 2, &w->skin.checkbox_checked, 6u, 6u));
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 200, 60, false, true, true);
    test_frame_begin(t, f);
    CHECK(ui_widget_checkbox(w, 3u, rect, "Shadows", &value));
    CHECK(!value);
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

TEST(a_slider_follows_the_pointer_and_the_wheel) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    /* 400 wide with a knob of 8 * 2 leaves 384 of travel, and the pointer means the knob's middle, 8 in. */
    const ui_rect_t rect = {100, 100, 400, 40};
    int32_t value = 2;
    const uint32_t id = 5u;

    /* Hovering alone changes nothing. */
    test_pointer(f, 300, 120, false, false, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 2);
    (void)ui_frame_end(&f->ui, false);

    /* Press at the very middle of the travel: x = 100 + 8 + 192. 2 + 192 / 384 * 510 = 257. */
    test_pointer(f, 300, 120, true, true, false);
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 257);
    (void)ui_frame_end(&f->ui, false);

    /* Dragged off the widget, it still follows, and stops at each end. */
    test_pointer(f, 1000, 400, true, false, false);
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 512);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 0, 0, true, false, false);
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 2);
    (void)ui_frame_end(&f->ui, false);

    /* Let go away from it: it stays, and no longer follows. */
    test_pointer(f, 300, 300, false, false, true);
    test_frame_begin(t, f);
    (void)ui_widget_slider(w, id, rect, "Render", &value, 2, 512);
    (void)ui_frame_end(&f->ui, false);
    const int32_t left = value;
    test_pointer(f, 450, 300, false, false, false);
    test_frame_begin(t, f);
    CHECK(!ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, left);
    (void)ui_frame_end(&f->ui, false);

    /* The wheel, over it: a step per detent, and stopping at the ends. */
    input_frame_t frame = pointer_frame(w, 300, 120);
    frame.wheel = 3;
    ui_widget_input_feed(w, &frame);
    value = 10;
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 13);
    (void)ui_frame_end(&f->ui, false);
    frame = pointer_frame(w, 300, 120);
    frame.wheel = -100;
    ui_widget_input_feed(w, &frame);
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 2);
    (void)ui_frame_end(&f->ui, false);

    /* Not over it, the wheel is someone else's. */
    frame = pointer_frame(w, 10, 10);
    frame.wheel = 3;
    ui_widget_input_feed(w, &frame);
    test_frame_begin(t, f);
    CHECK(!ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    (void)ui_frame_end(&f->ui, false);

    /* A value out of range comes back in, and a range of one value has nowhere to go. */
    value = 9000;
    test_frame_begin(t, f);
    CHECK(ui_widget_slider(w, id, rect, "Render", &value, 2, 512));
    CHECK_EQ_I64(value, 512);
    int32_t only = 4;
    CHECK(!ui_widget_slider(w, 6u, rect, "Only", &only, 4, 4));
    CHECK_EQ_I64(only, 4);
    (void)ui_frame_end(&f->ui, false);
    /* The full range of int32_t does not overflow. */
    int32_t wide = 0;
    test_pointer(f, 1000, 120, true, true, false);
    test_frame_begin(t, f);
    (void)ui_widget_slider(w, 9u, (ui_rect_t){100, 100, 400, 40}, "Wide", &wide, INT32_MIN, INT32_MAX);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 1000, 120, false, false, true);
    test_frame_begin(t, f);
    (void)ui_frame_end(&f->ui, false);
    /* A press from outside the rect, so not on it. */
    CHECK_EQ_I64(wide, 0);
    test_pointer(f, 499, 120, true, true, false);
    test_frame_begin(t, f);
    (void)ui_widget_slider(w, 9u, (ui_rect_t){100, 100, 400, 40}, "Wide", &wide, INT32_MIN, INT32_MAX);
    (void)ui_frame_end(&f->ui, false);
    CHECK_EQ_I64(wide, INT32_MAX);
    test_ui_stop(f);
}

/* Keys pressed this frame, with or without shift. */
static void test_keys(test_ui_t *f, const uint32_t *codes, uint32_t count, bool shift, bool repeat) {
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    if (shift) {
        test_input_set(&frame.down, KEY_LEFTSHIFT);
    }
    for (uint32_t i = 0u; i < count; i++) {
        test_input_set(repeat ? &frame.repeated : &frame.pressed, codes[i]);
    }
    ui_widget_input_feed(&f->ui.widget, &frame);
}

TEST(a_text_field_takes_focus_and_keys) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const uint32_t id = 11u;
    const ui_rect_t rect = {100, 100, 200, 40};
    char text[6] = "";

    /* Keys go nowhere until it has focus. */
    static const uint32_t a[] = {KEY_A};
    test_keys(f, a, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_NONE);
    CHECK_STR_EQ(text, "");
    (void)ui_frame_end(&f->ui, false);

    test_pointer(f, 150, 120, false, true, true);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_NONE);
    CHECK_EQ_U64(w->focus_id, id);
    (void)ui_frame_end(&f->ui, false);

    test_keys(f, a, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_CHANGED);
    CHECK_STR_EQ(text, "a");
    (void)ui_frame_end(&f->ui, false);

    static const uint32_t shift_keys[] = {KEY_B, KEY_1};
    test_keys(f, shift_keys, 2u, true, false); /* by key code, so 1 before B */
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_CHANGED);
    CHECK_STR_EQ(text, "a!B");
    (void)ui_frame_end(&f->ui, false);

    /* The kernel's repeat types too, and a held key's press is not typed twice. */
    test_keys(f, a, 1u, false, true);
    test_frame_begin(t, f);
    (void)ui_widget_text_field(w, id, rect, text, sizeof(text));
    CHECK_STR_EQ(text, "a!Ba");
    (void)ui_frame_end(&f->ui, false);

    /* Full, at 5 and a NUL: no more goes in. */
    static const uint32_t many[] = {KEY_C, KEY_D, KEY_E};
    test_keys(f, many, 3u, false, false);
    test_frame_begin(t, f);
    (void)ui_widget_text_field(w, id, rect, text, sizeof(text));
    CHECK_EQ_U64(strlen(text), 5u);
    (void)ui_frame_end(&f->ui, false);
    test_keys(f, a, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_NONE);
    (void)ui_frame_end(&f->ui, false);

    /* Keys that make no character do nothing, and Backspace takes one off. */
    static const uint32_t f1[] = {KEY_F1, KEY_LEFTCTRL, KEY_ESC};
    test_keys(f, f1, 3u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_NONE);
    (void)ui_frame_end(&f->ui, false);
    static const uint32_t back[] = {KEY_BACKSPACE};
    char before[6];
    strcpy(before, text);
    before[4] = '\0';
    test_keys(f, back, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_CHANGED);
    CHECK_STR_EQ(text, before);
    (void)ui_frame_end(&f->ui, false);

    /* Enter submits and lets go. */
    static const uint32_t enter[] = {KEY_ENTER};
    test_keys(f, enter, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, text, sizeof(text)), UI_FIELD_SUBMITTED);
    CHECK_EQ_U64(w->focus_id, 0u);
    (void)ui_frame_end(&f->ui, false);

    /* A press elsewhere lets go too. */
    test_pointer(f, 150, 120, false, true, true);
    test_frame_begin(t, f);
    (void)ui_widget_text_field(w, id, rect, text, sizeof(text));
    CHECK_EQ_U64(w->focus_id, id);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 10, 10, false, true, true);
    test_frame_begin(t, f);
    (void)ui_widget_text_field(w, id, rect, text, sizeof(text));
    CHECK_EQ_U64(w->focus_id, 0u);
    (void)ui_frame_end(&f->ui, false);

    /* A backspace on nothing is nothing. */
    char empty[4] = "";
    w->focus_id = id;
    test_keys(f, back, 1u, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_I64(ui_widget_text_field(w, id, rect, empty, sizeof(empty)), UI_FIELD_NONE);
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

TEST(every_key_types_what_a_us_keyboard_says) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    /* Each by itself, so that the order is the one written here, the keyboard's rows. */
    static const uint32_t codes[] = {
        KEY_GRAVE, KEY_1,         KEY_2,          KEY_3,          KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
        KEY_MINUS, KEY_EQUAL,     KEY_Q,          KEY_W,          KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O,
        KEY_P,     KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH,  KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J,
        KEY_K,     KEY_L,         KEY_SEMICOLON,  KEY_APOSTROPHE, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
        KEY_COMMA, KEY_DOT,       KEY_SLASH,      KEY_SPACE,
    };
    const char plain[] = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./ ";
    const char shifted[] = "~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>? ";
    const uint32_t count = (uint32_t)(sizeof(codes) / sizeof(codes[0]));
    for (uint32_t shift = 0u; shift < 2u; shift++) {
        char text[64] = "";
        w->focus_id = 21u;
        for (uint32_t i = 0u; i < count; i++) {
            test_keys(f, &codes[i], 1u, shift != 0u, false);
            test_frame_begin(t, f);
            (void)ui_widget_text_field(w, 21u, (ui_rect_t){10, 10, 200, 40}, text, sizeof(text));
            (void)ui_frame_end(&f->ui, false);
        }
        CHECK_STR_EQ(text, shift != 0u ? shifted : plain);
    }
    test_ui_stop(f);
}

TEST(a_long_text_field_shows_its_end_and_stays_inside) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    /* 80 wide at scale 2, with 8 inset on each side. */
    const ui_rect_t rect = {100, 100, 80, 40};
    char text[64] = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    w->focus_id = 31u;
    test_pointer(f, 10, 10, false, false, false);
    test_frame_begin(t, f);
    (void)ui_widget_text_field(w, 31u, rect, text, sizeof(text));
    /* Outside the rect, nothing moved. */
    const uint32_t background = 0xFF204060u;
    for (int32_t y = 90; y < 150; y++) {
        for (int32_t x = 90; x < 190; x++) {
            if (!ui_rect_contains(rect, x, y)) {
                CHECK_EQ_U64(sheet_at(&f->sheet, x, y), background);
            }
        }
    }
    /* The caret, white, is at the inner right edge: 2 wide, 14 high, centred in 40. */
    CHECK_EQ_U64(sheet_at(&f->sheet, 171, 120) & 0x00FFFFFFu, 0x00FFFFFFu);
    CHECK_EQ_U64(sheet_at(&f->sheet, 170, 120) & 0x00FFFFFFu, 0x00FFFFFFu);
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

TEST(a_slot_lights_up_and_says_what_the_pointer_did) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const ui_rect_t rect = {50, 50, 36, 36};
    test_pointer(f, 10, 10, false, true, false);
    test_frame_begin(t, f);
    CHECK_EQ_U64(ui_widget_slot(w, rect), 0u);
    (void)ui_frame_end(&f->ui, false);
    f->sheet = sheet_make(t, SHEET_WIDTH, SHEET_HEIGHT, 0xFF204060u); /* the slot above is still on the last one */
    test_pointer(f, 60, 60, false, false, false);
    test_frame_begin(t, f);
    CHECK_EQ_U64(ui_widget_slot(w, rect), UI_SLOT_HOVER);
    /* One texture pixel, 2 here, is left clear all round, and inside is the slot with the highlight on it. */
    uint32_t lit = 0xFF204060u;
    model_blend(&lit, w->skin.slot.borrowed_pixels[0]);
    model_blend(&lit, w->skin.slot_highlight.borrowed_pixels[0]);
    CHECK_EQ_U64(sheet_at(&f->sheet, 51, 51), 0xFF204060u);
    CHECK_EQ_U64(sheet_at(&f->sheet, 52, 52), lit);
    CHECK_EQ_U64(sheet_at(&f->sheet, 83, 83), lit);
    CHECK_EQ_U64(sheet_at(&f->sheet, 84, 84), 0xFF204060u);
    (void)ui_frame_end(&f->ui, false);
    test_pointer(f, 60, 60, true, true, false);
    test_frame_begin(t, f);
    CHECK_EQ_U64(ui_widget_slot(w, rect), UI_SLOT_HOVER | UI_SLOT_LEFT_PRESSED);
    (void)ui_frame_end(&f->ui, false);
    input_frame_t frame = pointer_frame(w, 60, 60);
    test_input_set(&frame.pressed, BTN_RIGHT);
    ui_widget_input_feed(w, &frame);
    test_frame_begin(t, f);
    CHECK_EQ_U64(ui_widget_slot(w, rect), UI_SLOT_HOVER | UI_SLOT_RIGHT_PRESSED);
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

typedef struct test_bar_t {
    int32_t value;
    int32_t maximum;
    int32_t filled;
} test_bar_t;
static_assert(sizeof(test_bar_t) == 12);

TEST(a_bar_fills_from_the_left) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    const ui_rect_t rect = {0, 0, 256, 48}; /* the textures' own size */
    uint32_t *const empty = test_alloc(t, 256u * 48u * sizeof(uint32_t));
    uint32_t *const full = test_alloc(t, 256u * 48u * sizeof(uint32_t));
    const uint32_t background = 0xFF204060u;
    /* What each texture alone gives, by the reference. */
    for (uint32_t y = 0u; y < 48u; y++) {
        for (uint32_t x = 0u; x < 256u; x++) {
            const uint64_t i = (uint64_t)y * 256u + x;
            empty[i] = background;
            model_blend(&empty[i], w->skin.progress_empty.borrowed_pixels[i]);
            full[i] = empty[i];
            model_blend(&full[i], w->skin.progress_full.borrowed_pixels[i]);
        }
    }
    static const test_bar_t cases[] = {{0, 4, 0},   {1, 4, 64}, {2, 4, 128}, {4, 4, 256},
                                       {9, 4, 256}, {-1, 4, 0}, {3, 0, 0},   {INT32_MAX, INT32_MAX, 256}};
    for (uint32_t c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++) {
        f->sheet = sheet_make(t, SHEET_WIDTH, SHEET_HEIGHT, background);
        test_frame_begin(t, f);
        ui_widget_progress(w, rect, cases[c].value, cases[c].maximum);
        (void)ui_frame_end(&f->ui, false);
        for (int32_t y = 0; y < 48; y += 3) {
            for (int32_t x = 0; x < 256; x++) {
                const uint64_t i = (uint64_t)y * 256u + (uint64_t)x;
                CHECK_EQ_U64(sheet_at(&f->sheet, x, y) & 0x00FFFFFFu,
                             (x < cases[c].filled ? full[i] : empty[i]) & 0x00FFFFFFu);
            }
        }
    }
    test_ui_stop(f);
}

TEST(a_tooltip_goes_on_top_stays_on_the_canvas_and_is_a_copy) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    /* In the bottom right corner it must slide back on. */
    test_pointer(f, (int32_t)SHEET_WIDTH - 1, (int32_t)SHEET_HEIGHT - 1, false, false, false);
    test_frame_begin(t, f);
    char text[8];
    strcpy(text, "Stone");
    ui_widget_tooltip(w, text);
    CHECK(f->ui.arena_frame.used > 0u);
    strcpy(text, "XXXXX");
    (void)ui_canvas_clip_set(&f->ui.canvas,
                             (ui_rect_t){0, 0, 1, 1}); /* the frame's end draws it whatever a widget left */
    (void)ui_frame_end(&f->ui, false);
    const ui_text_size_t size = ui_font_measure(&f->ui.font, "Stone", 2u);
    const int32_t width = (int32_t)size.width + 16, height = (int32_t)size.height + 16;
    const int32_t left = (int32_t)SHEET_WIDTH - width, top = (int32_t)SHEET_HEIGHT - height;
    CHECK(pixel_is(&f->sheet, left, top, &w->skin.tooltip, 0u, 0u));
    CHECK(pixel_is(&f->sheet, (int32_t)SHEET_WIDTH - 1, (int32_t)SHEET_HEIGHT - 1, &w->skin.tooltip, 11u, 11u));
    CHECK_EQ_U64(sheet_at(&f->sheet, left - 1, top), 0xFF204060u);
    /* A tooltip of no use to anyone is cut to length, not refused. */
    test_frame_begin(t, f);
    char *const long_text = test_alloc(t, 5000u);
    memset(long_text, 'A', 4999u);
    ui_widget_tooltip(w, long_text);
    CHECK_EQ_U64(strlen(w->borrowed_tooltip), UI_WIDGET_TOOLTIP_MAX - 1u);
    (void)ui_frame_end(&f->ui, false);
    /* The next frame starts with none. */
    const uint32_t before = sheet_at(&f->sheet, 5, 5);
    test_frame_begin(t, f);
    CHECK(w->borrowed_tooltip == nullptr);
    CHECK_EQ_U64(f->ui.arena_frame.used, 0u);
    (void)ui_frame_end(&f->ui, false);
    CHECK_EQ_U64(sheet_at(&f->sheet, 5, 5), before);
    test_ui_stop(f);
}

TEST(the_pointer_stays_on_the_canvas_and_is_drawn_when_asked) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    ui_widget_t *const w = &f->ui.widget;
    /* Nothing is known of the canvas yet, so a first frame puts it in the middle. */
    test_first_frame(t, f);
    CHECK_EQ_I64(w->input.pointer_x, 320);
    CHECK_EQ_I64(w->input.pointer_y, 240);
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.mouse_dx = 1000000;
    frame.mouse_dy = -1000000;
    ui_widget_input_feed(w, &frame);
    CHECK_EQ_I64(w->input.pointer_x, (int64_t)SHEET_WIDTH - 1);
    CHECK_EQ_I64(w->input.pointer_y, 0);
    /* Several polls add up, each clamped. */
    frame.mouse_dx = -10;
    frame.mouse_dy = 10;
    ui_widget_input_feed(w, &frame);
    ui_widget_input_feed(w, &frame);
    CHECK_EQ_I64(w->input.pointer_x, (int64_t)SHEET_WIDTH - 21);
    CHECK_EQ_I64(w->input.pointer_y, 20);
    /* Its tip is black at scale 2, on where the pointer is, and off when not asked for. */
    test_frame_begin(t, f);
    (void)ui_frame_end(&f->ui, false);
    CHECK_EQ_U64(sheet_at(&f->sheet, (int32_t)SHEET_WIDTH - 21, 20), 0xFF204060u);
    test_frame_begin(t, f);
    (void)ui_frame_end(&f->ui, true);
    CHECK_EQ_U64(sheet_at(&f->sheet, (int32_t)SHEET_WIDTH - 21, 20) & 0x00FFFFFFu, 0u);
    CHECK_EQ_U64(sheet_at(&f->sheet, (int32_t)SHEET_WIDTH - 20, 21) & 0x00FFFFFFu, 0u);
    /* A canvas of another size moves it to the new middle. */
    test_sheet_t other = sheet_make(t, 100u, 80u, 0xFF000000u);
    CHECK(ui_frame_begin(&f->ui, (uint8_t *)other.visible, other.pitch, other.width, other.height));
    CHECK_EQ_I64(w->input.pointer_x, 50);
    CHECK_EQ_I64(w->input.pointer_y, 40);
    CHECK_EQ_U64(w->scale, 1u);
    (void)ui_frame_end(&f->ui, false);
    test_ui_stop(f);
}

/* ---- The Owner --------------------------------------------------------------------------------------------------- */

/* Draws one of everything. */
static void sheet_all_widgets(test_ui_t *f) {
    ui_widget_t *const w = &f->ui.widget;
    const int32_t scale = (int32_t)w->scale;
    const int32_t button_h = (int32_t)UI_WIDGET_BUTTON_HEIGHT * scale;
    ui_widget_panel(w, (ui_rect_t){20, 20, 600, 440});
    ui_widget_label_centered(w, (ui_rect_t){20, 28, 600, 20}, "The quick brown fox jumps over", 0xFF404040u, false);
    ui_widget_label(w, 40, 56, "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklm", 0xFFFFFF55u, true);
    (void)ui_widget_button(w, 1u, (ui_rect_t){40, 80, 260, button_h}, "Singleplayer", true);
    (void)ui_widget_button(w, 2u, (ui_rect_t){340, 80, 260, button_h}, "Hovered", true);
    (void)ui_widget_button(w, 3u, (ui_rect_t){40, 130, 260, button_h}, "Disabled", false);
    bool off = false, on = true;
    (void)ui_widget_checkbox(w, 4u, (ui_rect_t){340, 130, 120, button_h}, "Fog", &off);
    (void)ui_widget_checkbox(w, 5u, (ui_rect_t){470, 130, 130, button_h}, "Shade", &on);
    int32_t distance = 32;
    (void)ui_widget_slider(w, 6u, (ui_rect_t){40, 180, 560, button_h}, "Render distance: 32", &distance, 2, 512);
    char name[32] = "New World";
    w->focus_id = 7u;
    (void)ui_widget_text_field(w, 7u, (ui_rect_t){40, 230, 560, button_h}, name, sizeof(name));
    for (int32_t i = 0; i < 9; i++) {
        (void)ui_widget_slot(w, (ui_rect_t){40 + i * 36, 280, 36, 36});
    }
    ui_widget_progress(w, (ui_rect_t){400, 280, 200, 36}, 2, 3);
    ui_widget_tooltip(w, "Oak Planks\nStack of 64");
}

/* The picture a person would look at, on a gradient, so that blending can be seen. */
static void sheet_gradient(test_sheet_t *sheet) {
    for (uint32_t y = 0u; y < sheet->height; y++) {
        for (uint32_t x = 0u; x < sheet->width; x++) {
            *sheet_ref(sheet, (int32_t)x, (int32_t)y) =
                0xFF000000u | (x * 255u / sheet->width) << 16 | (y * 255u / sheet->height) << 8 | 0x40u;
        }
    }
}

static uint64_t sheet_hash(const test_sheet_t *sheet) {
    uint64_t hash = 14695981039346656037ull;
    for (int32_t y = 0; y < (int32_t)sheet->height; y++) {
        for (int32_t x = 0; x < (int32_t)sheet->width; x++) {
            const uint32_t pixel = sheet_at(sheet, x, y) & 0x00FFFFFFu;
            for (uint32_t b = 0u; b < 3u; b++) {
                hash ^= (pixel >> (b * 8u)) & 0xFFu;
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

/* If UI_FONT_PNG names a file, the sheet goes there, to look at. */
static void sheet_write_png(test_context_t *t, const test_sheet_t *sheet) {
    const char *const path = getenv("UI_FONT_PNG");
    if (path == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(sheet->width, sheet->height);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    REQUIRE(display_png_encode((const uint8_t *)sheet->visible, (uint32_t)sheet->pitch, sheet->width, sheet->height,
                               png, capacity, &bytes));
    FILE *const file = fopen(path, "wb");
    REQUIRE(file != nullptr);
    CHECK_EQ_U64(fwrite(png, 1u, bytes, file), bytes);
    CHECK_EQ_I64(fclose(file), 0);
}

/* Set from a picture that was looked at; see README.md. A change here is a change a person would see. */
static const uint64_t SHEET_HASH = 0xa7dd20676cf0849dull;

TEST(the_whole_sheet_is_the_picture_that_was_looked_at) {
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    sheet_gradient(&f->sheet);
    test_first_frame(t, f);
    ui_widget_t *const w = &f->ui.widget;
    input_frame_t frame = pointer_frame(w, 470, 90); /* over "Hovered" */
    ui_widget_input_feed(w, &frame);
    test_frame_begin(t, f);
    sheet_all_widgets(f);
    (void)ui_frame_end(&f->ui, true);
    sheet_write_png(t, &f->sheet);
    const uint64_t hash = sheet_hash(&f->sheet);
    printf("sheet hash: 0x%016" PRIx64 "\n", hash);
    CHECK_EQ_U64(hash, SHEET_HASH);
    /* The kernel did all of it: nothing else writes to a canvas. */
    CHECK(f->ui.canvas.blend_calls > 1000u);
    test_ui_stop(f);
}

TEST(a_picture_is_copied_so_that_showing_it_again_does_not_blend_twice) {
    enum : uint32_t { WIDTH = 64u, HEIGHT = 48u };
    texture_t texture;
    REQUIRE(texture_init(&texture));
    ui_t ui;
    REQUIRE(ui_init(&ui, &texture));
    const uint64_t bytes = (uint64_t)WIDTH * HEIGHT * sizeof(uint32_t);
    uint32_t *const picture = test_alloc(t, bytes);
    uint32_t *const original = test_alloc(t, bytes);
    uint32_t *const first = test_alloc(t, bytes);
    for (uint32_t i = 0u; i < WIDTH * HEIGHT; i++) {
        picture[i] = 0xFF000000u | i * 2654435761u >> 8;
        original[i] = picture[i];
    }
    const uint8_t *const source = (const uint8_t *)picture;
    REQUIRE(ui_frame_begin_over(&ui, source, WIDTH, HEIGHT));
    ui_compose_fill(&ui.canvas, (ui_rect_t){4, 4, 20, 20}, 0x80FF0000u);
    const uint8_t *const composed = ui_frame_end(&ui, false);
    CHECK(composed != source);
    CHECK_MEM_EQ(picture, original, bytes);
    memcpy(first, composed, bytes);
    /* Translucent red twice over would differ. */
    uint32_t want = original[(uint64_t)10u * WIDTH + 10u];
    model_blend(&want, 0x80FF0000u);
    CHECK_EQ_U64(first[(uint64_t)10u * WIDTH + 10u], want);
    CHECK_EQ_U64(first[(uint64_t)30u * WIDTH + 30u], original[(uint64_t)30u * WIDTH + 30u]);
    REQUIRE(ui_frame_begin_over(&ui, source, WIDTH, HEIGHT));
    ui_compose_fill(&ui.canvas, (ui_rect_t){4, 4, 20, 20}, 0x80FF0000u);
    CHECK_MEM_EQ(ui_frame_end(&ui, false), first, bytes);
    /* Sizes it cannot do are refused with no frame started, so the next begin is not caught by its check. */
    CHECK(!ui_frame_begin_over(&ui, source, 0u, HEIGHT));
    CHECK(!ui_frame_begin_over(&ui, source, WIDTH, 0u));
    CHECK(!ui_frame_begin_over(&ui, source, UI_CANVAS_MAX_WIDTH + 1u, 1u));
    CHECK(!ui_frame_begin(&ui, (uint8_t *)picture, 3u, WIDTH, HEIGHT));
    CHECK(ui.widget.borrowed_canvas == nullptr);
    ui_deinit(&ui);
    texture_deinit(&texture);
}

TEST(memory_is_given_back_and_a_frame_takes_none_from_the_heap) {
    heap_counters_t start;
    heap_counters_read(&start);
    test_ui_t *const f = test_ui_alloc(t);
    REQUIRE(f != nullptr && test_ui_start(t, f));
    const uint64_t pixels = (uint64_t)SHEET_WIDTH * SHEET_HEIGHT;
    uint32_t *const picture = test_alloc(t, pixels * sizeof(uint32_t));
    /* What the budget in run/src/ui/index.c says about the tables. */
    CHECK(f->ui.arena_tables.used < 1024u * 1024u);
    CHECK_EQ_U64(f->ui.arena_tables.failed_count, 0u);

    /* The first frame over a picture reserves for it: that is the one allowed. */
    REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)picture, SHEET_WIDTH, SHEET_HEIGHT));
    sheet_all_widgets(f);
    (void)ui_frame_end(&f->ui, true);
    heap_counters_t after_first;
    heap_counters_read(&after_first);
    const uint64_t tables_used = f->ui.arena_tables.used;
    const uint64_t tables_allocs = f->ui.arena_tables.alloc_count;
    for (uint32_t frame = 0u; frame < 200u; frame++) {
        REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)picture, SHEET_WIDTH, SHEET_HEIGHT));
        sheet_all_widgets(f);
        (void)ui_frame_end(&f->ui, true);
    }
    /* A smaller size fits in what is there. */
    REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)picture, SHEET_WIDTH / 2u, SHEET_HEIGHT));
    (void)ui_frame_end(&f->ui, false);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, after_first.reserve_calls);
    CHECK_EQ_U64(after.release_calls, after_first.release_calls);
    CHECK_EQ_U64(f->ui.arena_tables.used, tables_used);
    CHECK_EQ_U64(f->ui.arena_tables.alloc_count, tables_allocs);
    CHECK_EQ_U64(f->ui.arena_frame.failed_count, 0u);

    /* A larger one moves to a larger block, once. */
    const uint64_t bigger = (uint64_t)2048u * 2048u * sizeof(uint32_t);
    uint32_t *const big = test_alloc(t, bigger);
    REQUIRE(ui_frame_begin_over(&f->ui, (const uint8_t *)big, 2048u, 2048u));
    (void)ui_frame_end(&f->ui, false);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, after_first.reserve_calls + 1u);
    CHECK_EQ_U64(after.release_calls, after_first.release_calls + 1u);

    test_ui_stop(f);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, start.live_bytes);
    CHECK_EQ_U64(after.live_regions, start.live_regions);
    /* Deinit twice, and on a zeroed one, does nothing. */
    ui_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    ui_deinit(&zeroed);
    ui_deinit(&zeroed);
}

TEST(a_ui_with_no_textures_does_not_start_and_leaves_nothing) {
    heap_counters_t start;
    heap_counters_read(&start);
    texture_t none;
    memset(&none, 0, sizeof(none));
    ui_t ui;
    CHECK(!ui_init(&ui, &none));
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, start.live_bytes);
    CHECK_EQ_U64(after.live_regions, start.live_regions);
}

TEST(the_program_starts_and_ends_with_its_ui_and_leaks_nothing) {
    REQUIRE(t->arg_count >= 1);
    char *const argv[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks", (char *)"2", nullptr};
    test_process_t process;
    test_process_run(t, argv, 120000u, &process);
    REQUIRE(process.started != 0u);
    CHECK(process.timed_out == 0u);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, " 0 bytes live");
}

TEST_MAIN()
