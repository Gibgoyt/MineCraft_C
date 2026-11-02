/*
 * ctest compile_texture_vector. See README.md. Arguments come from test.cmake: tool textures vector_yaml work_dir. Expected
 * values are worked out by hand in the comments beside them, so that this test does not merely repeat the code.
 */
#include "harness/index.h"
#include "texture_pack/mctex/index.h"
#include "texture_pack/support/index.h"
#include "texture_vector/analyze/index.h"
#include "texture_vector/materials/index.h"
#include "texture_vector/mcvec/index.h"
#include "texture_vector/palette/index.h"
#include "texture_vector/recipe/index.h"
#include "types/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TV_TEST_ARENA_BYTES (2ull << 20)
#define TV_TEST_TOOL_TIMEOUT_MS 60000u
#define TV_TEST_FORMAT_BYTES 4096u
/* FNV-1a of the pixels of TV_GOLDEN_RECIPE. Pins the generator and every operation in it: a change here changes every texture. */
#define TV_GOLDEN_HASH 5104691079017490019ull

static tp_arena_t tv_test_arena(test_context_t *t) {
    return (tp_arena_t){
        .borrowed_base = test_alloc(t, TV_TEST_ARENA_BYTES), .capacity = TV_TEST_ARENA_BYTES, .used = 0};
}

static const char *tv_format(test_context_t *t, const char *format, ...) {
    char *text = test_alloc(t, TV_TEST_FORMAT_BYTES);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, TV_TEST_FORMAT_BYTES, format, arguments);
    va_end(arguments);
    return text;
}

static void tv_set(uint8_t *pixels, uint32_t position, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    pixels[position * 4u] = red;
    pixels[position * 4u + 1u] = green;
    pixels[position * 4u + 2u] = blue;
    pixels[position * 4u + 3u] = alpha;
}

/* A grey opaque pixel, whose luma is exactly its value. */
static void tv_grey(uint8_t *pixels, uint32_t position, uint8_t value) {
    tv_set(pixels, position, value, value, value, 255);
}

static tv_image_t tv_image(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t frames) {
    return (tv_image_t){.borrowed_pixels = pixels,
                        .pixel_bytes = (uint64_t)width * height * frames * 4u,
                        .width = width,
                        .height = height,
                        .frames = frames};
}

static const tv_material_t TV_NO_MATERIAL = {.alpha_class = TV_ALPHA_FROM_PIXELS};

/* ---- Conversions ------------------------------------------------------------------------------------------------ */

TEST(luma_uses_rec_709_and_white_stays_255) {
    CHECK_EQ_U64(tv_luma(255, 255, 255), 255u);
    CHECK_EQ_U64(tv_luma(0, 0, 0), 0u);
    /* (54 * 255 + 128) >> 8 = 54, (183 * 255 + 128) >> 8 = 182, (19 * 255 + 128) >> 8 = 19. Green outweighs red outweighs blue. */
    CHECK_EQ_U64(tv_luma(255, 0, 0), 54u);
    CHECK_EQ_U64(tv_luma(0, 255, 0), 182u);
    CHECK_EQ_U64(tv_luma(0, 0, 255), 19u);
    for (uint32_t value = 0; value < 256u; value++) {
        CHECK_EQ_U64(tv_luma((uint8_t)value, (uint8_t)value, (uint8_t)value), value);
    }
}

TEST(linear_table_is_the_srgb_formula) {
    for (uint32_t i = 0; i < 256u; i++) {
        const float64_t c = (float64_t)i / 255.0f64;
        const float64_t linear = c <= 0.04045f64 ? c / 12.92f64 : pow((c + 0.055f64) / 1.055f64, 2.4f64);
        const int64_t expected = (int64_t)lround(linear * 65535.0f64);
        CHECK_EQ_I64((int64_t)tv_linear_from_srgb((uint8_t)i), expected);
    }
}

TEST(linear_conversion_round_trips_and_picks_the_nearest) {
    for (uint32_t i = 0; i < 256u; i++) {
        CHECK_EQ_U64(tv_srgb_from_linear(tv_linear_from_srgb((uint8_t)i)), i);
    }
    /* 32768 lies between entries 187 (32567) and 188 (32957), 201 and 189 away. */
    CHECK_EQ_U64(tv_srgb_from_linear(32768u), 188u);
    /* Between 0 and 20. 10 is exactly half way, and the lower wins; 11 is nearer the upper. */
    CHECK_EQ_U64(tv_srgb_from_linear(10u), 0u);
    CHECK_EQ_U64(tv_srgb_from_linear(11u), 1u);
    CHECK_EQ_U64(tv_srgb_from_linear(65535u), 255u);
}

/* ---- Alpha class and mean --------------------------------------------------------------------------------------- */

TEST(alpha_class_comes_from_every_frame) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4 * 4] = {0};
    tv_vector_t vector;
    tv_grey(pixels, 0, 100);
    tv_grey(pixels, 1, 100);
    tv_image_t image = tv_image(pixels, 2, 1, 1);
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_OPAQUE);
    CHECK_EQ_U64(vector.visible_pixels, 2u);

    pixels[7] = 0;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_CUTOUT);
    CHECK_EQ_U64(vector.visible_pixels, 1u);

    pixels[7] = 1;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_TRANSLUCENT);

    pixels[7] = 254;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_TRANSLUCENT);

    /* Only the last frame of two has a hole. */
    pixels[7] = 0;
    image = tv_image(pixels, 1, 1, 2);
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_CUTOUT);
}

TEST(alpha_class_can_be_overridden_and_nothing_else_moves) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4] = {0};
    tv_set(pixels, 0, 10, 20, 30, 255);
    const tv_image_t image = tv_image(pixels, 1, 1, 1);
    tv_vector_t vector;
    tv_material_t material = {.alpha_class = MCVEC_ALPHA_TRANSLUCENT};
    tv_analyze(&arena, &image, material, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_TRANSLUCENT);
    CHECK_EQ_U64(vector.mean_rgba[3], 255u);
    CHECK_EQ_U64(vector.visible_pixels, 1u);
}

TEST(mean_is_in_linear_light_not_srgb_bytes) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[2 * 4] = {0};
    tv_grey(pixels, 0, 0);
    tv_grey(pixels, 1, 255);
    const tv_image_t image = tv_image(pixels, 2, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    /* Half of 65535 is 32768, which is byte 188. Averaging bytes would give 128. */
    CHECK_EQ_U64(vector.mean_rgba[0], 188u);
    CHECK_EQ_U64(vector.mean_rgba[1], 188u);
    CHECK_EQ_U64(vector.mean_rgba[2], 188u);
    CHECK_EQ_U64(vector.mean_rgba[3], 255u);
    /* Luma's mean stays plain: (0 + 255 + 1) / 2. */
    CHECK_EQ_U64(vector.luma_mean, 128u);
}

TEST(mean_is_weighted_by_alpha) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[2 * 4] = {0};
    /* A transparent pixel's colour cannot show, so it cannot count. Red stays exactly red, and alpha is (255 + 0 + 1) / 2. */
    tv_set(pixels, 0, 200, 10, 10, 255);
    tv_set(pixels, 1, 0, 255, 0, 0);
    tv_image_t image = tv_image(pixels, 2, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.mean_rgba[0], 200u);
    CHECK_EQ_U64(vector.mean_rgba[1], 10u);
    CHECK_EQ_U64(vector.mean_rgba[2], 10u);
    CHECK_EQ_U64(vector.mean_rgba[3], 128u);

    /* White at 255 and black at 85: (65535 * 255 + 170) / 340 = 49151. Entries 224 (48850) and 225 (49344) are 301 and 193 away. */
    tv_set(pixels, 0, 255, 255, 255, 255);
    tv_set(pixels, 1, 0, 0, 0, 85);
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.mean_rgba[0], 225u);
    CHECK_EQ_U64(vector.mean_rgba[3], 170u);

    /* Now the white one is the faint one: (65535 * 85 + 170) / 340 = 16384. Entry 137 (16394) is nearest. */
    tv_set(pixels, 0, 255, 255, 255, 85);
    tv_set(pixels, 1, 0, 0, 0, 255);
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.mean_rgba[0], 137u);
}

TEST(fully_transparent_texture_has_no_mean) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[2 * 4] = {9, 9, 9, 0, 9, 9, 9, 0};
    const tv_image_t image = tv_image(pixels, 2, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.alpha_class, MCVEC_ALPHA_CUTOUT);
    CHECK_EQ_U64(vector.visible_pixels, 0u);
    CHECK_EQ_U64(vector.mean_rgba[0], 0u);
    CHECK_EQ_U64(vector.mean_rgba[3], 0u);
    CHECK_EQ_U64(vector.borrowed_height[0], 0u);
    CHECK_EQ_U64(vector.palette.count, 1u);
}

/* ---- Height ----------------------------------------------------------------------------------------------------- */

TEST(height_stretches_visible_luma_across_every_frame) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4 * 4] = {0};
    tv_grey(pixels, 0, 10);
    tv_grey(pixels, 1, 20);
    tv_set(pixels, 2, 255, 255, 255, 0); /* bright, and transparent: it must not count in the range */
    tv_grey(pixels, 3, 110);
    /* Two frames of two pixels: the second holds the range's top, and the first must still be stretched by it. */
    const tv_image_t image = tv_image(pixels, 2, 1, 2);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.luma_min, 10u);
    CHECK_EQ_U64(vector.luma_max, 110u);
    CHECK_EQ_U64(vector.borrowed_height[0], 0u);
    /* (10 * 255 + 50) / 100 = 26 */
    CHECK_EQ_U64(vector.borrowed_height[1], 26u);
    CHECK_EQ_U64(vector.borrowed_height[2], 0u);
    CHECK_EQ_U64(vector.borrowed_height[3], 255u);
}

TEST(height_of_a_nearly_flat_texture_is_flat) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[3 * 4] = {0};
    tv_grey(pixels, 0, 100);
    tv_grey(pixels, 1, 107); /* span 7 is under TV_HEIGHT_MIN_SPAN */
    tv_set(pixels, 2, 0, 0, 0, 0);
    tv_image_t image = tv_image(pixels, 3, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.borrowed_height[0], TV_HEIGHT_FLAT);
    CHECK_EQ_U64(vector.borrowed_height[1], TV_HEIGHT_FLAT);
    CHECK_EQ_U64(vector.borrowed_height[2], 0u);
    /* Span 8 is not. */
    tv_grey(pixels, 1, 108);
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.borrowed_height[0], 0u);
    CHECK_EQ_U64(vector.borrowed_height[1], 255u);
}

/* ---- Normal ----------------------------------------------------------------------------------------------------- */

