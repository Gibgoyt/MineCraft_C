/*
 * ctest compile_texture_blob. See README.md. Arguments come from test.cmake: blob_tool vector_tool work_dir. Expected values
 * are worked out by hand in the comments beside them, so that this test does not merely repeat the code.
 */
#include "harness/index.h"
#include "texture_blob/index.h"
#include "texture_blob/mip/index.h"
#include "texture_pack/mctex/index.h"
#include "texture_pack/support/index.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TB_TEST_ARENA_BYTES (2ull << 20)
#define TB_TEST_TOOL_TIMEOUT_MS 60000u
#define TB_TEST_FORMAT_BYTES 4096u

static const char *tb_format(test_context_t *t, const char *format, ...) {
    char *text = test_alloc(t, TB_TEST_FORMAT_BYTES);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, TB_TEST_FORMAT_BYTES, format, arguments);
    va_end(arguments);
    return text;
}

static void tb_set(uint8_t *pixels, uint32_t position, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    pixels[position * 4u] = red;
    pixels[position * 4u + 1u] = green;
    pixels[position * 4u + 2u] = blue;
    pixels[position * 4u + 3u] = alpha;
}

static tb_mip_source_t tb_source(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t frames,
                                 uint8_t alpha_class) {
    return (tb_mip_source_t){.borrowed_pixels = pixels,
                             .width = width,
                             .height = height,
                             .frame_count = frames,
                             .alpha_class = alpha_class,
                             .hole_rgb = {9u, 8u, 7u}};
}

/* ---- Sizes ------------------------------------------------------------------------------------------------------- */

TEST(level_sizes_halve_rounding_down_and_stop_at_one) {
    CHECK_EQ_U64(texture_blob_level_count(1u, 1u), 1u);
    CHECK_EQ_U64(texture_blob_level_count(16u, 16u), 5u);   /* 16 8 4 2 1 */
    CHECK_EQ_U64(texture_blob_level_count(64u, 32u), 7u);   /* 64 32 16 8 4 2 1 */
    CHECK_EQ_U64(texture_blob_level_count(165u, 170u), 8u); /* 170 85 42 21 10 5 2 1 */
    CHECK_EQ_U64(texture_blob_level_count(4096u, 4096u), TEXTURE_BLOB_MAX_LEVELS);
    CHECK_EQ_U64(texture_blob_level_dimension(165u, 1u), 82u);
    CHECK_EQ_U64(texture_blob_level_dimension(165u, 7u), 1u);
    CHECK_EQ_U64(texture_blob_level_dimension(2u, 4u), 1u); /* the short side stays at 1 while the long one goes on */
}

TEST(chain_bytes_add_up_and_offsets_follow) {
    uint32_t offset[TEXTURE_BLOB_MAX_LEVELS];
    /* 4 by 2, three frames: 32 + 8 + 4 bytes per frame, the last level being 1 by 1. */
    CHECK_EQ_U64(tb_mip_chain_bytes(4u, 2u, 3u, offset), (32u + 8u + 4u) * 3u);
    CHECK_EQ_U64(offset[0], 0u);
    CHECK_EQ_U64(offset[1], 96u);
    CHECK_EQ_U64(offset[2], 120u);
    CHECK_EQ_U64(offset[3], 0u);
    CHECK_EQ_U64(tb_mip_chain_bytes(4u, 2u, 3u, nullptr), 132u);
}

/* ---- Colour ------------------------------------------------------------------------------------------------------ */

