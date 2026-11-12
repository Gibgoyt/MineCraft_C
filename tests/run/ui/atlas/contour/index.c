/* ctest run_ui_atlas_contour. See README.md. */
#include "harness/index.h"

#include "arena/index.h"
#include "file/index.h"
#include "tables/blocks/index.h"
#include "texture/blob/index.h"
#include "ui/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Made by the Atlas of plan 004 phase 09 (version 1), before this phase changed the pixel: see README.md. */
static const char V1_FIXTURE[] = "tests/run/ui/atlas/fixtures/tile_v1_L0_-3_5.tile";

static int64_t floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

static void *aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- The pixel, the column top, mips and the file ------------------------------------------------------------------- */

TEST(a_pixel_packs_and_unpacks_height_and_cover) {
    static const uint32_t COVERS[3] = {0u, 7u, 31u};
    uint64_t wrong = 0u;
    for (int32_t z = -256; z <= 1279; z++) {
        for (uint32_t c = 0u; c < 3u; c++) {
            const uint16_t colour = (uint16_t)((uint32_t)(z + 256) * 40503u + c * 977u);
            const uint32_t pixel = ui_atlas_pixel_pack_cover(colour, z, COVERS[c]);
            wrong += ui_atlas_pixel_generated(pixel) ? 0u : 1u;
            wrong += ui_atlas_pixel_z(pixel) == z ? 0u : 1u;
            wrong += ui_atlas_pixel_cover(pixel) == COVERS[c] ? 0u : 1u;
            wrong += ui_atlas_pixel_ground_z(pixel) == z - (int32_t)COVERS[c] ? 0u : 1u;
            wrong += ui_atlas_pixel_rgb565(pixel) == colour ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    CHECK(!ui_atlas_pixel_generated(UI_ATLAS_PIXEL_NONE));
    CHECK_EQ_U64(UI_ATLAS_PIXEL_NONE, 0x07FF0000u);
    CHECK_EQ_U64(ui_atlas_pixel_pack(0u, -256), 0u); /* the lowest height is code 0 */
    CHECK_EQ_U64(ui_atlas_pixel_pack(0u, 0), 256u << 16);
    /* Held, never wrapped: past the ends, and cover past 31. */
    CHECK_EQ_I64(ui_atlas_pixel_z(ui_atlas_pixel_pack(0u, -300)), -256);
    CHECK_EQ_I64(ui_atlas_pixel_z(ui_atlas_pixel_pack(0u, 2000)), 1279);
    CHECK_EQ_U64(ui_atlas_pixel_cover(ui_atlas_pixel_pack_cover(0u, 70, 40u)), 31u);
    CHECK(ui_atlas_pixel_generated(ui_atlas_pixel_pack_cover(0xFFFFu, 1279, 31u)));
}

/* A column with nothing in it but what a test puts there, by height from sea level. */
static void column_put(uint16_t *states, uint32_t x, uint32_t y, int32_t z, uint16_t block) {
    const uint32_t from_floor = (uint32_t)(z - COLUMN_Z_STORAGE_MIN);
    states[(uint64_t)(from_floor / SECTION_SIZE) * SECTION_VOXELS +
           section_voxel_index(x, y, from_floor % SECTION_SIZE)] = block;
}

typedef struct column_want_t {
    int32_t top;    /* 0 */
    uint32_t cover; /* 4 */
    uint32_t block; /* 8 */
} column_want_t;
static_assert(sizeof(column_want_t) == 12);

typedef struct spacing_want_t {
    int32_t zoom;  /* 0 */
    int32_t minor; /* 4 */
    int32_t index; /* 8 */
} spacing_want_t;
static_assert(sizeof(spacing_want_t) == 12);

TEST(a_tree_s_cover_is_measured_down_to_the_ground) {
    uint16_t *const colours = test_alloc(t, BLOCK_COUNT * sizeof(uint16_t));
    for (uint32_t b = 0u; b < BLOCK_COUNT; b++) {
        colours[b] = (uint16_t)(b * 977u + 13u);
    }
    uint16_t *const states = test_alloc(t, (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t));
    /* (0, 0): an oak on grass at 72, its log 73 to 77, its leaves to 79. */
    column_put(states, 0u, 0u, 71, BLOCK_ID_DIRT);
    column_put(states, 0u, 0u, 72, BLOCK_ID_GRASS_BLOCK);
    for (int32_t z = 73; z <= 77; z++) {
        column_put(states, 0u, 0u, z, BLOCK_ID_OAK_LOG);
    }
    column_put(states, 0u, 0u, 78, BLOCK_ID_OAK_LEAVES);
    column_put(states, 0u, 0u, 79, BLOCK_ID_OAK_LEAVES);
    /* (1, 0): stone to 64. */
    for (int32_t z = 40; z <= 64; z++) {
        column_put(states, 1u, 0u, z, BLOCK_ID_STONE);
    }
    /* (2, 0): a jungle tree 40 tall on grass at 72. */
    column_put(states, 2u, 0u, 72, BLOCK_ID_GRASS_BLOCK);
    for (int32_t z = 73; z <= 112; z++) {
        column_put(states, 2u, 0u, z, BLOCK_ID_JUNGLE_LOG);
    }
    /* (3, 0): a canopy over air and a flower, on grass at 72. */
    column_put(states, 3u, 0u, 72, BLOCK_ID_GRASS_BLOCK);
    column_put(states, 3u, 0u, 73, BLOCK_ID_DANDELION);
    column_put(states, 3u, 0u, 78, BLOCK_ID_BIRCH_LEAVES);
    column_put(states, 3u, 0u, 79, BLOCK_ID_BIRCH_LEAVES);
    /* (4, 0): a snow layer on grass at 64. */
    column_put(states, 4u, 0u, 64, BLOCK_ID_GRASS_BLOCK);
    column_put(states, 4u, 0u, 65, BLOCK_ID_SNOW_LAYER);
    /* (5, 0): leaves at 100 over 50 blocks of air down to grass at 49. */
    column_put(states, 5u, 0u, 49, BLOCK_ID_GRASS_BLOCK);
    column_put(states, 5u, 0u, 100, BLOCK_ID_SPRUCE_LEAVES);
    /* (6, 0): leaves at -200 with nothing under them. */
    column_put(states, 6u, 0u, -200, BLOCK_ID_OAK_LEAVES);
    /* (7, 0): leaves over the sea at 0. */
    for (int32_t z = -5; z <= 0; z++) {
        column_put(states, 7u, 0u, z, BLOCK_ID_WATER);
    }
    column_put(states, 7u, 0u, 3, BLOCK_ID_MANGROVE_LEAVES);
    uint32_t out[UI_ATLAS_COLUMN_PIXELS];
    ui_atlas_column_top(colours, BLOCK_COUNT, states, out);
    static const column_want_t WANT[8] = {
        {79, 7u, BLOCK_ID_OAK_LEAVES},    {64, 0u, BLOCK_ID_STONE},          {112, 31u, BLOCK_ID_JUNGLE_LOG},
        {79, 7u, BLOCK_ID_BIRCH_LEAVES},  {65, 1u, BLOCK_ID_SNOW_LAYER},     {100, 31u, BLOCK_ID_SPRUCE_LEAVES},
        {-200, 31u, BLOCK_ID_OAK_LEAVES}, {3, 3u, BLOCK_ID_MANGROVE_LEAVES},
    };
    for (uint32_t x = 0u; x < 8u; x++) {
        const uint32_t pixel = out[15u * 16u + x]; /* y 0 is the south row, row 15 */
        if (!CHECK(ui_atlas_pixel_z(pixel) == WANT[x].top && ui_atlas_pixel_cover(pixel) == WANT[x].cover &&
                   ui_atlas_pixel_rgb565(pixel) == colours[WANT[x].block])) {
            printf("  x %u: top %" PRId32 " cover %u, want %" PRId32 " and %u\n", x, (int32_t)ui_atlas_pixel_z(pixel),
                   ui_atlas_pixel_cover(pixel), WANT[x].top, WANT[x].cover);
        }
    }
    CHECK_EQ_I64(ui_atlas_pixel_ground_z(out[15u * 16u + 0u]), 72);
    /* Everything else is empty: the floor, in black, with no cover. */
    CHECK_EQ_U64(out[0], ui_atlas_pixel_pack(0u, COLUMN_Z_STORAGE_MIN));
}

TEST(mips_average_cover) {
    const uint32_t four[4] = {ui_atlas_pixel_pack_cover(0x1111u, 70, 0u), ui_atlas_pixel_pack_cover(0x2222u, 74, 4u),
                              ui_atlas_pixel_pack_cover(0x3333u, 66, 8u), UI_ATLAS_PIXEL_NONE};
    const uint32_t mean = ui_atlas_pixel_mean(four);
    CHECK_EQ_U64(ui_atlas_pixel_cover(mean), 4u); /* (12 + 1) / 3, floored */
    CHECK_EQ_I64(ui_atlas_pixel_z(mean), 70);
    /* And through a halving, every parent pixel's cover the rounded mean of its children's. */
    uint32_t *const child = test_alloc(t, UI_ATLAS_TILE_BYTES);
    uint32_t *const parent = test_alloc(t, UI_ATLAS_TILE_BYTES);
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        child[k] = k % 7u == 3u ? UI_ATLAS_PIXEL_NONE : ui_atlas_pixel_pack_cover(0u, 50, (k * 13u) % 32u);
    }
    ui_atlas_tile_halve(child, 0u, parent);
    uint64_t wrong = 0u;
    for (uint32_t j = 0u; j < 256u; j++) {
        for (uint32_t i = 0u; i < 256u; i++) {
            int64_t sum = 0, n = 0;
            for (uint32_t d = 0u; d < 4u; d++) {
                const uint32_t k = (2u * j + d / 2u) * 512u + 2u * i + d % 2u;
                if (k % 7u != 3u) {
                    sum += (k * 13u) % 32u;
                    n++;
                }
            }
            const uint32_t got = parent[(256u + j) * 512u + i];
            wrong += ui_atlas_pixel_cover(got) == (uint64_t)floor_div(sum + n / 2, n) ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
}

/* The fixture's pixel k, as version 1 packed it: runs of 64, every 17th run not generated, heights -256 to 1279. */
static uint32_t v1_pixel(uint32_t k) {
    const uint32_t r = k / 64u;
    if (r % 17u == 5u) {
        return 0x80000000u;
    }
    const int32_t z = (int32_t)((r * 37u) % 1536u) - 256;
    const uint16_t colour = (uint16_t)((r * 2654435761u) >> 16);
    return (uint32_t)colour | (uint32_t)(uint16_t)(int16_t)z << 16;
}

TEST(a_version_one_tile_still_opens) {
    uint8_t *const file = test_alloc(t, UI_ATLAS_FILE_BYTES_MAX);
    uint64_t length = 0u;
    REQUIRE(file_read_whole(V1_FIXTURE, file, UI_ATLAS_FILE_BYTES_MAX, &length) == FILE_OK);
    ui_atlas_file_header_t header;
    memcpy(&header, file, sizeof(header));
    CHECK_EQ_U64(header.version, 1u);
    uint32_t *const tile = test_alloc(t, UI_ATLAS_TILE_BYTES);
    REQUIRE(ui_atlas_tile_decode(file, length, 42u, 0u, -3, 5, tile) == UI_ATLAS_DECODE_OK);
    uint64_t wrong = 0u, none = 0u;
    for (uint32_t k = 0u; k < UI_ATLAS_TILE_PIXELS; k++) {
        const uint32_t old = v1_pixel(k);
        if ((int16_t)(uint16_t)(old >> 16) == INT16_MIN) {
            wrong += ui_atlas_pixel_generated(tile[k]) ? 1u : 0u;
            none++;
            continue;
        }
        wrong += ui_atlas_pixel_generated(tile[k]) ? 0u : 1u;
        wrong += ui_atlas_pixel_z(tile[k]) == (int16_t)(uint16_t)(old >> 16) ? 0u : 1u;
        wrong += ui_atlas_pixel_rgb565(tile[k]) == (uint16_t)(old & 0xFFFFu) ? 0u : 1u;
        wrong += ui_atlas_pixel_cover(tile[k]) == 0u ? 0u : 1u;
    }
    CHECK_EQ_U64(wrong, 0u);
    CHECK(none > 0u);
    /* Written again, it is version 2, and reads back the same. */
    uint8_t *const again = test_alloc(t, UI_ATLAS_FILE_BYTES_MAX);
    uint32_t *const back = test_alloc(t, UI_ATLAS_TILE_BYTES);
    const uint64_t again_length = ui_atlas_tile_encode(tile, 42u, 0u, -3, 5, again);
    memcpy(&header, again, sizeof(header));
    CHECK_EQ_U64(header.version, 2u);
    CHECK(ui_atlas_tile_decode(again, again_length, 42u, 0u, -3, 5, back) == UI_ATLAS_DECODE_OK);
    CHECK_MEM_EQ(back, tile, UI_ATLAS_TILE_BYTES);
    /* A flipped byte, in the header or the payload, is still refused; and so is a version from the future. */
    file[33] ^= 0x01u;
    CHECK(ui_atlas_tile_decode(file, length, 42u, 0u, -3, 5, tile) != UI_ATLAS_DECODE_OK);
    file[33] ^= 0x01u;
    file[sizeof(ui_atlas_file_header_t) + 9u] ^= 0x10u;
    CHECK(ui_atlas_tile_decode(file, length, 42u, 0u, -3, 5, tile) == UI_ATLAS_DECODE_PAYLOAD);
    file[sizeof(ui_atlas_file_header_t) + 9u] ^= 0x10u;
    memcpy(&header, file, sizeof(header));
    header.version = 3u;
    header.checksum = texture_blob_hash((const uint8_t *)&header, offsetof(ui_atlas_file_header_t, checksum));
    memcpy(file, &header, sizeof(header));
    CHECK(ui_atlas_tile_decode(file, length, 42u, 0u, -3, 5, tile) == UI_ATLAS_DECODE_HEADER);
}

/* ---- The lines ------------------------------------------------------------------------------------------------------ */

TEST(contour_spacing_follows_the_zoom) {
    static const spacing_want_t WANT[] = {{-4, 25, 250}, {0, 25, 250},  {1, 50, 500},   {2, 50, 500},   {4, 50, 500},
                                          {5, 100, 500}, {8, 100, 500}, {9, 250, 1000}, {16, 250, 1000}};
    for (uint32_t k = 0u; k < sizeof(WANT) / sizeof(WANT[0]); k++) {
        int32_t minor = 0, index = 0;
        ui_atlas_contour_interval(WANT[k].zoom, &minor, &index);
        if (!CHECK(minor == WANT[k].minor && index == WANT[k].index)) {
            printf("  zoom %" PRId32 ": (%" PRId32 ", %" PRId32 ")\n", WANT[k].zoom, minor, index);
        }
    }
    /* On a slope of 1, minor lines I / s pixels apart are never closer than 3 to zoom 8. */
    for (int32_t zoom = UI_ATLAS_ZOOM_MIN; zoom <= 8; zoom++) {
        int32_t minor = 0, index = 0;
        ui_atlas_contour_interval(zoom, &minor, &index);
        CHECK((float64_t)minor / ui_atlas_blocks_per_pixel(zoom) >= 3.0f64);
        CHECK(index % minor == 0);
    }
}

/* A made-up ground through ui_atlas_ground_t's read. */
typedef enum synth_kind_t : uint8_t {
    SYNTH_PLANE = 0, /* z = floor(x / 10), flat in y */
    SYNTH_CONE = 1,  /* z = 1200 - r about (0, 0): a slope of 1 */
} synth_kind_t;
typedef struct synth_t {
    uint8_t kind;    /* 0: synth_kind_t */
    uint8_t trees;   /* 1: trees 6 to 20 tall on the plane, x 2000 to 2399 */
    uint8_t on_top;  /* 2: the trees' crowns as the ground, cover 0: the rule before this phase */
    uint8_t _pad[5]; /* 3 */
} synth_t;
static_assert(sizeof(synth_t) == 8);

static void synth_read(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width, uint32_t height,
                       uint32_t *out, arena_t *scratch) {
    (void)scratch;
    const synth_t *const s = borrowed_context;
    const int64_t size = (int64_t)1 << level;
    for (uint32_t j = 0u; j < height; j++) {
        const int64_t y = -1 - ((py + (int64_t)j) * size + size / 2);
        for (uint32_t i = 0u; i < width; i++) {
            const int64_t x = (px + (int64_t)i) * size + size / 2;
            int32_t ground = 0;
            uint32_t cover = 0u;
            if (s->kind == SYNTH_PLANE) {
                ground = (int32_t)floor_div(x, 10);
                if (s->trees != 0u && x >= 2000 && x < 2400 && (x * 7 + y * 13) % 5 != 0) {
                    cover = 6u + (uint32_t)((x * 31 + (y < 0 ? -y : y) * 17) % 15);
                }
            } else {
                ground = (int32_t)lround(1200.0f64 - sqrt((float64_t)x * (float64_t)x + (float64_t)y * (float64_t)y));
            }
            out[(uint64_t)j * width + i] = s->on_top != 0u
                                               ? ui_atlas_pixel_pack(0xFFFFu, ground + (int32_t)cover)
                                               : ui_atlas_pixel_pack_cover(0xFFFFu, ground + (int32_t)cover, cover);
        }
    }
}

enum : uint8_t {
    LINE_NONE = 0u,
    LINE_MINOR = 1u,
    LINE_INDEX = 2u,
};

/* The terrain layer of a white made-up ground with no hill shading, each pixel read back as which line it is on. */
static void synth_draw(test_context_t *t, synth_t *s, int32_t zoom, float64_t cx, uint32_t width, uint32_t height,
                       uint8_t *lines, ui_atlas_geometry_t *geometry) {
    ui_atlas_draw_input_t input = {
        .view = {.center = {cx, 0.0f64}, .zoom = zoom, .width = width, .height = height, .layer = 0u},
        .ground = {.borrowed_context = s, .read = synth_read},
        .sample = 1u,
        .flat = 1u,
    };
    REQUIRE(ui_atlas_layers[0].value == UI_ATLAS_VALUE_CONTOURS);
    const uint64_t count = (uint64_t)width * height;
    const ui_atlas_target_t target = {
        .borrowed_pixels = test_alloc(t, count * 4u),
        .borrowed_grey = test_alloc(t, count),
        .borrowed_field = test_alloc(t, count * 4u),
        .borrowed_scratch = aligned(t, UI_ATLAS_OVERLAY_SCRATCH_BYTES),
    };
    const uint64_t bytes = ui_atlas_band_scratch_bytes(width, height) + (1u << 20);
    arena_t *const arena = aligned(t, sizeof(arena_t));
    arena_init(arena, "test_contour", aligned(t, bytes), bytes);
    ui_atlas_draw_picture(&input, &target, nullptr, nullptr, 1u, arena);
    arena_deinit(arena);
    for (uint64_t k = 0u; k < count; k++) {
        const uint32_t red = (target.borrowed_pixels[k] >> 16) & 0xFFu; /* 255, 128 at 0.5, 77 at 0.3 */
        lines[k] = red > 200u ? LINE_NONE : (red > 100u ? LINE_MINOR : LINE_INDEX);
    }
    *geometry = ui_atlas_geometry(&input.view);
}

enum : uint32_t {
    PLANE_WIDTH = 1920u,
    PLANE_HEIGHT = 4u,
};

TEST(lines_sit_where_the_plane_crosses_each_band) {
    /* At zoom 0 a picture column is a block: x = column0 + i. Minor lines where floor(z / 25) changes to the east (x 249
     * mod 250), index lines both sides of where floor(z / 250) changes (x 2499 and 2500), which the origin is placed on. */
    synth_t s = {.kind = SYNTH_PLANE};
    uint8_t *const lines = test_alloc(t, PLANE_WIDTH * PLANE_HEIGHT);
    ui_atlas_geometry_t g;
    synth_draw(t, &s, 0, 2600.0f64, PLANE_WIDTH, PLANE_HEIGHT, lines, &g);
    uint64_t wrong = 0u, minors = 0u, indexes = 0u;
    for (uint32_t i = 0u; i < PLANE_WIDTH; i++) {
        const int64_t x = (int64_t)floor(ui_atlas_pixel_x(&g, (float64_t)i));
        const uint8_t want = (x == 2499 || x == 2500) ? LINE_INDEX : (x % 250 == 249 ? LINE_MINOR : LINE_NONE);
        for (uint32_t j = 0u; j < PLANE_HEIGHT; j++) {
            if (lines[(uint64_t)j * PLANE_WIDTH + i] != want) {
                wrong++;
                if (wrong <= 5u) {
                    printf("  x %" PRId64 " row %u: line %u, want %u\n", x, j, lines[(uint64_t)j * PLANE_WIDTH + i],
                           want);
                }
            }
        }
        minors += want == LINE_MINOR ? 1u : 0u;
        indexes += want == LINE_INDEX ? 1u : 0u;
    }
    CHECK_EQ_U64(wrong, 0u);
    CHECK(minors >= 6u);
    CHECK_EQ_U64(indexes, 2u);
}

TEST(lines_follow_the_ground_under_trees) {
    uint8_t *const bare = test_alloc(t, PLANE_WIDTH * PLANE_HEIGHT);
    uint8_t *const wooded = test_alloc(t, PLANE_WIDTH * PLANE_HEIGHT);
    uint8_t *const crowns = test_alloc(t, PLANE_WIDTH * PLANE_HEIGHT);
    ui_atlas_geometry_t g;
    synth_t s = {.kind = SYNTH_PLANE};
    synth_draw(t, &s, 0, 2600.0f64, PLANE_WIDTH, PLANE_HEIGHT, bare, &g);
    s.trees = 1u;
    synth_draw(t, &s, 0, 2600.0f64, PLANE_WIDTH, PLANE_HEIGHT, wooded, &g);
    CHECK_MEM_EQ(wooded, bare, PLANE_WIDTH * PLANE_HEIGHT);
    /* The rule on the crowns would draw round the trees. */
    s.on_top = 1u;
    synth_draw(t, &s, 0, 2600.0f64, PLANE_WIDTH, PLANE_HEIGHT, crowns, &g);
    CHECK(memcmp(crowns, bare, PLANE_WIDTH * PLANE_HEIGHT) != 0);
}

enum : uint32_t {
    CONE_WIDTH = 1024u,
    CONE_HEIGHT = 5u,
};

TEST(a_one_to_one_cone_never_smears) {
    /* Along the row through the peak, eastward: each run of line pixels is one line, and runs start 3 or more apart. */
    synth_t s = {.kind = SYNTH_CONE};
    uint8_t *const lines = test_alloc(t, CONE_WIDTH * CONE_HEIGHT);
    for (int32_t zoom = UI_ATLAS_ZOOM_MIN; zoom <= 8; zoom++) {
        ui_atlas_geometry_t g;
        synth_draw(t, &s, zoom, 0.0f64, CONE_WIDTH, CONE_HEIGHT, lines, &g);
        const uint8_t *const row = lines + (uint64_t)(CONE_HEIGHT / 2u) * CONE_WIDTH;
        int64_t last = -1000, closest = INT64_MAX;
        uint32_t count = 0u;
        for (uint32_t i = CONE_WIDTH / 2u + 1u; i < CONE_WIDTH; i++) {
            if (row[i] != LINE_NONE && row[i - 1u] == LINE_NONE) {
                closest = (int64_t)i - last < closest ? (int64_t)i - last : closest;
                last = (int64_t)i;
                count++;
            }
        }
        if (!CHECK(closest >= 3 && count >= 3u)) {
            printf("  zoom %" PRId32 ": %u lines, the closest %" PRId64 " pixels apart\n", zoom, count, closest);
        }
    }
}

TEST_MAIN()