TEST(normal_of_a_step_is_worked_by_hand_and_wraps) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4 * 4] = {0};
    tv_grey(pixels, 0, 0);
    tv_grey(pixels, 1, 0);
    tv_grey(pixels, 2, 255);
    tv_grey(pixels, 3, 255);
    const tv_image_t image = tv_image(pixels, 4, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    /*
     * With height 1 every row of the Sobel window is this row. At x = 1: Sobel x is (1 + 2 + 1) * (255 - 0) = 1020, so the
     * vector is (-3060, 0, 2040), length sqrt(13525200) = 3677, and x is -3060 * 127 / 3677 = -105.7, rounded to -106.
     * The surface rises to the right, so it faces left.
     */
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[1u * 2u], -106);
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[1u * 2u + 1u], 0);
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[2u * 2u], -106);
    /* x = 0 sees x = 3 on its left only by wrapping: it falls from 255 to 0, so it faces right. x = 3 likewise. */
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[0u * 2u], 106);
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[3u * 2u], 106);
}

TEST(normal_points_down_the_texture_for_a_step_in_y) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4 * 4] = {0};
    tv_grey(pixels, 0, 0);
    tv_grey(pixels, 1, 0);
    tv_grey(pixels, 2, 255);
    tv_grey(pixels, 3, 255);
    const tv_image_t image = tv_image(pixels, 1, 4, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    /* The same numbers. +y is down the texture, and this one rises going down, so at y = 1 it faces up: negative y. */
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[1u * 2u], 0);
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[1u * 2u + 1u], -106);
    CHECK_EQ_I64((int8_t)vector.borrowed_normal[0u * 2u + 1u], 106);
}

TEST(normal_is_flat_on_flat_and_at_transparent_edges) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[3 * 4] = {0};
    tv_grey(pixels, 0, 200);
    tv_grey(pixels, 1, 200);
    tv_set(pixels, 2, 255, 255, 255, 0);
    const tv_image_t image = tv_image(pixels, 3, 1, 1);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    /* A hole beside it counts as level with it, so a leaf's edge is no cliff. */
    for (uint32_t i = 0; i < 6u; i++) {
        CHECK_EQ_U64(vector.borrowed_normal[i], 0u);
    }
}

TEST(normal_is_per_frame_and_never_longer_than_one) {
    tp_arena_t arena = tv_test_arena(t);
    enum { WIDTH = 8, HEIGHT = 8, FRAMES = 3 };
    uint8_t pixels[WIDTH * HEIGHT * FRAMES * 4];
    uint64_t state = 1;
    for (uint32_t i = 0; i < WIDTH * HEIGHT * FRAMES; i++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        tv_grey(pixels, i, (uint8_t)(state >> 56));
    }
    /* Frame 1 is flat: the frames before and after it must not leak into it. */
    for (uint32_t i = WIDTH * HEIGHT; i < WIDTH * HEIGHT * 2u; i++) {
        tv_grey(pixels, i, 77);
    }
    const tv_image_t image = tv_image(pixels, WIDTH, HEIGHT, FRAMES);
    tv_vector_t vector;
    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    for (uint32_t i = 0; i < WIDTH * HEIGHT * FRAMES; i++) {
        const int32_t x = (int8_t)vector.borrowed_normal[i * 2u];
        const int32_t y = (int8_t)vector.borrowed_normal[i * 2u + 1u];
        /* Rounding may add just under a unit to each. */
        CHECK(x * x + y * y <= 128 * 128);
        if (i >= WIDTH * HEIGHT && i < WIDTH * HEIGHT * 2u) {
            CHECK(x == 0 && y == 0);
        }
    }
}

/* ---- Emissive --------------------------------------------------------------------------------------------------- */

TEST(emissive_classes_and_rules) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[4 * 4] = {0};
    tv_set(pixels, 0, 250, 200, 40, 255);  /* a flame: luma 195, warm */
    tv_set(pixels, 1, 90, 60, 30, 255);    /* a stick: luma 65, warm */
    tv_set(pixels, 2, 200, 200, 200, 255); /* a white stone: luma 200, not warm */
    tv_set(pixels, 3, 255, 200, 40, 0);    /* bright and warm, and invisible */
    const tv_image_t image = tv_image(pixels, 4, 1, 1);
    tv_vector_t vector;

    tv_analyze(&arena, &image, TV_NO_MATERIAL, &vector);
    CHECK_EQ_U64(vector.emissive_class, MCVEC_EMISSIVE_NONE);
    CHECK_EQ_U64(vector.emissive_level, 0u);
    CHECK_EQ_U64(vector.emissive_pixels, 0u);

    tv_material_t material = {.emissive_level = 14, .alpha_class = TV_ALPHA_FROM_PIXELS};
    tv_analyze(&arena, &image, material, &vector);
    CHECK_EQ_U64(vector.emissive_class, MCVEC_EMISSIVE_FULL);
    CHECK_EQ_U64(vector.emissive_pixels, 3u);
    CHECK_EQ_U64(vector.borrowed_emissive[3], 0u);

    material.min_luma = 100;
    tv_analyze(&arena, &image, material, &vector);
    CHECK_EQ_U64(vector.emissive_class, MCVEC_EMISSIVE_PARTIAL);
    CHECK_EQ_U64(vector.borrowed_emissive[0], 14u);
    CHECK_EQ_U64(vector.borrowed_emissive[1], 0u);
    CHECK_EQ_U64(vector.borrowed_emissive[2], 14u);

    material.warm = 1;
    tv_analyze(&arena, &image, material, &vector);
    CHECK_EQ_U64(vector.emissive_pixels, 1u);
    CHECK_EQ_U64(vector.borrowed_emissive[0], 14u);
    CHECK_EQ_U64(vector.borrowed_emissive[2], 0u);
    CHECK_EQ_U64(vector.emissive_level, 14u);

    /* A level with nothing bright enough gives no light at all: the class must not claim any. */
    material.min_luma = 255;
    material.warm = 0;
    tv_analyze(&arena, &image, material, &vector);
    CHECK_EQ_U64(vector.emissive_class, MCVEC_EMISSIVE_NONE);
    CHECK_EQ_U64(vector.emissive_level, 0u);
}

/* ---- Palette ---------------------------------------------------------------------------------------------------- */

TEST(palette_is_exact_and_most_used_first) {
    tp_arena_t arena = tv_test_arena(t);
    uint8_t pixels[6 * 4] = {0};
    tv_set(pixels, 0, 1, 2, 3, 255);
    tv_set(pixels, 1, 9, 9, 9, 255);
    tv_set(pixels, 2, 9, 9, 9, 255);
    tv_set(pixels, 3, 9, 9, 9, 255);
    tv_set(pixels, 4, 200, 0, 0, 0); /* alpha 0: counts as #00000000 */
    tv_set(pixels, 5, 0, 200, 0, 0); /* and so does this, with it */
    const tv_image_t image = tv_image(pixels, 6, 1, 1);
    tv_palette_t palette;
    uint8_t indices[6];
    tv_palette_build(&arena, &image, &palette, indices);
    CHECK_EQ_U64(palette.count, 3u);
    CHECK_EQ_U64(palette.exact, 1u);
    const uint8_t expected_palette[12] = {9, 9, 9, 255, 0, 0, 0, 0, 1, 2, 3, 255};
    CHECK_MEM_EQ(palette.rgba, expected_palette, 12u);
    const uint8_t expected_indices[6] = {2, 0, 0, 0, 1, 1};
    CHECK_MEM_EQ(indices, expected_indices, 6u);
    CHECK_EQ_U64(arena.used, 0u); /* scratch memory goes back */
}

TEST(palette_stays_exact_at_256_colours) {
    tp_arena_t arena = tv_test_arena(t);
    enum { COUNT = 256 };
    uint8_t pixels[COUNT * 4];
    for (uint32_t i = 0; i < COUNT; i++) {
        tv_set(pixels, i, (uint8_t)i, (uint8_t)(255u - i), 7, 255);
    }
    const tv_image_t image = tv_image(pixels, COUNT, 1, 1);
    tv_palette_t palette;
    uint8_t indices[COUNT];
    tv_palette_build(&arena, &image, &palette, indices);
    CHECK_EQ_U64(palette.count, 256u);
    CHECK_EQ_U64(palette.exact, 1u);
    for (uint32_t i = 0; i < COUNT; i++) {
        CHECK_MEM_EQ(&palette.rgba[(uint32_t)indices[i] * 4u], &pixels[i * 4u], 4u);
    }
}

/* Many colours in a smooth ramp: what a median cut has to summarize. */
static void tv_fill_ramp(uint8_t *pixels, uint32_t width, uint32_t height, bool reversed) {
    const uint32_t count = width * height;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t x = i % width;
        const uint32_t y = i / width;
        const uint32_t at = reversed ? count - 1u - i : i;
        tv_set(pixels, at, (uint8_t)(x * 6u), (uint8_t)(y * 10u), (uint8_t)((x + y) * 3u), 255);
    }
}

TEST(palette_over_256_colours_cuts_to_256_near_ones) {
    tp_arena_t arena = tv_test_arena(t);
    enum { WIDTH = 40, HEIGHT = 25 };
    uint8_t pixels[WIDTH * HEIGHT * 4];
    tv_fill_ramp(pixels, WIDTH, HEIGHT, false);
    const tv_image_t image = tv_image(pixels, WIDTH, HEIGHT, 1);
    tv_palette_t palette;
    uint8_t indices[WIDTH * HEIGHT];
    tv_palette_build(&arena, &image, &palette, indices);
    CHECK_EQ_U64(palette.count, 256u);
    CHECK_EQ_U64(palette.exact, 0u);
    uint64_t total_error = 0;
    uint32_t worst = 0;
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        for (uint32_t c = 0; c < 4u; c++) {
            const int32_t gap = (int32_t)palette.rgba[(uint32_t)indices[i] * 4u + c] - (int32_t)pixels[i * 4u + c];
            const uint32_t size = (uint32_t)(gap < 0 ? -gap : gap);
            total_error += size;
            worst = size > worst ? size : worst;
        }
    }
    /* 1000 colours in 256 boxes over a cube of 234 by 240 by 189: each box is about 25 wide at the most, so about 13 from its mean. */
    CHECK(worst <= 16u);
    CHECK(total_error / (WIDTH * HEIGHT * 3u) <= 6u);
}

TEST(palette_does_not_depend_on_the_order_of_pixels) {
    tp_arena_t arena = tv_test_arena(t);
    enum { WIDTH = 40, HEIGHT = 25 };
    uint8_t forward[WIDTH * HEIGHT * 4];
    uint8_t backward[WIDTH * HEIGHT * 4];
    tv_fill_ramp(forward, WIDTH, HEIGHT, false);
    tv_fill_ramp(backward, WIDTH, HEIGHT, true);
    const tv_image_t image_forward = tv_image(forward, WIDTH, HEIGHT, 1);
    const tv_image_t image_backward = tv_image(backward, WIDTH, HEIGHT, 1);
    tv_palette_t left;
    tv_palette_t right;
    uint8_t indices[WIDTH * HEIGHT];
    tv_palette_build(&arena, &image_forward, &left, indices);
    tv_palette_build(&arena, &image_backward, &right, indices);
    CHECK_MEM_EQ(left.rgba, right.rgba, sizeof(left.rgba));
    CHECK_EQ_U64(left.count, right.count);
}