TEST(colour_averages_in_linear_light_not_in_srgb_bytes) {
    uint8_t pixels[4u * 4u];
    /* Half black and half white. Averaging the bytes gives 128. In linear light it is 0.5, whose sRGB byte is 188: sRGB 187 is
     * 0.4969 and 188 is 0.5029, and 0.5 is nearer the second. */
    tb_set(pixels, 0u, 0u, 0u, 0u, 255u);
    tb_set(pixels, 1u, 255u, 255u, 255u, 255u);
    tb_set(pixels, 2u, 255u, 255u, 255u, 255u);
    tb_set(pixels, 3u, 0u, 0u, 0u, 255u);
    uint8_t chain[(4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 1u, TEXTURE_BLOB_ALPHA_OPAQUE);
    tb_mip_build(&source, chain);
    CHECK_MEM_EQ(chain, pixels, sizeof(pixels));
    const uint8_t want[4] = {188u, 188u, 188u, 255u};
    CHECK_MEM_EQ(chain + 16u, want, 4u);
}

TEST(colour_under_a_transparent_texel_does_not_bleed) {
    uint8_t pixels[2u * 4u];
    tb_set(pixels, 0u, 255u, 0u, 0u, 255u);
    tb_set(pixels, 1u, 0u, 0u, 255u, 0u);
    uint8_t chain[(2u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 1u, 1u, TEXTURE_BLOB_ALPHA_TRANSLUCENT);
    tb_mip_build(&source, chain);
    /* Pure red, with none of the blue beside it. Alpha is (255 + 0) / 2 = 127.5, which rounds to 128. */
    const uint8_t want[4] = {255u, 0u, 0u, 128u};
    CHECK_MEM_EQ(chain + 8u, want, 4u);
}

TEST(a_faint_pixel_counts_by_its_alpha) {
    uint8_t pixels[2u * 4u];
    tb_set(pixels, 0u, 0u, 0u, 0u, 255u);
    tb_set(pixels, 1u, 255u, 255u, 255u, 85u);
    uint8_t chain[(2u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 1u, 1u, TEXTURE_BLOB_ALPHA_TRANSLUCENT);
    tb_mip_build(&source, chain);
    /* White has weight 85 of 340, a quarter, so linear 0.25, and sRGB 0.25 is byte 137: 137 is 0.2502. Alpha (255 + 85) / 2. */
    const uint8_t want[4] = {137u, 137u, 137u, 170u};
    CHECK_MEM_EQ(chain + 8u, want, 4u);
}

TEST(a_texel_with_nothing_visible_takes_the_hole_colour) {
    uint8_t pixels[4u * 4u];
    for (uint32_t i = 0; i < 4u; i++) {
        tb_set(pixels, i, 200u, 100u, 50u, 0u);
    }
    uint8_t chain[(4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 1u, TEXTURE_BLOB_ALPHA_TRANSLUCENT);
    tb_mip_build(&source, chain);
    const uint8_t want[4] = {9u, 8u, 7u, 0u};
    CHECK_MEM_EQ(chain + 16u, want, 4u);
    /* Level 0 keeps what it had, even under a hole. */
    CHECK_MEM_EQ(chain, pixels, sizeof(pixels));
}

TEST(odd_sizes_share_a_source_texel_between_two_targets) {
    uint8_t pixels[5u * 4u];
    tb_set(pixels, 0u, 255u, 255u, 255u, 255u);
    tb_set(pixels, 1u, 255u, 255u, 255u, 255u);
    for (uint32_t i = 2; i < 5u; i++) {
        tb_set(pixels, i, 0u, 0u, 0u, 255u);
    }
    /* 5 wide goes to 2 wide, then 1. The first texel of level 1 covers 2.5 source texels: two white and half of a black, so
     * linear 2 / 2.5 = 0.8, whose sRGB byte is 231: 231 is 0.7991 and 232 is 0.8070. The second covers only black. */
    uint8_t chain[(5u + 2u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 5u, 1u, 1u, TEXTURE_BLOB_ALPHA_OPAQUE);
    tb_mip_build(&source, chain);
    const uint8_t want[8] = {231u, 231u, 231u, 255u, 0u, 0u, 0u, 255u};
    CHECK_MEM_EQ(chain + 20u, want, 8u);
    /* Level 2 comes from level 0, and not from level 1: linear 2 / 5 = 0.4, byte 170 (170 is 0.4020, 169 is 0.3968). Going
     * through level 1 would give the same here only by accident of even weights. */
    const uint8_t want_last[4] = {170u, 170u, 170u, 255u};
    CHECK_MEM_EQ(chain + 28u, want_last, 4u);
}

TEST(three_wide_goes_straight_to_one) {
    uint8_t pixels[3u * 4u];
    tb_set(pixels, 0u, 0u, 0u, 0u, 255u);
    tb_set(pixels, 1u, 0u, 0u, 0u, 255u);
    tb_set(pixels, 2u, 255u, 255u, 255u, 255u);
    uint8_t chain[(3u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 3u, 1u, 1u, TEXTURE_BLOB_ALPHA_OPAQUE);
    tb_mip_build(&source, chain);
    /* Linear 1 / 3, and sRGB byte 156 is 0.3325 where 157 is 0.3372. */
    const uint8_t want[4] = {156u, 156u, 156u, 255u};
    CHECK_MEM_EQ(chain + 12u, want, 4u);
}

TEST(each_frame_has_its_own_chain) {
    uint8_t pixels[2u * 2u * 2u * 4u];
    for (uint32_t i = 0; i < 4u; i++) {
        tb_set(pixels, i, 255u, 0u, 0u, 255u);
        tb_set(pixels, 4u + i, 0u, 255u, 0u, 255u);
    }
    uint8_t chain[(4u + 1u) * 2u * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 2u, TEXTURE_BLOB_ALPHA_OPAQUE);
    tb_mip_build(&source, chain);
    CHECK_MEM_EQ(chain, pixels, sizeof(pixels));
    const uint8_t red[4] = {255u, 0u, 0u, 255u};
    const uint8_t green[4] = {0u, 255u, 0u, 255u};
    CHECK_MEM_EQ(chain + 32u, red, 4u);
    CHECK_MEM_EQ(chain + 36u, green, 4u);
}

/* ---- Alpha ------------------------------------------------------------------------------------------------------- */

TEST(opaque_is_255_at_every_level) {
    uint8_t pixels[4u * 4u];
    for (uint32_t i = 0; i < 4u; i++) {
        tb_set(pixels, i, 10u, 20u, 30u, 252u); /* what the pack's lit furnace has in places */
    }
    uint8_t chain[(4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 1u, TEXTURE_BLOB_ALPHA_OPAQUE);
    tb_mip_build(&source, chain);
    for (uint32_t i = 0; i < 5u; i++) {
        CHECK_EQ_U64(chain[i * 4u + 3u], 255u);
    }
    CHECK_EQ_U64(chain[0], 10u); /* colours stay */
}

TEST(cutout_level_zero_is_solid_at_128) {
    uint8_t pixels[4u * 4u];
    tb_set(pixels, 0u, 1u, 2u, 3u, 5u); /* the pack's leaves, where a leaf is absent */
    tb_set(pixels, 1u, 1u, 2u, 3u, 127u);
    tb_set(pixels, 2u, 1u, 2u, 3u, 128u);
    tb_set(pixels, 3u, 1u, 2u, 3u, 255u);
    uint8_t chain[(4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    CHECK_EQ_U64(chain[3], 0u);
    CHECK_EQ_U64(chain[7], 0u);
    CHECK_EQ_U64(chain[11], 255u);
    CHECK_EQ_U64(chain[15], 255u);
    CHECK_EQ_U64(chain[0], 1u); /* colours stay */
}

/*
 * A 4 by 4 whose quarters hold 4, 3, 3 and 2 solid texels: 12 of 16, so level 1 should be 3 of 4 solid. Its averaged alphas
 * are 255, 191, 191 and 128 (127.5 rounded). Rounding at 128 would make all 4 solid, and a thick tree fills in with distance.
 * Keeping the share drops the 128.
 */
TEST(cutout_levels_keep_the_share_of_solid_texels) {
    uint8_t pixels[16u * 4u];
    for (uint32_t i = 0; i < 16u; i++) {
        tb_set(pixels, i, 50u, 60u, 70u, 255u);
    }
    pixels[2u * 4u + 3u] = 0u;  /* the top right quarter loses one */
    pixels[8u * 4u + 3u] = 0u;  /* the bottom left quarter loses one */
    pixels[10u * 4u + 3u] = 0u; /* the bottom right quarter loses two */
    pixels[15u * 4u + 3u] = 0u;
    uint8_t chain[(16u + 4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 4u, 4u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    const uint8_t *level1 = chain + 64u;
    CHECK_EQ_U64(level1[3], 255u);
    CHECK_EQ_U64(level1[7], 255u);
    CHECK_EQ_U64(level1[11], 255u);
    CHECK_EQ_U64(level1[15], 0u);
    /* 12 of 16 is more than half, so the last level is solid. */
    CHECK_EQ_U64(chain[80u + 3u], 255u);
}

/* Half solid in every quarter, so all of level 1 averages to one alpha, 128. A threshold could only give all 4 or none. Ties go
 * in Bayer order, (0,0) then (1,1), so the 2 wanted are on one diagonal. As glass is, whose mips went 44%, 25%, 0, 100%, 0
 * under my first threshold. */
TEST(cutout_ties_spread_out_and_still_keep_the_share) {
    uint8_t pixels[16u * 4u];
    for (uint32_t i = 0; i < 16u; i++) {
        tb_set(pixels, i, 50u, 60u, 70u, (i % 2u) == 0u ? 255u : 0u);
    }
    uint8_t chain[(16u + 4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 4u, 4u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    const uint8_t *level1 = chain + 64u;
    CHECK_EQ_U64(level1[3], 255u);
    CHECK_EQ_U64(level1[7], 0u);
    CHECK_EQ_U64(level1[11], 0u);
    CHECK_EQ_U64(level1[15], 255u);
    /* 8 of 16 at 1 texel rounds up. */
    CHECK_EQ_U64(chain[80u + 3u], 255u);
}

/* Three quarters hold 3 solid texels each and the last holds none: 9 of 16, and of level 1's 4 texels, 2.25 rounds to 2. Three
 * texels tie at 191, so one must miss out. Bayer order takes (0,0), then (1,1), and passes over (1,0) and the empty (0,1). */
TEST(cutout_takes_no_more_of_a_tie_than_it_wants) {
    uint8_t pixels[16u * 4u];
    for (uint32_t i = 0; i < 16u; i++) {
        tb_set(pixels, i, 50u, 60u, 70u, 255u);
    }
    pixels[0u * 4u + 3u] = 0u;  /* top left quarter */
    pixels[2u * 4u + 3u] = 0u;  /* top right quarter */
    pixels[10u * 4u + 3u] = 0u; /* bottom right quarter */
    for (uint32_t i = 8u; i < 10u; i++) {
        pixels[i * 4u + 3u] = 0u; /* bottom left quarter */
        pixels[(i + 4u) * 4u + 3u] = 0u;
    }
    uint8_t chain[(16u + 4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 4u, 4u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    const uint8_t *level1 = chain + 64u;
    CHECK_EQ_U64(level1[3], 255u);
    CHECK_EQ_U64(level1[7], 0u);
    CHECK_EQ_U64(level1[11], 0u);
    CHECK_EQ_U64(level1[15], 255u);
}

/* 3 by 1 has a grid of 4 by 4, most of it off the image, and level 1 is 1 by 1: its position must still come up. */
TEST(cutout_ties_are_found_when_the_image_is_not_a_square_power_of_two) {
    uint8_t pixels[7u * 3u * 4u];
    for (uint32_t i = 0; i < 21u; i++) {
        tb_set(pixels, i, 50u, 60u, 70u, 255u);
    }
    /* All solid, so every level must stay all solid, in a level of 3 by 1 and a level of 1 by 1. */
    uint8_t chain[(21u + 3u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 7u, 3u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    for (uint32_t i = 0; i < 25u; i++) {
        CHECK_EQ_U64(chain[i * 4u + 3u], 255u);
    }
}

TEST(cutout_too_sparse_for_a_texel_vanishes_rather_than_filling_it) {
    /* One solid texel of 16, as a torch might be: at 1 by 1 the share rounds to none. */
    uint8_t pixels[16u * 4u];
    memset(pixels, 0, sizeof(pixels));
    tb_set(pixels, 5u, 200u, 150u, 50u, 255u);
    uint8_t chain[(16u + 4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 4u, 4u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    CHECK_EQ_U64(chain[80u + 3u], 0u);
    /* Its colour is still worked out from what is visible, and not from the hole colour. */
    CHECK_EQ_U64(chain[80u], 200u);
}

TEST(an_empty_cutout_stays_empty_at_every_level) {
    uint8_t pixels[4u * 4u];
    memset(pixels, 0, sizeof(pixels));
    uint8_t chain[(4u + 1u) * 4u];
    const tb_mip_source_t source = tb_source(pixels, 2u, 2u, 1u, TEXTURE_BLOB_ALPHA_CUTOUT);
    tb_mip_build(&source, chain);
    CHECK_EQ_U64(chain[16u + 3u], 0u);
    CHECK_EQ_U64(chain[16u], 9u);
}

/* ---- The tool ---------------------------------------------------------------------------------------------------- */

static void tb_tree_write_file(test_context_t *t, const char *path, const void *bytes, uint64_t length) {
    tp_arena_t arena = {
        .borrowed_base = test_alloc(t, TB_TEST_ARENA_BYTES), .capacity = TB_TEST_ARENA_BYTES, .used = 0};
    CHECK(tp_write_if_changed(&arena, path, bytes, length, nullptr));
}

static void tb_tree_make_directories(test_context_t *t, const char *path) {
    tp_arena_t arena = {
        .borrowed_base = test_alloc(t, TB_TEST_ARENA_BYTES), .capacity = TB_TEST_ARENA_BYTES, .used = 0};
    CHECK(tp_make_directories(&arena, path));
}

static const uint8_t *tb_tree_read_file(test_context_t *t, const char *path, uint64_t *out_length) {
    tp_arena_t arena = {
        .borrowed_base = test_alloc(t, TB_TEST_ARENA_BYTES), .capacity = TB_TEST_ARENA_BYTES, .used = 0};
    uint8_t *bytes = nullptr;
    const tp_read_status_t status = tp_read_path(&arena, path, TB_TEST_ARENA_BYTES / 2u, &bytes, out_length);
    CHECK_EQ_U64(status, TP_READ_OK);
    return bytes;
}

static void tb_tree_remove(test_context_t *t, const char *path) {
    char *const argv[] = {(char *)"rm", (char *)"-rf", (char *)path, nullptr};
    test_process_t process;
    test_process_run(t, argv, TB_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

/* A mctex of one flat colour. */
static void tb_tree_add(test_context_t *t, const char *root, const char *to, uint32_t width, uint32_t height,
                        uint8_t red) {
    tb_tree_make_directories(t, tb_format(t, "%s/textures/%s", root, to));
    const uint64_t pixel_bytes = (uint64_t)width * height * 4u;
    uint8_t *pixels = test_alloc(t, pixel_bytes);
    for (uint64_t i = 0; i < pixel_bytes / 4u; i++) {
        tb_set(pixels, (uint32_t)i, red, 20u, 30u, 255u);
    }
    const uint64_t file_bytes = sizeof(mctex_header_t) + pixel_bytes;
    uint8_t *file = test_alloc(t, file_bytes);
    CHECK_EQ_U64(mctex_encode(file, file_bytes, width, height, 1u, pixels, pixel_bytes), MCTEX_OK);
    tb_tree_write_file(t, tb_format(t, "%s/textures/%s/index.mctex", root, to), file, file_bytes);
}

static const char *tb_tree_begin(test_context_t *t, const char *name) {
    if (t->arg_count != 3) {
        CHECK_EQ_I64(t->arg_count, 3);
        return nullptr;
    }
    const char *root = tb_format(t, "%s/%s", t->borrowed_args[2], name);
    tb_tree_remove(t, root);
    const char *const groups[] = {"blocks", "items", "mobs", "ui"};
    for (uint32_t i = 0; i < 4u; i++) {
        tb_tree_make_directories(t, tb_format(t, "%s/textures/%s", root, groups[i]));
    }
    tb_tree_make_directories(t, tb_format(t, "%s/vectors", root));
    static const char table[] = "materials: []\nneeds: {}\n";
    tb_tree_write_file(t, tb_format(t, "%s/vector.yaml", root), table, sizeof(table) - 1u);
    return root;
}

static test_process_t tb_tree_vectors(test_context_t *t, const char *root) {
    char *const argv[] = {t->borrowed_args[1],
                          (char *)"vectors",
                          (char *)"--textures",
                          (char *)tb_format(t, "%s/textures", root),
                          (char *)"--materials",
                          (char *)tb_format(t, "%s/vector.yaml", root),
                          (char *)"--out",
                          (char *)tb_format(t, "%s/vectors", root),
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, TB_TEST_TOOL_TIMEOUT_MS, &process);
    return process;
}

static test_process_t tb_tree_blob(test_context_t *t, const char *root, const char *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--textures",
                          (char *)tb_format(t, "%s/textures", root),
                          (char *)"--vectors",
                          (char *)tb_format(t, "%s/vectors", root),
                          (char *)"--out",
                          (char *)tb_format(t, "%s/%s", root, out),
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, TB_TEST_TOOL_TIMEOUT_MS, &process);
    return process;
}

TEST(the_tool_lays_a_blob_out_as_the_format_says) {
    const char *root = tb_tree_begin(t, "layout");
    REQUIRE(root != nullptr);
    /* Listed out of order on purpose: ids follow the names, not the order they were made in. */
    tb_tree_add(t, root, "ui/z", 2u, 2u, 40u);
    tb_tree_add(t, root, "blocks/b", 3u, 1u, 50u);
    tb_tree_add(t, root, "blocks/a", 4u, 2u, 60u);
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 0);
    const test_process_t process = tb_tree_blob(t, root, "blob");
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_OK);
    CHECK_CONTAINS(process.output, "3 textures");

    uint64_t length = 0;
    const uint8_t *blob = tb_tree_read_file(t, tb_format(t, "%s/blob", root), &length);
    REQUIRE(length > sizeof(texture_blob_header_t));
    texture_blob_header_t header;
    memcpy(&header, blob, sizeof(header));
    CHECK_EQ_U64(header.magic, TEXTURE_BLOB_MAGIC);
    CHECK_EQ_U64(header.version, TEXTURE_BLOB_VERSION);
    CHECK_EQ_U64(header.texture_count, 3u);
    CHECK_EQ_U64(header.total_bytes, length);
    CHECK_EQ_U64(header.payload_hash, mctex_hash(blob + sizeof(header), length - sizeof(header)));
    CHECK_EQ_U64(header.entries_offset, 80u);
    CHECK_EQ_U64(header.names_offset, 80u + 3u * 144u);
    /* "blocks/a", "blocks/b" and "ui/z" with their NULs: 9 + 9 + 5. */
    CHECK_EQ_U64(header.name_bytes, 23u);
    CHECK_EQ_U64(header.pixels_offset % TEXTURE_BLOB_ALIGNMENT, 0u);
    CHECK_EQ_U64(header.vectors_offset % TEXTURE_BLOB_TEXTURE_ALIGNMENT, 0u);

    texture_blob_entry_t entries[3];
    memcpy(entries, blob + header.entries_offset, sizeof(entries));
    const char *names = (const char *)blob + header.names_offset;
    CHECK_STR_EQ(names + entries[0].name_offset, "blocks/a");
    CHECK_STR_EQ(names + entries[1].name_offset, "blocks/b");
    CHECK_STR_EQ(names + entries[2].name_offset, "ui/z");
    CHECK_EQ_U64(entries[0].name_hash, mctex_hash((const uint8_t *)"blocks/a", 8u));
    /* 4 by 2 has levels of 8, 2 and 1 pixels, 44 bytes, and the next texture starts on the next 32-byte boundary. */
    CHECK_EQ_U64(entries[0].level_count, 3u);
    CHECK_EQ_U64(entries[0].pixels_offset, 0u);
    CHECK_EQ_U64(entries[0].pixels_bytes, 44u);
    CHECK_EQ_U64(entries[1].pixels_offset, 64u);
    /* 3 by 1 has levels of 3 and 1 pixels, 16 bytes; 2 by 2 has 4 and 1, 20 bytes. */
    CHECK_EQ_U64(entries[1].pixels_bytes, 16u);
    CHECK_EQ_U64(entries[2].pixels_offset, 96u);
    CHECK_EQ_U64(header.pixels_bytes, 116u);
    /* One colour: palette of 4 bytes, then 5 per pixel. */
    CHECK_EQ_U64(entries[0].vector_bytes, 4u + 8u * 5u);
    CHECK_EQ_U64(entries[0].palette_count, 1u);
    CHECK_EQ_U64(entries[0].alpha_class, TEXTURE_BLOB_ALPHA_OPAQUE);
    /* Everything is one colour, so every level is too: a mip of a flat texture is the same flat texture. */
    const uint8_t *pixels = blob + header.pixels_offset;
    for (uint32_t i = 0; i < 11u; i++) {
        CHECK_EQ_U64(pixels[i * 4u], 60u);
        CHECK_EQ_U64(pixels[i * 4u + 3u], 255u);
    }
    /* The gaps are zero: 44 to 64 in the pixels. */
    for (uint32_t i = 44u; i < 64u; i++) {
        CHECK_EQ_U64(pixels[i], 0u);
    }
    tb_tree_remove(t, root);
}

TEST(the_same_input_gives_the_same_bytes_and_a_change_shows) {
    const char *root = tb_tree_begin(t, "same");
    REQUIRE(root != nullptr);
    tb_tree_add(t, root, "blocks/a", 4u, 4u, 60u);
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 0);
    CHECK_EQ_I64(tb_tree_blob(t, root, "first").exit_code, 0);
    CHECK_EQ_I64(tb_tree_blob(t, root, "second").exit_code, 0);
    uint64_t first_length = 0;
    uint64_t second_length = 0;
    const uint8_t *first = tb_tree_read_file(t, tb_format(t, "%s/first", root), &first_length);
    const uint8_t *second = tb_tree_read_file(t, tb_format(t, "%s/second", root), &second_length);
    REQUIRE(first_length == second_length);
    CHECK_MEM_EQ(first, second, first_length);
    /* One colour repainted, and vectors run again: the same size, and other bytes. */
    tb_tree_add(t, root, "blocks/a", 4u, 4u, 61u);
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 0);
    CHECK_EQ_I64(tb_tree_blob(t, root, "third").exit_code, 0);
    uint64_t third_length = 0;
    const uint8_t *third = tb_tree_read_file(t, tb_format(t, "%s/third", root), &third_length);
    REQUIRE(third_length == first_length);
    CHECK(memcmp(first, third, first_length) != 0);
    tb_tree_remove(t, root);
}

TEST(vector_data_of_other_pixels_is_refused) {
    const char *root = tb_tree_begin(t, "stale");
    REQUIRE(root != nullptr);
    tb_tree_add(t, root, "blocks/a", 4u, 4u, 60u);
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 0);
    tb_tree_add(t, root, "blocks/a", 4u, 4u, 61u); /* repainted, and vectors was not run */
    const test_process_t process = tb_tree_blob(t, root, "blob");
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_REFUSED);
    CHECK_CONTAINS(process.output, "was not made from");
    tb_tree_remove(t, root);
}

TEST(missing_files_are_refused_and_named) {
    const char *root = tb_tree_begin(t, "missing");
    REQUIRE(root != nullptr);
    test_process_t process = tb_tree_blob(t, root, "blob");
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_REFUSED); /* no texture at all */
    CHECK_CONTAINS(process.output, "holds 0 textures");

    tb_tree_add(t, root, "blocks/a", 4u, 4u, 60u);
    process = tb_tree_blob(t, root, "blob");
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_REFUSED); /* its vector data was never made */
    CHECK_CONTAINS(process.output, "blocks/a/index.mcvec: missing");

    tb_tree_make_directories(t, tb_format(t, "%s/textures/items/empty", root));
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 1); /* texture_vector refuses it first */
    process = tb_tree_blob(t, root, "blob");
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_REFUSED);
    CHECK_CONTAINS(process.output, "items/empty/index.mctex: missing");
    tb_tree_remove(t, root);
}

TEST(a_damaged_mctex_is_refused) {
    const char *root = tb_tree_begin(t, "damaged");
    REQUIRE(root != nullptr);
    tb_tree_add(t, root, "blocks/a", 4u, 4u, 60u);
    CHECK_EQ_I64(tb_tree_vectors(t, root).exit_code, 0);
    static const uint8_t junk[] = {1u, 2u, 3u};
    tb_tree_write_file(t, tb_format(t, "%s/textures/blocks/a/index.mctex", root), junk, sizeof(junk));
    CHECK_EQ_I64(tb_tree_blob(t, root, "blob").exit_code, TEXTURE_BLOB_EXIT_REFUSED);
    tb_tree_write_file(t, tb_format(t, "%s/vectors/blocks/a/index.mcvec", root), junk, sizeof(junk));
    CHECK_EQ_I64(tb_tree_blob(t, root, "blob").exit_code, TEXTURE_BLOB_EXIT_REFUSED);
    tb_tree_remove(t, root);
}

TEST(a_bad_command_line_is_usage) {
    REQUIRE(t->arg_count == 3);
    char *const argv[] = {t->borrowed_args[0], (char *)"--textures", (char *)"x", nullptr};
    test_process_t process;
    test_process_run(t, argv, TB_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_USAGE);
    CHECK_CONTAINS(process.output, "usage:");
    char *const unknown[] = {t->borrowed_args[0], (char *)"--nope", (char *)"x", nullptr};
    test_process_run(t, unknown, TB_TEST_TOOL_TIMEOUT_MS, &process);
    CHECK_EQ_I64(process.exit_code, TEXTURE_BLOB_EXIT_USAGE);
}

TEST_MAIN()