TEST(palette_keeps_a_lone_outlier) {
    tp_arena_t arena = tv_test_arena(t);
    enum { COUNT = 400 };
    uint8_t pixels[COUNT * 4];
    for (uint32_t i = 0; i < COUNT; i++) {
        tv_set(pixels, i, (uint8_t)(i % 20u), (uint8_t)(i / 20u), 0, 255);
    }
    tv_set(pixels, COUNT - 1u, 255, 255, 255, 255);
    const tv_image_t image = tv_image(pixels, COUNT, 1, 1);
    tv_palette_t palette;
    uint8_t indices[COUNT];
    tv_palette_build(&arena, &image, &palette, indices);
    /* One pixel in 400, and the widest channel by far: it must not be averaged away into a dark box. */
    CHECK_EQ_U64(palette.rgba[(uint32_t)indices[COUNT - 1u] * 4u], 255u);
}

/* ---- .mcvec ----------------------------------------------------------------------------------------------------- */

/* A small file of 2 by 1 pixels and 2 palette entries, written into out, which holds 256 bytes. Returns its size. */
static uint64_t tv_small_mcvec(uint8_t *out) {
    const uint8_t palette[8] = {1, 2, 3, 255, 4, 5, 6, 255};
    const uint8_t indices[2] = {1, 0};
    const uint8_t height[2] = {10, 20};
    const uint8_t normal[4] = {30, 31, 32, 33};
    const uint8_t emissive[2] = {0, 14};
    const mcvec_header_t header = {.width = 2,
                                   .height = 1,
                                   .frame_count = 1,
                                   .palette_count = 2,
                                   .source_hash = 77,
                                   .mean_rgba = {1, 2, 3, 4},
                                   .alpha_class = MCVEC_ALPHA_OPAQUE,
                                   .emissive_class = MCVEC_EMISSIVE_PARTIAL,
                                   .emissive_level = 14,
                                   .palette_exact = 1,
                                   .luma_min = 5,
                                   .luma_max = 6,
                                   .luma_mean = 5,
                                   .visible_pixels = 2,
                                   .emissive_pixels = 1};
    const mcvec_status_t status = mcvec_encode(out, 256, &header, palette, indices, height, normal, emissive);
    return status == MCVEC_OK ? 64u + 8u + 2u * 5u : 0u;
}

TEST(mcvec_round_trips) {
    uint8_t file[256] = {0};
    const uint64_t bytes = tv_small_mcvec(file);
    REQUIRE(bytes == 82u);
    mcvec_view_t view;
    REQUIRE(mcvec_parse(file, bytes, &view) == MCVEC_OK);
    CHECK_EQ_U64(view.header.magic, MCVEC_MAGIC);
    CHECK_MEM_EQ(file, "MCTV", 4u);
    CHECK_EQ_U64(view.header.source_hash, 77u);
    CHECK_EQ_U64(view.pixel_count, 2u);
    CHECK_EQ_U64(view.borrowed_palette[4], 4u);
    CHECK_EQ_U64(view.borrowed_indices[0], 1u);
    CHECK_EQ_U64(view.borrowed_height[1], 20u);
    CHECK_EQ_U64(view.borrowed_normal[3], 33u);
    CHECK_EQ_U64(view.borrowed_emissive[1], 14u);
    CHECK_EQ_U64(view.header.payload_hash, mctex_hash(file + 64u, bytes - 64u));
}

TEST(mcvec_refuses_every_truncation_and_trailing_byte) {
    uint8_t file[256] = {0};
    const uint64_t bytes = tv_small_mcvec(file);
    mcvec_view_t view;
    for (uint64_t length = 0; length < bytes; length++) {
        CHECK_EQ_U64(mcvec_parse(file, length, &view), MCVEC_TRUNCATED);
        CHECK(view.borrowed_palette == nullptr);
    }
    CHECK_EQ_U64(mcvec_parse(file, bytes + 1u, &view), MCVEC_TRAILING);
}

TEST(mcvec_refuses_every_bad_field) {
    typedef struct field_case_t {
        uint32_t offset;  /* 0 */
        uint8_t bytes;    /* 4: 1 or 4 */
        uint8_t status;   /* 5 */
        uint8_t _pad0[2]; /* 6 */
        uint32_t value;   /* 8 */
    } field_case_t;
    static_assert(sizeof(field_case_t) == 12);
    const field_case_t cases[] = {
        {0, 4, MCVEC_BAD_MAGIC, {0}, 0x12345678u},
        {4, 4, MCVEC_BAD_VERSION, {0}, 2u},
        {8, 4, MCVEC_BAD_SIZE, {0}, 0u},
        {8, 4, MCVEC_BAD_SIZE, {0}, MCTEX_MAX_DIMENSION + 1u},
        {12, 4, MCVEC_BAD_SIZE, {0}, 0u},
        {16, 4, MCVEC_BAD_SIZE, {0}, 0u},
        {16, 4, MCVEC_BAD_SIZE, {0}, MCTEX_MAX_FRAMES + 1u},
        {20, 4, MCVEC_BAD_SIZE, {0}, 0u},
        {20, 4, MCVEC_BAD_SIZE, {0}, MCVEC_MAX_PALETTE + 1u},
        {44, 1, MCVEC_BAD_CLASS, {0}, MCVEC_ALPHA_CLASS_COUNT},
        {45, 1, MCVEC_BAD_CLASS, {0}, MCVEC_EMISSIVE_CLASS_COUNT},
        {45, 1, MCVEC_BAD_CLASS, {0}, MCVEC_EMISSIVE_NONE}, /* its level and pixels say otherwise */
        {46, 1, MCVEC_BAD_CLASS, {0}, 16u},
        {46, 1, MCVEC_BAD_CLASS, {0}, 0u},
        {56, 4, MCVEC_BAD_CLASS, {0}, 0u},
        {51, 1, MCVEC_BAD_PADDING, {0}, 1u},
        {60, 4, MCVEC_BAD_PADDING, {0}, 1u},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t file[256] = {0};
        const uint64_t bytes = tv_small_mcvec(file);
        memcpy(file + cases[i].offset, &cases[i].value, cases[i].bytes);
        mcvec_view_t view;
        CHECK_EQ_U64(mcvec_parse(file, bytes, &view), cases[i].status);
    }
}

TEST(mcvec_catches_every_bit_of_damage) {
    uint8_t file[256] = {0};
    const uint64_t bytes = tv_small_mcvec(file);
    mcvec_view_t view;
    for (uint64_t bit = 64u * 8u; bit < bytes * 8u; bit++) {
        file[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        CHECK_EQ_U64(mcvec_parse(file, bytes, &view), MCVEC_BAD_HASH);
        file[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
    CHECK_EQ_U64(mcvec_parse(file, bytes, &view), MCVEC_OK);
}

TEST(mcvec_refuses_an_index_past_the_palette) {
    uint8_t file[256] = {0};
    const uint64_t bytes = tv_small_mcvec(file);
    file[64u + 8u] = 2u; /* the palette has entries 0 and 1 */
    mcvec_header_t header;
    memcpy(&header, file, sizeof(header));
    header.payload_hash = mctex_hash(file + 64u, bytes - 64u);
    memcpy(file, &header, sizeof(header));
    mcvec_view_t view;
    CHECK_EQ_U64(mcvec_parse(file, bytes, &view), MCVEC_BAD_INDEX);
}

TEST(mcvec_encode_refuses_a_small_buffer_and_status_text_is_total) {
    uint8_t file[81];
    const uint8_t zeros[16] = {0};
    const mcvec_header_t header = {.width = 2, .height = 1, .frame_count = 1, .palette_count = 2};
    CHECK_EQ_U64(mcvec_encode(file, sizeof(file), &header, zeros, zeros, zeros, zeros, zeros), MCVEC_NO_ROOM);
    for (uint32_t status = 0; status < 256u; status++) {
        CHECK(mcvec_status_text((mcvec_status_t)status) != nullptr);
    }
    uint64_t bytes = 0;
    CHECK(!mcvec_file_bytes(1, 1, 1, 0, &bytes));
    CHECK(!mcvec_file_bytes(1, 1, 1, 257, &bytes));
    CHECK(!mcvec_file_bytes(0, 1, 1, 1, &bytes));
    CHECK(mcvec_file_bytes(1, 1, 1, 256, &bytes));
    CHECK_EQ_U64(bytes, 64u + 1024u + 5u);
}

/* ---- Recipes ---------------------------------------------------------------------------------------------------- */

/*
 * Runs a recipe body: the text of the mapping `source:` would hold. Pixels go in a test allocation. Returns them, or nullptr
 * with the error in out_error, formatted as "line:column: message".
 */
static const uint8_t *tv_run_recipe(test_context_t *t, const char *body, uint32_t width, uint32_t height,
                                    uint32_t frames, const char **out_error) {
    tp_arena_t arena = tv_test_arena(t);
    const uint64_t length = strlen(body);
    const yaml_storage_t storage = {.nodes = test_alloc(t, sizeof(yaml_node_t) * (length + 1u)),
                                    .strings = test_alloc(t, length + 1u),
                                    .node_capacity = (uint32_t)(length + 1u),
                                    .string_capacity = (uint32_t)length};
    yaml_document_t document;
    yaml_error_t error = {0};
    *out_error = "";
    if (!yaml_parse(&document, (const uint8_t *)body, length, storage, &error)) {
        *out_error = tv_format(t, "yaml %u:%u: %s", error.line, error.column, error.message);
        return nullptr;
    }
    uint8_t *pixels = test_alloc(t, (uint64_t)width * height * frames * 4u);
    if (!tv_recipe_run(&arena, &document, yaml_root(&document), width, height, frames, pixels, &error)) {
        *out_error = tv_format(t, "%u:%u: %s", error.line, error.column, error.message);
        return nullptr;
    }
    CHECK_EQ_U64(arena.used, 0u); /* scratch memory goes back */
    return pixels;
}

/* A recipe whose ops are one line each. */
static const uint8_t *tv_run_ops(test_context_t *t, const char *ops, uint32_t width, uint32_t height) {
    const char *error = "";
    const uint8_t *pixels = tv_run_recipe(t, tv_format(t, "seed: 5\nops:\n%s", ops), width, height, 1, &error);
    if (pixels == nullptr) {
        test_check_contains(t, error, "(no error expected)", __FILE__, __LINE__, "recipe error");
    }
    return pixels;
}

/* Compares a canvas with rows of text, a character per pixel: '.' is transparent and the others are in the given colours. */
static void tv_check_rows(test_context_t *t, const uint8_t *pixels, uint32_t width, uint32_t height, const char *rows,
                          const char *letters, const uint8_t *colours) {
    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t expected[4] = {0, 0, 0, 0};
        const char *letter = strchr(letters, rows[i]);
        if (rows[i] != '.' && letter != nullptr) {
            memcpy(expected, colours + (uint64_t)(letter - letters) * 4u, 4u);
        }
        if (memcmp(pixels + (uint64_t)i * 4u, expected, 4u) != 0) {
            test_check(t, false, __FILE__, __LINE__,
                       tv_format(t, "pixel (%u, %u) is %u %u %u %u, and row text says '%c'", i % width, i / width,
                                 pixels[i * 4u], pixels[i * 4u + 1u], pixels[i * 4u + 2u], pixels[i * 4u + 3u],
                                 rows[i]));
        }
    }
    CHECK(true);
}

static const uint8_t TV_RED[4] = {255, 0, 0, 255};

TEST(recipe_starts_transparent_and_fills_a_region) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: fill, color: \"#ff0000\", x: 1, y: 1, w: 2, h: 1 }\n", 4, 3);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 4, 3,
                  "...."
                  ".rr."
                  "....",
                  "r", TV_RED);
}

TEST(recipe_colour_forms) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: fill, color: \"#0a0B0c\" }\n", 1, 1);
    REQUIRE(pixels != nullptr);
    const uint8_t opaque[4] = {10, 11, 12, 255};
    CHECK_MEM_EQ(pixels, opaque, 4u);
    pixels = tv_run_ops(t, "  - { op: fill, color: \"#01020304\" }\n", 1, 1);
    REQUIRE(pixels != nullptr);
    const uint8_t translucent[4] = {1, 2, 3, 4};
    CHECK_MEM_EQ(pixels, translucent, 4u);
}

TEST(recipe_outline) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: outline, color: \"#ff0000\", x: 1, y: 0, w: 4, h: 3 }\n", 5, 4);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 5, 4,
                  ".rrrr"
                  ".r..r"
                  ".rrrr"
                  ".....",
                  "r", TV_RED);
}

TEST(recipe_line_is_bresenham_and_clipped) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: line, color: \"#ff0000\", x0: 0, y0: 0, x1: 4, y1: 2 }\n", 5, 3);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 5, 3,
                  "r...."
                  ".rr.."
                  "...rr",
                  "r", TV_RED);
    /* Reversed ends draw a line too, and what lies outside is dropped. */
    pixels = tv_run_ops(t, "  - { op: line, color: \"#ff0000\", x0: 5, y0: 1, x1: -3, y1: 1 }\n", 3, 3);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 3, 3,
                  "..."
                  "rrr"
                  "...",
                  "r", TV_RED);
}

TEST(recipe_disc) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: disc, color: \"#ff0000\", cx: 2, cy: 2, rx: 2, ry: 2 }\n", 5, 5);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 5, 5,
                  ".rrr."
                  "rrrrr"
                  "rrrrr"
                  "rrrrr"
                  ".rrr.",
                  "r", TV_RED);
    /* An ellipse, half off the canvas. */
    pixels = tv_run_ops(t, "  - { op: disc, color: \"#ff0000\", cx: 0, cy: 1, rx: 2, ry: 0 }\n", 3, 3);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 3, 3,
                  "..."
                  "rrr"
                  "...",
                  "r", TV_RED);
}

TEST(recipe_gradient) {
    const uint8_t *pixels = tv_run_ops(
        t, "  - { op: gradient, from: \"#000000\", to: \"#c86400\", axis: x, x: 0, y: 0, w: 3, h: 1 }\n", 3, 1);
    REQUIRE(pixels != nullptr);
    /* 200 and 100 over two steps. */
    const uint8_t expected[12] = {0, 0, 0, 255, 100, 50, 0, 255, 200, 100, 0, 255};
    CHECK_MEM_EQ(pixels, expected, 12u);
    pixels = tv_run_ops(t, "  - { op: gradient, from: \"#640000\", to: \"#000000\", axis: y }\n", 1, 2);
    REQUIRE(pixels != nullptr);
    const uint8_t falling[8] = {100, 0, 0, 255, 0, 0, 0, 255};
    CHECK_MEM_EQ(pixels, falling, 8u);
    /* One pixel long: the start, and no division by zero. */
    pixels = tv_run_ops(t, "  - { op: gradient, from: \"#640000\", to: \"#000000\", axis: x }\n", 1, 1);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[0], 100u);
}

TEST(recipe_tint) {
    const uint8_t *pixels =
        tv_run_ops(t, "  - { op: fill, color: \"#c8ff0040\" }\n  - { op: tint, color: \"#80ffff\" }\n", 1, 1);
    REQUIRE(pixels != nullptr);
    /* (200 * 128 + 127) / 255 = 100. Green keeps 255, blue keeps 0, and alpha is untouched. */
    const uint8_t expected[4] = {100, 255, 0, 64};
    CHECK_MEM_EQ(pixels, expected, 4u);
}

TEST(recipe_bricks_are_a_running_bond) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: bricks, color: \"#ff0000\", brick_w: 4, brick_h: 2 }\n", 8, 6);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 8, 6,
                  "rrrrrrrr"
                  "r...r..."
                  "rrrrrrrr"
                  "..r...r."
                  "rrrrrrrr"
                  "r...r...",
                  "r", TV_RED);
}

TEST(recipe_copy_flips_and_may_overlap) {
    const uint8_t *pixels = tv_run_ops(t,
                                       "  - { op: fill, color: \"#ff0000\", x: 0, y: 0, w: 1, h: 1 }\n"
                                       "  - { op: copy, x: 0, y: 0, w: 2, h: 1, to_x: 2, to_y: 1, flip: x }\n",
                                       4, 2);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 4, 2,
                  "r..."
                  "...r",
                  "r", TV_RED);
    pixels = tv_run_ops(t,
                        "  - { op: fill, color: \"#ff0000\", x: 0, y: 0, w: 1, h: 1 }\n"
                        "  - { op: copy, x: 0, y: 0, w: 1, h: 2, to_x: 0, to_y: 2, flip: y }\n",
                        1, 4);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 1, 4, "r..r", "r", TV_RED);
    /* Moved one to the right over itself, it must be a copy of what was there, not a smear. */
    pixels = tv_run_ops(t,
                        "  - { op: fill, color: \"#ff0000\", x: 0, y: 0, w: 1, h: 1 }\n"
                        "  - { op: copy, x: 0, y: 0, w: 3, h: 1, to_x: 1, to_y: 0 }\n",
                        4, 1);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 4, 1, "rr..", "r", TV_RED);
}

TEST(recipe_art) {
    const uint8_t *pixels = tv_run_ops(t,
                                       "  - { op: fill, color: \"#ff0000\" }\n"
                                       "  - op: art\n"
                                       "    x: 1\n"
                                       "    y: 1\n"
                                       "    palette: { a: \"#00ff00\", \"#\": \"#0000ff\" }\n"
                                       "    rows:\n"
                                       "      - \"a.\"\n"
                                       "      - \".#\"\n",
                                       3, 3);
    REQUIRE(pixels != nullptr);
    const uint8_t colours[12] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    tv_check_rows(t, pixels, 3, 3,
                  "rrr"
                  "rar"
                  "rrb",
                  "rab", colours);
}

TEST(recipe_speckle_chance_and_holes) {
    const uint8_t *pixels = tv_run_ops(t, "  - { op: speckle, color: \"#ff0000\", permille: 1000 }\n", 2, 1);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 2, 1, "rr", "r", TV_RED);
    pixels = tv_run_ops(t, "  - { op: speckle, color: \"#ff0000\", permille: 0 }\n", 2, 1);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 2, 1, "..", "r", TV_RED);
    /* About half of 10000: within 4 standard deviations of 50. */
    pixels = tv_run_ops(t, "  - { op: speckle, color: \"#ff0000\", permille: 500 }\n", 100, 100);
    REQUIRE(pixels != nullptr);
    uint32_t painted = 0;
    for (uint32_t i = 0; i < 10000u; i++) {
        painted += pixels[i * 4u + 3u] != 0 ? 1u : 0u;
    }
    CHECK(painted > 4800u && painted < 5200u);
    /* A transparent colour punches holes, and those holes are real. */
    pixels = tv_run_ops(
        t, "  - { op: fill, color: \"#ff0000\" }\n  - { op: speckle, color: \"#00000000\", permille: 1000 }\n", 2, 1);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 2, 1, "..", "r", TV_RED);
}

TEST(recipe_visible_keeps_unused_space_empty) {
    const uint8_t *pixels = tv_run_ops(t,
                                       "  - { op: fill, color: \"#00ff00\", x: 0, y: 0, w: 1, h: 2 }\n"
                                       "  - { op: speckle, color: \"#ff0000\", permille: 1000, visible: true }\n",
                                       2, 2);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 2, 2, "r.r.", "r", TV_RED);
    pixels =
        tv_run_ops(t,
                   "  - { op: fill, color: \"#00ff00\", x: 0, y: 0, w: 1, h: 2 }\n"
                   "  - { op: blobs, color: \"#ff0000\", count: 200, radius_min: 0, radius_max: 0, visible: true }\n",
                   2, 2);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[1u * 4u + 3u], 0u);
    CHECK_EQ_U64(pixels[3u * 4u + 3u], 0u);
    CHECK_EQ_U64(pixels[0u * 4u], 255u);
}

TEST(recipe_blobs_stay_in_their_region_and_wrap) {
    /* Count 0 paints nothing. Radius 0 paints one pixel each. */
    const uint8_t *pixels =
        tv_run_ops(t, "  - { op: blobs, color: \"#ff0000\", count: 0, radius_min: 1, radius_max: 2 }\n", 2, 2);
    REQUIRE(pixels != nullptr);
    tv_check_rows(t, pixels, 2, 2, "....", "r", TV_RED);
    pixels = tv_run_ops(t, "  - { op: blobs, color: \"#ff0000\", count: 1, radius_min: 0, radius_max: 0 }\n", 5, 5);
    REQUIRE(pixels != nullptr);
    uint32_t painted = 0;
    for (uint32_t i = 0; i < 25u; i++) {
        painted += pixels[i * 4u + 3u] != 0 ? 1u : 0u;
    }
    CHECK_EQ_U64(painted, 1u);
    /* Large blobs in a small region: nothing outside it, and nothing runs off the canvas. */
    pixels = tv_run_ops(
        t, "  - { op: blobs, color: \"#ff0000\", count: 30, radius_min: 2, radius_max: 3, x: 2, y: 2, w: 3, h: 3 }\n",
        8, 8);
    REQUIRE(pixels != nullptr);
    uint32_t inside = 0;
    for (uint32_t y = 0; y < 8u; y++) {
        for (uint32_t x = 0; x < 8u; x++) {
            const bool is_in = x >= 2u && x < 5u && y >= 2u && y < 5u;
            const bool is_painted = pixels[(y * 8u + x) * 4u + 3u] != 0;
            CHECK(is_in || !is_painted);
            inside += is_painted ? 1u : 0u;
        }
    }
    CHECK(inside > 0u);
    /* Jitter stays around the colour, and is applied. */
    pixels = tv_run_ops(
        t, "  - { op: blobs, color: \"#808080\", count: 40, radius_min: 2, radius_max: 2, jitter: 9 }\n", 16, 16);
    REQUIRE(pixels != nullptr);
    uint32_t low = 255;
    uint32_t high = 0;
    for (uint32_t i = 0; i < 256u; i++) {
        if (pixels[i * 4u + 3u] != 0) {
            low = pixels[i * 4u] < low ? pixels[i * 4u] : low;
            high = pixels[i * 4u] > high ? pixels[i * 4u] : high;
        }
    }
    CHECK(low >= 128u - 9u && high <= 128u + 9u && low < high);
}

TEST(recipe_noise_stays_in_bounds_and_on_visible_pixels) {
    const uint8_t *pixels = tv_run_ops(
        t, "  - { op: fill, color: \"#646464\", x: 0, y: 0, w: 8, h: 16 }\n  - { op: noise, amount: 20 }\n", 16, 16);
    REQUIRE(pixels != nullptr);
    bool changed = false;
    for (uint32_t y = 0; y < 16u; y++) {
        for (uint32_t x = 0; x < 16u; x++) {
            const uint8_t *pixel = pixels + (y * 16u + x) * 4u;
            if (x >= 8u) {
                CHECK_EQ_U64(pixel[3], 0u);
                CHECK_EQ_U64(pixel[0], 0u);
            } else {
                CHECK(pixel[0] >= 80u && pixel[0] <= 120u);
                /* Grey stays grey: noise moves all three channels together. */
                CHECK_EQ_U64(pixel[0], pixel[1]);
                CHECK_EQ_U64(pixel[0], pixel[2]);
                CHECK_EQ_U64(pixel[3], 255u);
                changed = changed || pixel[0] != 100u;
            }
        }
    }
    CHECK(changed);
    /* Amount 0 changes nothing, and clamping holds at the ends. */
    pixels = tv_run_ops(t, "  - { op: fill, color: \"#646464\" }\n  - { op: noise, amount: 0 }\n", 4, 4);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[0], 100u);
    pixels = tv_run_ops(t, "  - { op: fill, color: \"#fe0101\" }\n  - { op: noise, amount: 200 }\n", 4, 4);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(pixels[3], 255u);
}

TEST(recipe_noise_tiles_across_the_seam) {
    const uint8_t *pixels =
        tv_run_ops(t, "  - { op: fill, color: \"#808080\" }\n  - { op: noise, amount: 100, cell: 4 }\n", 16, 16);
    REQUIRE(pixels != nullptr);
    uint32_t worst_inside = 0;
    uint32_t worst_seam = 0;
    for (uint32_t y = 0; y < 16u; y++) {
        for (uint32_t x = 0; x < 16u; x++) {
            const int32_t here = pixels[(y * 16u + x) * 4u];
            const int32_t next = pixels[(y * 16u + (x + 1u) % 16u) * 4u];
            const uint32_t gap = (uint32_t)(here > next ? here - next : next - here);
            if (x == 15u) {
                worst_seam = gap > worst_seam ? gap : worst_seam;
            } else {
                worst_inside = gap > worst_inside ? gap : worst_inside;
            }
        }
    }
    /* Across the seam it is no rougher than anywhere else, as it would be if the lattice did not wrap. */
    CHECK(worst_seam <= worst_inside);
    CHECK(worst_inside > 0u);
}

TEST(recipe_noise_cell_axes_make_grain) {
    /* Each cell_y spans the canvas: all of one column is alike, so it runs in vertical streaks. */
    const uint8_t *pixels = tv_run_ops(
        t, "  - { op: fill, color: \"#808080\" }\n  - { op: noise, amount: 60, cell_x: 1, cell_y: 16 }\n", 16, 16);
    REQUIRE(pixels != nullptr);
    bool any_across = false;
    for (uint32_t y = 0; y < 16u; y++) {
        for (uint32_t x = 0; x < 16u; x++) {
            CHECK_EQ_U64(pixels[(y * 16u + x) * 4u], pixels[x * 4u]);
            any_across = any_across || pixels[x * 4u] != pixels[0];
        }
    }
    CHECK(any_across);
}

/* Every operation, once. */
static const char TV_GOLDEN_RECIPE[] =
    "seed: 12345\n"
    "ops:\n"
    "  - { op: fill, color: \"#7f6a55\" }\n"
    "  - { op: noise, amount: 30, cell: 4 }\n"
    "  - { op: noise, amount: 15, cell_x: 1, cell_y: 8, seed: 3 }\n"
    "  - { op: speckle, color: \"#203040\", permille: 90 }\n"
    "  - { op: blobs, color: \"#a04020\", count: 3, radius_min: 1, radius_max: 2, jitter: 8 }\n"
    "  - { op: bricks, color: \"#101010\", brick_w: 8, brick_h: 4, x: 0, y: 8, w: 16, h: 8 }\n"
    "  - { op: outline, color: \"#ffffff\", x: 2, y: 2, w: 5, h: 5 }\n"
    "  - { op: line, color: \"#00ff00\", x0: 0, y0: 15, x1: 15, y1: 0 }\n"
    "  - { op: disc, color: \"#0000ff\", cx: 12, cy: 4, rx: 2, ry: 3 }\n"
    "  - { op: gradient, from: \"#000000\", to: \"#ffffff\", axis: x, x: 0, y: 0, w: 16, h: 1 }\n"
    "  - { op: tint, color: \"#c0c0ff\", x: 8, y: 8, w: 8, h: 8 }\n"
    "  - { op: copy, x: 0, y: 0, w: 4, h: 4, to_x: 12, to_y: 12, flip: y }\n"
    "  - op: art\n"
    "    x: 6\n"
    "    y: 6\n"
    "    palette: { a: \"#ff00ff\" }\n"
    "    rows: [\"a.a\", \".a.\"]\n";

TEST(recipe_output_is_pinned) {
    const char *error = "";
    const uint8_t *pixels = tv_run_recipe(t, TV_GOLDEN_RECIPE, 16, 16, 1, &error);
    REQUIRE(pixels != nullptr);
    CHECK_EQ_U64(mctex_hash(pixels, 16u * 16u * 4u), TV_GOLDEN_HASH);
    const uint8_t *again = tv_run_recipe(t, TV_GOLDEN_RECIPE, 16, 16, 1, &error);
    REQUIRE(again != nullptr);
    CHECK_MEM_EQ(pixels, again, 16u * 16u * 4u);
}

TEST(recipe_seed_changes_it_and_frames_vary) {
    const char *error = "";
    const char *ops = "ops:\n  - { op: fill, color: \"#808080\" }\n  - { op: noise, amount: 40 }\n";
    const uint8_t *one = tv_run_recipe(t, tv_format(t, "seed: 1\n%s", ops), 8, 8, 1, &error);
    const uint8_t *two = tv_run_recipe(t, tv_format(t, "seed: 2\n%s", ops), 8, 8, 1, &error);
    const uint8_t *frames = tv_run_recipe(t, tv_format(t, "seed: 1\n%s", ops), 8, 8, 3, &error);
    REQUIRE(one != nullptr && two != nullptr && frames != nullptr);
    CHECK(memcmp(one, two, 8u * 8u * 4u) != 0);
    /* Frame 0 is the single-frame recipe, and each later frame differs from it. */
    CHECK_MEM_EQ(one, frames, 8u * 8u * 4u);
    CHECK(memcmp(frames, frames + 8u * 8u * 4u, 8u * 8u * 4u) != 0);
    CHECK(memcmp(frames + 8u * 8u * 4u, frames + 2u * 8u * 8u * 4u, 8u * 8u * 4u) != 0);
    /* An operation's own seed changes it too. */
    const char *with_seed =
        "seed: 1\nops:\n  - { op: fill, color: \"#808080\" }\n  - { op: noise, amount: 40, seed: 9 }\n";
    const uint8_t *seeded = tv_run_recipe(t, with_seed, 8, 8, 1, &error);
    REQUIRE(seeded != nullptr);
    CHECK(memcmp(one, seeded, 8u * 8u * 4u) != 0);
}

/* One bad recipe, and what it must say. */
typedef struct tv_bad_recipe_t {
    const char *body;     /* 0 */
    const char *expected; /* 8 */
} tv_bad_recipe_t;
static_assert(sizeof(tv_bad_recipe_t) == 16);

TEST(recipe_refuses_bad_input_with_a_position) {
    const tv_bad_recipe_t cases[] = {
        {"ops:\n  - { op: fill, color: \"#000000\" }\n", "a recipe needs a seed"},
        {"seed: 1\n", "a recipe needs a seed"},
        {"seed: 1\nops: []\n", "a recipe needs a seed"},
        {"seed: 1\nops: 3\n", "a recipe needs a seed"},
        {"seed: -1\nops:\n  - { op: fill, color: \"#000000\" }\n", "1:7: a seed is not negative"},
        {"seed: x\nops:\n  - { op: fill, color: \"#000000\" }\n", "1:"},
        {"seed: 1\nfrom: blocks/stone\nops:\n  - { op: fill, color: \"#000000\" }\n", "2:7: a recipe has kind"},
        {"seed: 1\nops:\n  - 3\n", "3:5: each operation must be a mapping"},
        {"seed: 1\nops:\n  - { color: \"#000000\" }\n", "3:3: needs op"},
        {"seed: 1\nops:\n  - { op: read_pack, file: a.png }\n", "3:11: no such operation"},
        {"seed: 1\nops:\n  - { op: fill, colour: \"#000000\" }\n", "3:25: fill has no such key"},
        {"seed: 1\nops:\n  - { op: fill }\n", "3:3: needs color"},
        {"seed: 1\nops:\n  - { op: fill, color: red }\n", "3:24: a colour is"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#12345\" }\n", "a colour is"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#gg0000\" }\n", "a colour is"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#000000\", x: 16 }\n", "must be from 0 to 15"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#000000\", y: -1 }\n", "must be from 0 to 15"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#000000\", w: 0 }\n", "must be from 1 to 16"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#000000\", x: 8, w: 9 }\n", "does not fit the 16x16 canvas"},
        {"seed: 1\nops:\n  - { op: fill, color: \"#000000\", y: 8, h: 9 }\n", "does not fit the 16x16 canvas"},
        {"seed: 1\nops:\n  - { op: noise }\n", "needs amount"},
        {"seed: 1\nops:\n  - { op: noise, amount: 256 }\n", "must be from 0 to 255"},
        {"seed: 1\nops:\n  - { op: noise, amount: 1, cell: 0 }\n", "must be from 1 to 256"},
        {"seed: 1\nops:\n  - { op: speckle, color: \"#000000\", permille: 1001 }\n", "must be from 0 to 1000"},
        {"seed: 1\nops:\n  - { op: speckle, color: \"#000000\", permille: 1, visible: 1 }\n", "1"},
        {"seed: 1\nops:\n  - { op: blobs, color: \"#000000\", count: 1, radius_min: 3, radius_max: 2 }\n",
         "must be from 3 to 256"},
        {"seed: 1\nops:\n  - { op: gradient, from: \"#000000\", to: \"#000000\" }\n", "needs axis"},
        {"seed: 1\nops:\n  - { op: gradient, from: \"#000000\", to: \"#000000\", axis: z }\n", "needs axis"},
        {"seed: 1\nops:\n  - { op: copy, w: 2, h: 2, to_x: 15, to_y: 0 }\n", "the copy does not fit"},
        {"seed: 1\nops:\n  - { op: copy, w: 2, h: 2, to_x: 0, to_y: 0, flip: z }\n", "flip is x or y"},
        {"seed: 1\nops:\n  - { op: art, rows: [a] }\n", "art needs a palette"},
        {"seed: 1\nops:\n  - { op: art, palette: { ab: \"#000000\" }, rows: [a] }\n", "one character"},
        {"seed: 1\nops:\n  - { op: art, palette: { \".\": \"#000000\" }, rows: [a] }\n", "one character"},
        {"seed: 1\nops:\n  - { op: art, palette: { a: \"#000000\" }, rows: [b] }\n",
         "character 1 is not in the palette"},
        {"seed: 1\nops:\n  - { op: art, palette: { a: \"#000000\" }, rows: [aaaaaaaaaaaaaaaaa] }\n", "does not fit"},
        {"seed: 1\nops:\n  - { op: art, y: 15, palette: { a: \"#000000\" }, rows: [a, a] }\n", "does not fit"},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *error = "";
        const uint8_t *pixels = tv_run_recipe(t, cases[i].body, 16, 16, 1, &error);
        CHECK(pixels == nullptr);
        CHECK_CONTAINS(error, cases[i].expected);
    }
}

/* ---- Files for the tool ----------------------------------------------------------------------------------------- */

static void tv_mkdirs(test_context_t *t, const char *path) {
    char *const argv[] = {(char *)"mkdir", (char *)"-p", (char *)path, nullptr};
    test_process_t process;
    test_process_run(t, argv, TV_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static void tv_remove(test_context_t *t, const char *path) {
    char *const argv[] = {(char *)"rm", (char *)"-rf", (char *)path, nullptr};
    test_process_t process;
    test_process_run(t, argv, TV_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static void tv_write(test_context_t *t, const char *path, const void *bytes, uint64_t length) {
    const int32_t file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(file >= 0);
    CHECK_EQ_I64(write(file, bytes, length), (int64_t)length);
    close(file);
}

static void tv_write_text(test_context_t *t, const char *path, const char *text) {
    tv_write(t, path, text, strlen(text));
}

static const uint8_t *tv_read(test_context_t *t, const char *path, uint64_t *out_length) {
    *out_length = 0;
    const int32_t file = open(path, O_RDONLY);
    if (file < 0) {
        test_check(t, false, __FILE__, __LINE__, tv_format(t, "cannot open %s", path));
        return nullptr;
    }
    struct stat info;
    fstat(file, &info);
    uint8_t *bytes = test_alloc(t, (uint64_t)info.st_size + 1u);
    const int64_t got = read(file, bytes, (uint64_t)info.st_size);
    close(file);
    CHECK_EQ_I64(got, (int64_t)info.st_size);
    *out_length = (uint64_t)info.st_size;
    return bytes;
}

static const char TV_MINI_RECIPE[] = "name: moss\n"
                                     "group: blocks\n"
                                     "size: { width: 2, height: 1, frames: 1 }\n"
                                     "source:\n"
                                     "  kind: procedural\n"
                                     "  about: Two pixels of moss.\n"
                                     "  seed: 1\n"
                                     "  ops:\n"
                                     "    - { op: fill, color: \"#102030\" }\n";

static const char TV_MINI_TABLE[] = "materials:\n"
                                    "  - { texture: blocks/lit, emissive: 9 }\n"
                                    "needs:\n"
                                    "  blocks: [moss, lit]\n";

/* A small tree: one recipe, and one texture that says it came from a pack. Returns its root. */
static const char *tv_mini_tree(test_context_t *t, const char *name) {
    const char *root = tv_format(t, "%s/%s", t->borrowed_args[3], name);
    tv_remove(t, root);
    for (uint32_t i = 0; i < 4u; i++) {
        const char *const groups[] = {"blocks", "items", "mobs", "ui"};
        tv_mkdirs(t, tv_format(t, "%s/textures/%s", root, groups[i]));
    }
    tv_mkdirs(t, tv_format(t, "%s/textures/blocks/moss", root));
    tv_mkdirs(t, tv_format(t, "%s/textures/blocks/lit", root));
    tv_write_text(t, tv_format(t, "%s/textures/blocks/moss/index.yaml", root), TV_MINI_RECIPE);
    tv_write_text(t, tv_format(t, "%s/textures/blocks/lit/index.yaml", root),
                  "name: lit\ngroup: blocks\nsize: { width: 1, height: 1, frames: 1 }\nsource:\n  kind: pack\n");
    tv_write_text(t, tv_format(t, "%s/textures/blocks/lit/README.md", root), "lit\n");
    const uint8_t pixel[4] = {200, 100, 50, 255};
    uint8_t file[sizeof(mctex_header_t) + 4u];
    CHECK_EQ_U64(mctex_encode(file, sizeof(file), 1, 1, 1, pixel, 4u), MCTEX_OK);
    tv_write(t, tv_format(t, "%s/textures/blocks/lit/index.mctex", root), file, sizeof(file));
    tv_write_text(t, tv_format(t, "%s/vector.yaml", root), TV_MINI_TABLE);
    return root;
}

static test_process_t tv_tool(test_context_t *t, const char *command, const char *textures, const char *table,
                              const char *out) {
    char *argv[9] = {t->borrowed_args[0], (char *)command, (char *)"--textures", (char *)textures};
    uint32_t count = 4;
    if (table != nullptr) {
        argv[count++] = (char *)"--materials";
        argv[count++] = (char *)table;
    }
    if (out != nullptr) {
        argv[count++] = (char *)"--out";
        argv[count++] = (char *)out;
    }
    argv[count] = nullptr;
    test_process_t process;
    test_process_run(t, argv, TV_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK(process.started != 0);
    CHECK(process.timed_out == 0);
    return process;
}

static test_process_t tv_recipes(test_context_t *t, const char *root) {
    return tv_tool(t, "recipes", tv_format(t, "%s/textures", root), nullptr, nullptr);
}

static test_process_t tv_check_tree(test_context_t *t, const char *root) {
    return tv_tool(t, "check", tv_format(t, "%s/textures", root), tv_format(t, "%s/vector.yaml", root), nullptr);
}

/* ---- The tool ---------------------------------------------------------------------------------------------------- */

TEST(tool_usage_errors) {
    char *const cases[][5] = {
        {t->borrowed_args[0], nullptr, nullptr, nullptr, nullptr},
        {t->borrowed_args[0], (char *)"paint", nullptr, nullptr, nullptr},
        {t->borrowed_args[0], (char *)"recipes", nullptr, nullptr, nullptr},
        {t->borrowed_args[0], (char *)"recipes", (char *)"--textures", nullptr, nullptr},
        {t->borrowed_args[0], (char *)"recipes", (char *)"--pack", (char *)"x", nullptr},
        {t->borrowed_args[0], (char *)"check", (char *)"--textures", (char *)"x", nullptr},
        {t->borrowed_args[0], (char *)"vectors", (char *)"--textures", (char *)"x", nullptr},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_process_t process;
        test_process_run(t, cases[i], TV_TEST_TOOL_TIMEOUT_MS, &process);
        CHECK_EQ_I64(process.exit_code, 2);
        CHECK_CONTAINS(process.output, "usage:");
    }
}

TEST(tool_recipes_writes_exactly_what_the_recipe_makes) {
    const char *root = tv_mini_tree(t, "writes");
    test_process_t process = tv_recipes(t, root);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "1 recipes painted, 2 files changed");

    uint64_t length = 0;
    const uint8_t *file = tv_read(t, tv_format(t, "%s/textures/blocks/moss/index.mctex", root), &length);
    REQUIRE(file != nullptr);
    mctex_view_t view;
    REQUIRE(mctex_parse(file, length, &view) == MCTEX_OK);
    CHECK_EQ_U64(view.width, 2u);
    CHECK_EQ_U64(view.height, 1u);
    const uint8_t expected[8] = {0x10, 0x20, 0x30, 255, 0x10, 0x20, 0x30, 255};
    CHECK_MEM_EQ(view.borrowed_pixels, expected, 8u);

    const uint8_t *readme = tv_read(t, tv_format(t, "%s/textures/blocks/moss/README.md", root), &length);
    REQUIRE(readme != nullptr);
    CHECK_CONTAINS((const char *)readme, "# textures/blocks/moss");
    CHECK_CONTAINS((const char *)readme, "Two pixels of moss.");
    CHECK_CONTAINS((const char *)readme, "no pack's pixels");

    /* It leaves the texture from a pack alone, and a second run changes nothing. */
    uint64_t lit_length = 0;
    const uint8_t *lit = tv_read(t, tv_format(t, "%s/textures/blocks/lit/index.mctex", root), &lit_length);
    REQUIRE(lit != nullptr);
    CHECK_EQ_U64(lit_length, 36u);
    process = tv_recipes(t, root);
    CHECK_CONTAINS(process.output, "1 recipes painted, 0 files changed");
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "1 from the pack, 1 from recipes, 2 needed and all there");
}

TEST(tool_check_finds_each_kind_of_damage) {
    const char *root = tv_mini_tree(t, "damage");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    const char *moss = tv_format(t, "%s/textures/blocks/moss", root);

    /* Pixels edited by hand, even with a good hash. */
    const uint8_t other[8] = {1, 1, 1, 255, 1, 1, 1, 255};
    uint8_t file[sizeof(mctex_header_t) + 8u];
    CHECK_EQ_U64(mctex_encode(file, sizeof(file), 2, 1, 1, other, 8u), MCTEX_OK);
    tv_write(t, tv_format(t, "%s/index.mctex", moss), file, sizeof(file));
    test_process_t process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "is not what its recipe makes");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    CHECK_EQ_I64(tv_check_tree(t, root).exit_code, 0);

    /* A recipe edited, with no repaint. */
    const char *changed =
        "name: moss\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n"
        "  about: Two pixels of moss.\n  seed: 1\n  ops:\n    - { op: fill, color: \"#102031\" }\n";
    tv_write_text(t, tv_format(t, "%s/index.yaml", moss), changed);
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "is not what its recipe makes");
    /* Put back, it matches what is still there. */
    tv_write_text(t, tv_format(t, "%s/index.yaml", moss), TV_MINI_RECIPE);
    CHECK_EQ_I64(tv_check_tree(t, root).exit_code, 0);

    /* The output files gone. */
    unlink(tv_format(t, "%s/README.md", moss));
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "README.md: missing");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    unlink(tv_format(t, "%s/index.mctex", moss));
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "index.mctex: missing");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);

    /* A pack texture damaged: check has no recipe for it, and must still catch it. */
    const char *lit = tv_format(t, "%s/textures/blocks/lit/index.mctex", root);
    uint64_t lit_length = 0;
    const uint8_t *lit_bytes = tv_read(t, lit, &lit_length);
    REQUIRE(lit_bytes != nullptr);
    uint8_t *damaged = test_alloc(t, lit_length);
    memcpy(damaged, lit_bytes, lit_length);
    damaged[lit_length - 1u] ^= 1u;
    tv_write(t, lit, damaged, lit_length);
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "do not match");
}

TEST(tool_check_holds_the_tree_to_needs_and_materials) {
    const char *root = tv_mini_tree(t, "needs");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    tv_write_text(t, tv_format(t, "%s/vector.yaml", root),
                  "materials: []\nneeds:\n  mobs: [zombie]\n  blocks: [moss]\n");
    test_process_t process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "the game needs mobs/zombie");
    tv_write_text(t, tv_format(t, "%s/vector.yaml", root),
                  "materials:\n  - { texture: blocks/gone, emissive: 3 }\nneeds:\n  blocks: [moss]\n");
    process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, ":2: no such texture: blocks/gone");
}

/* The lit pixel is {200, 100, 50}: luma 117, and warm. A row its thresholds shut out must not pass as a light. */
TEST(a_material_that_gives_no_light_is_refused) {
    const char *root = tv_mini_tree(t, "no_light");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    const char *out = tv_format(t, "%s/out", root);
    const char *textures = tv_format(t, "%s/textures", root);
    const char *table = tv_format(t, "%s/vector.yaml", root);

    tv_write_text(t, table, "materials:\n  - { texture: blocks/lit, emissive: 9, min_luma: 118 }\nneeds: {}\n");
    test_process_t process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "blocks/lit asks for light level 9");
    process = tv_tool(t, "vectors", textures, table, out);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "blocks/lit asks for light level 9");

    /* One level lower it passes, in both commands. */
    tv_write_text(t, table, "materials:\n  - { texture: blocks/lit, emissive: 9, min_luma: 117 }\nneeds: {}\n");
    CHECK_EQ_I64(tv_check_tree(t, root).exit_code, 0);
    CHECK_EQ_I64(tv_tool(t, "vectors", textures, table, out).exit_code, 0);
}

TEST(tool_refuses_a_bad_directory) {
    const char *root = tv_mini_tree(t, "bad_directory");
    const char *yaml = tv_format(t, "%s/textures/blocks/moss/index.yaml", root);

    tv_write_text(t, yaml, "name: moss\ngroup: blocks\nsource:\n  kind: magic\n");
    test_process_t process = tv_recipes(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "neither pack nor procedural");

    tv_write_text(t, yaml, "name: moss\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\n");
    process = tv_recipes(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "says nothing of source.kind");

    const char *const wrong[][2] = {
        {"name: other\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n  about: "
         "A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "its name and group must be those of its directory"},
        {"name: moss\ngroup: mobs\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n  about: A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "its name and group must be those of its directory"},
        {"name: moss\ngroup: blocks\nsource:\n  kind: procedural\n  about: A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "size"},
        {"name: moss\ngroup: blocks\nsize: { width: 0, height: 1, frames: 1 }\nsource:\n  kind: procedural\n  about: "
         "A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "size.width is out of range"},
        {"name: moss\ngroup: blocks\nsize: { width: 4096, height: 4096, frames: 256 }\nsource:\n  kind: procedural\n  "
         "about: A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "over the limits"},
        {"name: moss\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "needs source.about"},
        {"name: moss\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n  about: "
         "\"A\\tB\"\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#000000\" }\n",
         "must be printable ASCII"},
        {"name: moss\ngroup: blocks\nsize: { width: 2, height: 1, frames: 1 }\nsource:\n  kind: procedural\n  about: "
         "A.\n"
         "  seed: 1\n  ops:\n    - { op: fill, color: \"#00000\" }\n",
         "index.yaml:9:"},
        {"name: moss\n  bad indentation: 1\n", "index.yaml:2:"},
    };
    for (uint32_t i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++) {
        tv_write_text(t, yaml, wrong[i][0]);
        process = tv_recipes(t, root);
        CHECK_EQ_I64(process.exit_code, 1);
        CHECK_CONTAINS(process.output, wrong[i][1]);
    }
    /* A missing group directory is refused, not taken as empty. */
    const char *gone = tv_mini_tree(t, "no_group");
    tv_remove(t, tv_format(t, "%s/textures/ui", gone));
    process = tv_recipes(t, gone);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "cannot be listed");
}

TEST(tool_vectors_match_what_analyze_makes_and_keep_times) {
    const char *root = tv_mini_tree(t, "vectors");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    const char *out = tv_format(t, "%s/deep/out", root);
    test_process_t process =
        tv_tool(t, "vectors", tv_format(t, "%s/textures", root), tv_format(t, "%s/vector.yaml", root), out);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output,
                   "vector data for 2 textures, 2 opaque, 0 cutout, 0 translucent, 1 emissive; 2 files");

    uint64_t length = 0;
    const char *path = tv_format(t, "%s/blocks/lit/index.mcvec", out);
    const uint8_t *file = tv_read(t, path, &length);
    REQUIRE(file != nullptr);
    mcvec_view_t view;
    REQUIRE(mcvec_parse(file, length, &view) == MCVEC_OK);
    /* (200, 100, 50): the source's own hash, one colour, its own mean, and the level from the table. */
    const uint8_t pixel[4] = {200, 100, 50, 255};
    CHECK_EQ_U64(view.header.source_hash, mctex_hash(pixel, 4u));
    CHECK_EQ_U64(view.header.palette_count, 1u);
    CHECK_MEM_EQ(view.borrowed_palette, pixel, 4u);
    CHECK_MEM_EQ(view.header.mean_rgba, pixel, 4u);
    CHECK_EQ_U64(view.header.emissive_class, MCVEC_EMISSIVE_FULL);
    CHECK_EQ_U64(view.header.emissive_level, 9u);
    CHECK_EQ_U64(view.borrowed_emissive[0], 9u);
    CHECK_EQ_U64(view.borrowed_height[0], TV_HEIGHT_FLAT);

    struct stat before;
    struct stat after;
    REQUIRE(stat(path, &before) == 0);
    process = tv_tool(t, "vectors", tv_format(t, "%s/textures", root), tv_format(t, "%s/vector.yaml", root), out);
    CHECK_CONTAINS(process.output, "0 files changed");
    REQUIRE(stat(path, &after) == 0);
    CHECK_EQ_I64(before.st_mtim.tv_nsec, after.st_mtim.tv_nsec);
    /* A damaged source stops it. */
    tv_write_text(t, tv_format(t, "%s/textures/blocks/lit/index.mctex", root), "nonsense");
    process = tv_tool(t, "vectors", tv_format(t, "%s/textures", root), tv_format(t, "%s/vector.yaml", root), out);
    CHECK_EQ_I64(process.exit_code, 1);
}

/* One bad vector.yaml, and what it must say. */
TEST(materials_refuse_bad_input_with_a_line) {
    const tv_bad_recipe_t cases[] = {
        {"needs: {}\n", "needs both"},
        {"materials: []\n", "needs both"},
        {"materials: []\nneeds: {}\nmore: 1\n", ":3:7: unknown key"},
        {"materials: 1\nneeds: {}\n", "needs both"},
        {"materials: []\nneeds: 3\n", ":2:8: needs must be a mapping"},
        {"materials: []\nneeds: { blocks: stone }\n", "each group must be a sequence"},
        {"materials: []\nneeds: { things: [stone] }\n", "not a group/name"},
        {"materials: []\nneeds: { blocks: [\"Stone\"] }\n", "not a group/name"},
        {"materials: []\nneeds: { blocks: [\"../x\"] }\n", "not a group/name"},
        {"materials: []\nneeds: { blocks: [stone, stone] }\n", "named twice"},
        {"materials:\n  - 3\nneeds: {}\n", ":2:5: each material must be a mapping"},
        {"materials:\n  - { emissive: 3 }\nneeds: {}\n", ":2:3: a material needs a texture"},
        {"materials:\n  - { texture: stone, emissive: 3 }\nneeds: {}\n", "texture must be group/name"},
        {"materials:\n  - { texture: nope/stone, emissive: 3 }\nneeds: {}\n", "texture must be group/name"},
        {"materials:\n  - { texture: blocks/stone, glow: 3 }\nneeds: {}\n", "unknown key"},
        {"materials:\n  - { texture: blocks/stone, emissive: 16 }\nneeds: {}\n", "out of range"},
        {"materials:\n  - { texture: blocks/stone, emissive: -1 }\nneeds: {}\n", "out of range"},
        {"materials:\n  - { texture: blocks/stone, emissive: 3, min_luma: 256 }\nneeds: {}\n", "out of range"},
        {"materials:\n  - { texture: blocks/stone, emissive: 3, warm: 1 }\nneeds: {}\n", ":2:"},
        {"materials:\n  - { texture: blocks/stone, alpha: glass }\nneeds: {}\n", "alpha must be"},
        {"materials:\n  - { texture: blocks/stone }\nneeds: {}\n", "must say emissive or alpha"},
        {"materials:\n  - { texture: blocks/stone, emissive: 0 }\nneeds: {}\n", "must say emissive or alpha"},
        {"materials:\n  - { texture: blocks/stone, min_luma: 3, alpha: cutout }\nneeds: {}\n", "mean nothing"},
        {"materials:\n  - { texture: blocks/a, emissive: 1 }\n  - { texture: blocks/a, emissive: 2 }\nneeds: {}\n",
         ":3:3: this texture already has"},
    };
    const char *root = tv_mini_tree(t, "materials");
    CHECK_EQ_I64(tv_recipes(t, root).exit_code, 0);
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        tv_write_text(t, tv_format(t, "%s/vector.yaml", root), cases[i].body);
        const test_process_t process = tv_check_tree(t, root);
        CHECK_EQ_I64(process.exit_code, 1);
        CHECK_CONTAINS(process.output, cases[i].expected);
    }
    unlink(tv_format(t, "%s/vector.yaml", root));
    const test_process_t process = tv_check_tree(t, root);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK_CONTAINS(process.output, "vector.yaml: missing");
    /* The tool speaks in its own name, though it shares a helper with texture_pack. */
    CHECK_CONTAINS(process.output, "texture_vector: ");
}

TEST(materials_load_and_find) {
    tp_arena_t arena = tv_test_arena(t);
    const char *root = tv_format(t, "%s/materials_load", t->borrowed_args[3]);
    tv_mkdirs(t, root);
    const char *path = tv_format(t, "%s/vector.yaml", root);
    tv_write_text(t, path,
                  "materials:\n"
                  "  - { texture: blocks/torch, emissive: 14, min_luma: 130, warm: true }\n"
                  "  - { texture: blocks/water, alpha: translucent }\n"
                  "  - { texture: blocks/leaf, alpha: cutout }\n"
                  "  - { texture: blocks/hot, emissive: 3, alpha: opaque }\n"
                  "needs:\n"
                  "  blocks: [torch, water]\n"
                  "  ui: [font]\n");
    tv_materials_t materials;
    REQUIRE(tv_materials_load(&arena, path, &materials));
    CHECK_EQ_U64(materials.row_count, 4u);
    CHECK_EQ_U64(materials.need_count, 3u);
    CHECK_STR_EQ(materials.borrowed_needs[2], "ui/font");
    tv_material_t found = tv_materials_find(&materials, "blocks/torch");
    CHECK_EQ_U64(found.emissive_level, 14u);
    CHECK_EQ_U64(found.min_luma, 130u);
    CHECK_EQ_U64(found.warm, 1u);
    CHECK_EQ_U64(found.alpha_class, TV_ALPHA_FROM_PIXELS);
    CHECK_EQ_U64(tv_materials_find(&materials, "blocks/water").alpha_class, MCVEC_ALPHA_TRANSLUCENT);
    CHECK_EQ_U64(tv_materials_find(&materials, "blocks/leaf").alpha_class, MCVEC_ALPHA_CUTOUT);
    CHECK_EQ_U64(tv_materials_find(&materials, "blocks/hot").alpha_class, MCVEC_ALPHA_OPAQUE);
    found = tv_materials_find(&materials, "blocks/other");
    CHECK_EQ_U64(found.emissive_level, 0u);
    CHECK_EQ_U64(found.alpha_class, TV_ALPHA_FROM_PIXELS);
}

/* ---- The committed tree ------------------------------------------------------------------------------------------ */

TEST(committed_tree_is_whole) {
    const char *table = t->borrowed_args[2];
    const test_process_t process = tv_tool(t, "check", t->borrowed_args[1], table, nullptr);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, " from the pack, ");
}

/* The vector data of one committed texture. Vectors are made once per run, into this directory. */
static mcvec_header_t tv_committed_vector(test_context_t *t, const char *out, const char *to) {
    mcvec_header_t header;
    memset(&header, 0, sizeof(header));
    uint64_t length = 0;
    const uint8_t *file = tv_read(t, tv_format(t, "%s/%s/index.mcvec", out, to), &length);
    if (file == nullptr) {
        return header;
    }
    mcvec_view_t view;
    const mcvec_status_t status = mcvec_parse(file, length, &view);
    CHECK_EQ_U64(status, MCVEC_OK);
    return status == MCVEC_OK ? view.header : header;
}

/* One committed texture's classes, as they must come out. */
typedef struct tv_class_case_t {
    const char *to;   /* 0 */
    uint8_t alpha;    /* 8 */
    uint8_t emissive; /* 9 */
    uint8_t level;    /* 10 */
    uint8_t _pad0[5]; /* 11 */
} tv_class_case_t;
static_assert(sizeof(tv_class_case_t) == 16);

TEST(committed_tree_has_vector_data_with_the_right_classes) {
    const char *out = tv_format(t, "%s/committed", t->borrowed_args[3]);
    tv_remove(t, out);
    const test_process_t process = tv_tool(t, "vectors", t->borrowed_args[1], t->borrowed_args[2], out);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "vector data for ");
    const tv_class_case_t cases[] = {
        {"blocks/stone", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/dirt", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/deepslate", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/water_still", MCVEC_ALPHA_TRANSLUCENT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/ice", MCVEC_ALPHA_TRANSLUCENT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/glass", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/oak_leaves", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/rose", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/lava_still", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_FULL, 15, {0}},
        {"blocks/fire", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_FULL, 15, {0}},
        {"blocks/torch_floor", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_PARTIAL, 14, {0}},
        {"blocks/furnace_front_lit", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_PARTIAL, 13, {0}},
        {"blocks/furnace_front", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"blocks/jack_o_lantern", MCVEC_ALPHA_OPAQUE, MCVEC_EMISSIVE_PARTIAL, 15, {0}},
        {"mobs/zombie", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"mobs/skeleton", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"mobs/creeper", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"items/gold_pickaxe", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"ui/font", MCVEC_ALPHA_CUTOUT, MCVEC_EMISSIVE_NONE, 0, {0}},
        {"ui/sun", MCVEC_ALPHA_TRANSLUCENT, MCVEC_EMISSIVE_FULL, 15, {0}},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const mcvec_header_t header = tv_committed_vector(t, out, cases[i].to);
        if (header.alpha_class != cases[i].alpha || header.emissive_class != cases[i].emissive ||
            header.emissive_level != cases[i].level) {
            test_check(t, false, __FILE__, __LINE__, cases[i].to);
        }
    }
    /* Means that must look right on a far terrain: grass is green, dirt is brown, and stone is grey. */
    const mcvec_header_t grass = tv_committed_vector(t, out, "blocks/grass_top");
    CHECK(grass.mean_rgba[1] > grass.mean_rgba[0] && grass.mean_rgba[1] > grass.mean_rgba[2]);
    const mcvec_header_t dirt = tv_committed_vector(t, out, "blocks/dirt");
    CHECK(dirt.mean_rgba[0] > dirt.mean_rgba[1] && dirt.mean_rgba[1] > dirt.mean_rgba[2]);
    const mcvec_header_t stone = tv_committed_vector(t, out, "blocks/stone");
    CHECK(stone.mean_rgba[0] < stone.mean_rgba[1] + 8u && stone.mean_rgba[1] < stone.mean_rgba[2] + 8u);
    CHECK_EQ_U64(stone.mean_rgba[3], 255u);
}

/* A table of skins this game's mobs must have, whatever a pack does. */
TEST(mob_skins_are_our_own_recipes_with_the_right_layout) {
    const char *const names[] = {"mobs/zombie", "mobs/skeleton", "mobs/creeper"};
    for (uint32_t i = 0; i < 3u; i++) {
        uint64_t length = 0;
        const uint8_t *yaml = tv_read(t, tv_format(t, "%s/%s/index.yaml", t->borrowed_args[1], names[i]), &length);
        REQUIRE(yaml != nullptr);
        CHECK_CONTAINS((const char *)yaml, "kind: procedural");
        const uint8_t *file = tv_read(t, tv_format(t, "%s/%s/index.mctex", t->borrowed_args[1], names[i]), &length);
        REQUIRE(file != nullptr);
        mctex_view_t view;
        REQUIRE(mctex_parse(file, length, &view) == MCTEX_OK);
        /* The same 64 by 32 unwrapping as the player's skin. The head's front face, at (8, 8) by 8 by 8, is fully painted. */
        CHECK_EQ_U64(view.width, 64u);
        CHECK_EQ_U64(view.height, 32u);
        for (uint32_t y = 8u; y < 16u; y++) {
            for (uint32_t x = 8u; x < 16u; x++) {
                CHECK_EQ_U64(view.borrowed_pixels[(y * 64u + x) * 4u + 3u], 255u);
            }
        }
        /* The corner of the sheet that no box uses stays empty. */
        CHECK_EQ_U64(view.borrowed_pixels[(1u * 64u + 1u) * 4u + 3u], 0u);
    }
}

/* Whatever else changes, these must always have a source. A list here, so that dropping one from vector.yaml is not enough. */
TEST(needs_cover_what_the_game_cannot_do_without) {
    tp_arena_t arena = tv_test_arena(t);
    tv_materials_t materials;
    REQUIRE(tv_materials_load(&arena, t->borrowed_args[2], &materials));
    const char *const required[] = {
        "mobs/zombie",
        "mobs/skeleton",
        "mobs/creeper",
        "mobs/player",
        "mobs/arrow",
        "blocks/dirt",
        "blocks/stone",
        "blocks/grass_top",
        "blocks/deepslate",
        "blocks/oak_log",
        "blocks/water_still",
        "blocks/lava_still",
        "blocks/torch_floor",
        "blocks/crack",
        "blocks/furnace_front_lit",
        "items/gold_pickaxe",
        "items/iron_helmet",
        "items/raw_iron",
        "items/bow",
        "items/arrow",
        "items/rotten_flesh",
        "items/stick",
        "items/bread",
        "ui/font",
        "ui/button",
        "ui/crosshair",
        "ui/hotbar",
        "ui/icon_health",
        "ui/icon_hunger",
        "ui/icon_hunger_empty",
        "ui/icon_armor",
        "ui/sun",
        "ui/moon",
    };
    for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        bool found = false;
        for (uint32_t n = 0; n < materials.need_count; n++) {
            found = found || strcmp(materials.borrowed_needs[n], required[i]) == 0;
        }
        if (!found) {
            test_check(t, false, __FILE__, __LINE__, required[i]);
        }
    }
    CHECK(materials.need_count > 400u);
}

/* Copies every recipe, and nothing else, to a clean tree, and repaints it. If any recipe read a pack's pixels, this would fail. */
TEST(recipes_need_no_pack_at_all) {
    const char *clean = tv_format(t, "%s/clean", t->borrowed_args[3]);
    tv_remove(t, clean);
    const char *const groups[] = {"blocks", "items", "mobs", "ui"};
    uint32_t copied = 0;
    for (uint32_t g = 0; g < 4u; g++) {
        const char *group = tv_format(t, "%s/%s", t->borrowed_args[1], groups[g]);
        tv_mkdirs(t, tv_format(t, "%s/%s", clean, groups[g]));
        DIR *listing = opendir(group);
        REQUIRE(listing != nullptr);
        struct dirent *item = readdir(listing);
        while (item != nullptr) {
            const char *yaml = tv_format(t, "%s/%s/index.yaml", group, item->d_name);
            if (item->d_name[0] != '.' && access(yaml, R_OK) == 0) {
                uint64_t length = 0;
                const uint8_t *text = tv_read(t, yaml, &length);
                if (text != nullptr && strstr((const char *)text, "kind: procedural") != nullptr) {
                    const char *directory = tv_format(t, "%s/%s/%s", clean, groups[g], item->d_name);
                    REQUIRE(mkdir(directory, 0755) == 0);
                    tv_write(t, tv_format(t, "%s/index.yaml", directory), text, length);
                    copied++;
                }
            }
            item = readdir(listing);
        }
        closedir(listing);
    }
    CHECK(copied >= 150u);
    const test_process_t process = tv_tool(t, "recipes", clean, nullptr, nullptr);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, "files changed");
    /* Every painted texture is byte for byte what is committed. */
    for (uint32_t g = 0; g < 4u; g++) {
        const char *group = tv_format(t, "%s/%s", clean, groups[g]);
        DIR *listing = opendir(group);
        REQUIRE(listing != nullptr);
        struct dirent *item = readdir(listing);
        while (item != nullptr) {
            if (item->d_name[0] != '.') {
                uint64_t left_length = 0;
                uint64_t right_length = 0;
                const uint8_t *left = tv_read(t, tv_format(t, "%s/%s/index.mctex", group, item->d_name), &left_length);
                const uint8_t *right =
                    tv_read(t, tv_format(t, "%s/%s/%s/index.mctex", t->borrowed_args[1], groups[g], item->d_name),
                            &right_length);
                REQUIRE(left != nullptr && right != nullptr);
                CHECK_EQ_U64(left_length, right_length);
                CHECK(memcmp(left, right, left_length) == 0);
            }
            item = readdir(listing);
        }
        closedir(listing);
    }
    tv_remove(t, clean);
}

TEST_MAIN()
