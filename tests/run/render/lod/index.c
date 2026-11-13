/* ctest run_render_lod. See README.md. */
#include "harness/index.h"

#include "arena/index.h"
#include "heap/index.h"
#include "render/encode/index.h"
#include "render/lod/index.h"
#include "render/octant/index.h"
#include "world/section/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    LOD_TEST_PLACES = 6u,
    /* Lines of a column looked at in each place and level: a row of a column's 16, and one down its middle. */
    LOD_TEST_LINES = 32u,
    LOD_TEST_CACHE_BOXES = 400u,
};
enum : uint64_t {
    /* Ahmed's seed, from plan 005 phase 14: forest and mountain round the spawn. */
    LOD_TEST_SEED_AHMED = 11107447384583428913ull,
};

static const uint64_t LOD_TEST_SEEDS[2] = {LOD_TEST_SEED_AHMED, TERRAIN_SEED_DEFAULT};

/* Places in blocks: the spawn, and ground of other kinds round it, out to where the far field reaches at render distance 512. */
static const int32_t LOD_TEST_PLACES_XY[LOD_TEST_PLACES][2] = {
    {-8, -8}, {1500, 300}, {-2600, 1900}, {4000, -4000}, {-6000, -500}, {9000, 7000},
};

/* What the far field sampled before plan 005 phase 14: every stage it takes, over all 1536 layers, with no cache. */
static void lod_test_sample_whole(const terrain_t *terrain, int32_t x, int32_t y, uint32_t shift, uint16_t *out) {
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = RENDER_LOD_HEIGHT_BLOCKS,
    };
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, out);
    terrain_surface_fill_box(terrain->borrowed_surface, nullptr, &box, out);
    if (shift <= RENDER_LOD_FEATURES_SHIFT_MAX) {
        terrain_feature_fill_box(terrain->borrowed_feature, nullptr, &box, out);
    }
}

/* A line of a column is the same blocks, all 1536 of them, with the chunks of what grows kept from one line to the next as without, at every
 * level that grows anything, on two seeds, lines taken in the order a job takes them. */
TEST(a_sample_with_the_cache_is_the_sample_without_it) {
    uint16_t *const whole = test_alloc(t, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t));
    uint16_t *const cached = test_alloc(t, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, RENDER_LOD_SAMPLE_SCRATCH_BYTES);
    uint64_t lines = 0u, wrong = 0u;
    for (uint32_t s = 0u; s < 2u; s++) {
        terrain_t own;
        terrain_t *const terrain = &own;
        REQUIRE(terrain_init(terrain, LOD_TEST_SEEDS[s]));
        for (uint32_t shift = LOD_SHIFT_MIN; shift <= RENDER_LOD_FEATURES_SHIFT_MAX; shift++) {
            const uint32_t size = 1u << shift;
            for (uint32_t place = 0u; place < LOD_TEST_PLACES; place++) {
                const int32_t chunk_x = LOD_TEST_PLACES_XY[place][0] >> (4 + (int32_t)shift);
                const int32_t chunk_y = LOD_TEST_PLACES_XY[place][1] >> (4 + (int32_t)shift);
                memset(scratch, 0, RENDER_LOD_SAMPLE_SCRATCH_BYTES);
                for (uint32_t line = 0u; line < LOD_TEST_LINES; line++) {
                    /* As render_lod_job_run goes: x fastest, then y, so the cache is asked as a job asks it. */
                    const uint32_t lx = line < 16u ? line : 8u;
                    const uint32_t ly = line < 16u ? 8u : line - 16u;
                    const int32_t x = (chunk_x * 16 + (int32_t)lx) * (int32_t)size + (int32_t)(size / 2u);
                    const int32_t y = (chunk_y * 16 + (int32_t)ly) * (int32_t)size + (int32_t)(size / 2u);
                    lod_test_sample_whole(terrain, x, y, shift, whole);
                    render_lod_sample_terrain(terrain, x, y, shift, cached, scratch);
                    lines++;
                    if (memcmp(whole, cached, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t)) != 0) {
                        if (wrong < 8u) {
                            printf("  seed %" PRIu32 ", shift %" PRIu32 ", line at %" PRId32 ", %" PRId32 " differs\n",
                                   s, shift, x, y);
                        }
                        wrong++;
                    }
                }
            }
        }
        terrain_deinit(terrain);
    }
    printf("  %" PRIu64 " lines, %" PRIu64 " differing\n", lines, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* The cache of chunks gives terrain_feature_fill_box's very bytes, for small boxes asked in any order, near each other and not. */
TEST(the_feature_cache_gives_the_bytes_terrain_feature_fill_box_gives) {
    terrain_t own;
    terrain_t *const terrain = &own;
    REQUIRE(terrain_init(terrain, LOD_TEST_SEED_AHMED));
    enum : uint32_t { LAYERS = 96u, SIDE = 3u, VOXELS = SIDE * SIDE * LAYERS };
    uint16_t *const plain = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const cached = test_alloc(t, VOXELS * sizeof(uint16_t));
    terrain_feature_cache_t *const cache = test_alloc(t, sizeof(terrain_feature_cache_t));
    terrain_feature_cache_start(cache);
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    uint32_t wrong = 0u, grown = 0u;
    for (uint32_t i = 0u; i < LOD_TEST_CACHE_BOXES; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        /* Mostly within a few chunks of the spawn, where the cache is used again, and now and then far off. */
        const int32_t reach = (rng & 15u) == 0u ? 4000 : 96;
        const int32_t x = (int32_t)((rng >> 8) % (uint64_t)(2 * reach)) - reach;
        const int32_t y = (int32_t)((rng >> 24) % (uint64_t)(2 * reach)) - reach;
        const uint32_t side = 1u + (uint32_t)((rng >> 40) % SIDE);
        const int32_t ground = (int32_t)terrain_height_estimate(terrain, x, y);
        const terrain_density_box_t box = {
            .origin_x = x, .origin_y = y, .origin_z = ground - 40, .count_x = side, .count_y = side, .count_z = LAYERS};
        const uint64_t count = terrain_density_box_voxels(&box);
        terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, plain);
        terrain_surface_fill_box(terrain->borrowed_surface, nullptr, &box, plain);
        memcpy(cached, plain, count * sizeof(uint16_t));
        terrain_feature_fill_box(terrain->borrowed_feature, nullptr, &box, plain);
        terrain_feature_fill_box_cached(terrain->borrowed_feature, nullptr, &box, cached, cache);
        wrong += memcmp(plain, cached, count * sizeof(uint16_t)) != 0 ? 1u : 0u;
        for (uint64_t v = 0u; v < count; v++) {
            const uint16_t block = plain[v];
            grown += block_defs[block].shape == BLOCK_SHAPE_CROSS || block == BLOCK_ID_OAK_LEAVES ? 1u : 0u;
        }
    }
    printf("  %" PRIu32 " boxes, %" PRIu32 " chunks loaded, %" PRIu32 " voxels of plants and leaves\n",
           LOD_TEST_CACHE_BOXES, cache->loads, grown);
    CHECK_EQ_U64(wrong, 0u);
    CHECK(grown > 0u);
    CHECK(cache->loads < LOD_TEST_CACHE_BOXES * 9u);
    terrain_deinit(terrain);
}

/* What a row of a far-field column costs, both ways, for the record: nothing is held to here, as this is a debug build too. */
TEST(what_a_column_costs_on_this_build) {
    terrain_t own;
    terrain_t *const terrain = &own;
    REQUIRE(terrain_init(terrain, LOD_TEST_SEED_AHMED));
    uint16_t *const out = test_alloc(t, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, RENDER_LOD_SAMPLE_SCRATCH_BYTES);
    for (uint32_t shift = 2u; shift <= 4u; shift++) {
        const uint32_t size = 1u << shift;
        int64_t whole_ns = 0, band_ns = 0;
        memset(scratch, 0, RENDER_LOD_SAMPLE_SCRATCH_BYTES);
        for (uint32_t way = 0u; way < 2u; way++) {
            const int64_t started = time_now_ns();
            for (uint32_t line = 0u; line < 16u; line++) {
                const int32_t x = (int32_t)(line * size + size / 2u), y = (int32_t)(size / 2u);
                if (way == 0u) {
                    lod_test_sample_whole(terrain, x, y, shift, out);
                } else {
                    render_lod_sample_terrain(terrain, x, y, shift, out, scratch);
                }
            }
            *(way == 0u ? &whole_ns : &band_ns) = time_now_ns() - started;
        }
        printf("  shift %" PRIu32
               ": a row of 16 lines, %.2f ms walking the chunks for each line, %.2f ms with the cache\n",
               shift, (libc_double_t)((float64_t)whole_ns / 1.0e6f64), (libc_double_t)((float64_t)band_ns / 1.0e6f64));
    }
    terrain_deinit(terrain);
}

/* ---- The pyramid's bands and plan (plan 009 phase 03), on paper ------------------------------------------------------- */

/* tan(35 degrees), half of Ahmed's 70 degree vertical field of view. */
static float64_t lod_test_half_fov(float64_t degrees) {
    return tan(degrees * 3.14159265358979323846f64 / 360.0f64);
}

static bool lod_test_near(float64_t got, float64_t want, float64_t within) {
    const bool near = fabs(got - want) <= within;
    if (!near) {
        printf("  %.3f, wanted %.3f within %.3f\n", (libc_double_t)got, (libc_double_t)want, (libc_double_t)within);
    }
    return near;
}

/* Master 2.2: at 1080 rows and 70 degrees one pixel is 1.297e-3 rad, so level k starts at 385.6 s_k / E blocks, never nearer than 16. */
TEST(bands_follow_the_pixel_error) {
    const float64_t half = lod_test_half_fov(70.0f64);
    const render_lod_bands_t two = render_lod_bands(2.0f64, 16u, half, 1080u);
    CHECK_EQ_U64(two.level_count, RENDER_LOD_LEVEL_COUNT);
    CHECK(lod_test_near(two.from[0], 0.0f64, 0.0f64));
    CHECK(lod_test_near(two.from[1], 385.6f64, 0.5f64));
    CHECK(lod_test_near(two.from[2], 771.2f64, 0.5f64));
    CHECK(lod_test_near(two.from[3], 1542.4f64, 1.0f64));
    CHECK(lod_test_near(render_lod_bands(1.0f64, 16u, half, 1080u).from[1], 771.2f64, 0.5f64));
    CHECK(lod_test_near(render_lod_bands(4.0f64, 16u, half, 1080u).from[1], 192.8f64, 0.5f64));
    CHECK(lod_test_near(render_lod_bands(8.0f64, 16u, half, 1080u).from[1], 96.4f64, 0.1f64));
    CHECK(lod_test_near(render_lod_bands(16.0f64, 16u, half, 1080u).from[1], 48.2f64, 0.1f64));
    /* 12.05 would be under Ahmed's 16 blocks: the floor holds it, and only it. */
    const render_lod_bands_t floor = render_lod_bands(64.0f64, 16u, half, 1080u);
    CHECK(lod_test_near(floor.from[1], 16.0f64, 0.0f64));
    CHECK(lod_test_near(floor.from[2], 24.1f64, 0.1f64));
    CHECK(lod_test_near(render_lod_bands(64.0f64, 32u, half, 1080u).from[1], 32.0f64, 0.0f64));
    for (uint32_t k = 1u; k <= two.level_count; k++) {
        CHECK(two.from[k + 1u] > two.from[k]);
    }
}

/* A narrow field of view zooms in, so finer levels reach further; fewer rows make pixels bigger, so levels come nearer. */
TEST(bands_follow_the_fov_and_the_rows) {
    const render_lod_bands_t wide = render_lod_bands(2.0f64, 16u, lod_test_half_fov(70.0f64), 1080u);
    const render_lod_bands_t narrow = render_lod_bands(2.0f64, 16u, lod_test_half_fov(30.0f64), 1080u);
    const render_lod_bands_t half_rows = render_lod_bands(2.0f64, 16u, lod_test_half_fov(70.0f64), 540u);
    CHECK(lod_test_near(narrow.from[1] / wide.from[1],
                        tan(35.0f64 * 3.14159265358979323846f64 / 180.0f64) /
                            tan(15.0f64 * 3.14159265358979323846f64 / 180.0f64),
                        1.0e-9f64));
    CHECK(lod_test_near(narrow.from[1] / wide.from[1], 2.61f64, 0.01f64));
    CHECK(lod_test_near(half_rows.from[1], wide.from[1] / 2.0f64, 1.0e-9f64));
    CHECK(lod_test_near(half_rows.from[1], 192.8f64, 0.5f64));
}

/* Every level whose band starts inside the render distance, and no other; each as wide as its band's far edge and the margin. */
TEST(the_plan_keeps_every_level_inside_the_render_distance) {
    const render_lod_bands_t two = render_lod_bands(2.0f64, 16u, lod_test_half_fov(70.0f64), 1080u);
    const render_lod_plan_t at_64 = render_lod_plan_by_bands(&two, 64u);
    CHECK_EQ_U64(at_64.level_count, 2u);
    CHECK_EQ_U64(at_64.shifts[0], 1u);
    CHECK_EQ_U64(at_64.shifts[1], 2u);
    /* Level 1 to 771 blocks, in columns of 32: 25 and the margin, 27 (the phase file's table). Level 2 to the render distance, 1024. */
    CHECK_EQ_U64(at_64.radii[0], 25u + RENDER_LOD_MARGIN_COLUMNS);
    CHECK_EQ_U64(at_64.radii[1], 16u + RENDER_LOD_MARGIN_COLUMNS);
    const render_lod_plan_t at_128 = render_lod_plan_by_bands(&two, 128u);
    CHECK_EQ_U64(at_128.level_count, 3u);
    CHECK_EQ_U64(at_128.shifts[2], 3u);
    CHECK_EQ_U64(at_128.radii[1], 25u + RENDER_LOD_MARGIN_COLUMNS);
    CHECK_EQ_U64(at_128.radii[2], 16u + RENDER_LOD_MARGIN_COLUMNS);
    /* At render distance 32 level 1's band starts at 386, inside 512. */
    const render_lod_plan_t at_32 = render_lod_plan_by_bands(&two, 32u);
    CHECK_EQ_U64(at_32.level_count, 1u);
    CHECK_EQ_U64(at_32.radii[0], 16u + RENDER_LOD_MARGIN_COLUMNS);
    /* Nothing past 386 blocks to draw at 16 chunks (256 blocks): no level. */
    CHECK_EQ_U64(render_lod_plan_by_bands(&two, 16u).level_count, 0u);
    /* E = 8: level 1 from 96, and its band to 193, 7 columns; the cap holds a wide level. */
    const render_lod_plan_t coarse = render_lod_plan_by_bands(&(render_lod_bands_t){0}, 64u);
    CHECK_EQ_U64(coarse.level_count, 0u);
    const render_lod_bands_t eight = render_lod_bands(8.0f64, 16u, lod_test_half_fov(70.0f64), 1080u);
    const render_lod_plan_t at_64_eight = render_lod_plan_by_bands(&eight, 64u);
    CHECK_EQ_U64(at_64_eight.level_count, 4u);
    CHECK_EQ_U64(at_64_eight.radii[0], 7u + RENDER_LOD_MARGIN_COLUMNS);
    const render_lod_bands_t fine = render_lod_bands(0.25f64, 16u, lod_test_half_fov(70.0f64), 1080u);
    const render_lod_plan_t capped = render_lod_plan_by_bands(&fine, 512u);
    CHECK(capped.level_count >= 1u);
    for (uint32_t i = 0u; i < capped.level_count; i++) {
        CHECK(capped.radii[i] <= RENDER_LOD_RADIUS_COLUMNS_MAX);
        CHECK(capped.radii[i] > RENDER_LOD_MARGIN_COLUMNS);
    }
}

/* Plan 009 phase 04: what the game holds block by block is d_1 and two chunks, at least 12 and never past the render distance; and the band
 * base the frame works out is the bands' own, before their floor. */
TEST(the_block_radius_follows_the_bands) {
    const float64_t half = lod_test_half_fov(70.0f64);
    const render_lod_bands_t two = render_lod_bands(2.0f64, 16u, half, 1080u);
    CHECK_EQ_U64(render_lod_block_radius(&two, 64u), 27u);
    CHECK_EQ_U64(render_lod_block_radius(&two, 20u), 20u);
    const render_lod_bands_t eight = render_lod_bands(8.0f64, 16u, half, 1080u);
    CHECK_EQ_U64(render_lod_block_radius(&eight, 64u), 12u); /* 96 blocks and 2 chunks is 9: the floor */
    CHECK_EQ_U64(render_lod_block_radius(&eight, 8u), 8u);
    const render_lod_bands_t four = render_lod_bands(4.0f64, 16u, half, 1080u);
    CHECK_EQ_U64(render_lod_block_radius(&four, 64u), 15u);
    /* 30 degrees wants 65 chunks at E 2: held to 48. */
    const render_lod_bands_t narrow = render_lod_bands(2.0f64, 16u, lod_test_half_fov(30.0f64), 1080u);
    CHECK_EQ_U64(render_lod_block_radius(&narrow, 512u), RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS);
    const float32_t base = render_lod_band_base(2.0f32, (float32_t)half, 1080u);
    CHECK(lod_test_near((float64_t)base * 2.0f64, two.from[1], 0.01f64));
    CHECK(lod_test_near((float64_t)base * 4.0f64, two.from[2], 0.02f64));
    CHECK(render_lod_band_base(0.0f32, (float32_t)half, 1080u) == 0.0f32);
}

/* ---- The octant store: assembled is direct (plan 009 phase 03) ------------------------------------------------------- */

enum : uint32_t {
    LOD_TEST_ASSEMBLE_CUBES = 48u,
    /* 2 x 2 chunks and two sections of height: the 32 x 32 x 32 blocks under one level 1 section. */
    LOD_TEST_CUBE = 32u,
};

static uint64_t lod_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

/* Which of the 8 level 0 sections, x fastest then y then z, a block of the cube is in. */
static uint32_t lod_test_part(uint32_t x, uint32_t y, uint32_t z) {
    return (x >> 4) + 2u * (y >> 4) + 4u * (z >> 4);
}

/* Ground of some kind in each of the 8 sections: all air, all stone (given as one value), or a slope of stone, dirt and grass with water,
 * torches and flowers on it, at a random height, with random light. Every sixth cube is all buried stone, as most of a column is. */
static void lod_test_cube_fill(uint64_t *rng, uint16_t (*blocks)[SECTION_VOXELS], uint8_t (*light)[SECTION_VOXELS],
                               uint8_t *uniform, bool buried) {
    uint32_t kind[8];
    for (uint32_t part = 0u; part < 8u; part++) {
        kind[part] = buried ? 1u : (uint32_t)(lod_test_random(rng) % 5u); /* 0 air, 1 stone, else ground */
        uniform[part] = kind[part] <= 1u ? 1u : 0u;
    }
    const int32_t base = (int32_t)(lod_test_random(rng) % LOD_TEST_CUBE);
    for (uint32_t z = 0u; z < LOD_TEST_CUBE; z++) {
        for (uint32_t y = 0u; y < LOD_TEST_CUBE; y++) {
            for (uint32_t x = 0u; x < LOD_TEST_CUBE; x++) {
                const uint32_t part = lod_test_part(x, y, z);
                const uint32_t index = section_voxel_index(x & 15u, y & 15u, z & 15u);
                const uint64_t r = lod_test_random(rng);
                uint16_t block = BLOCK_ID_AIR;
                uint8_t packed = 0u;
                if (kind[part] == 1u) {
                    block = BLOCK_ID_STONE;
                    packed = 0x31u;
                } else if (kind[part] >= 2u) {
                    const int32_t height = base + (int32_t)((x + 2u * y) / 5u) - 6 + (int32_t)(r % 3u);
                    const int32_t at = (int32_t)z;
                    block = at < height - 3 ? BLOCK_ID_STONE
                            : at < height   ? BLOCK_ID_DIRT
                            : at == height  ? BLOCK_ID_GRASS_BLOCK
                            : at < 10       ? BLOCK_ID_WATER
                                            : BLOCK_ID_AIR;
                    if (at == height + 1 && (r >> 8) % 7u == 0u) {
                        block = (r >> 12) % 2u == 0u ? BLOCK_ID_TORCH : BLOCK_ID_DANDELION;
                    }
                    packed = (uint8_t)(r >> 20);
                }
                blocks[part][index] = block;
                light[part][index] = packed;
            }
        }
    }
}

/* One level 1 section two ways, from the same random blocks: its 8 sections reduced to octants on their own, kept in the store and put
 * together; and the 32 x 32 x 32 blocks reduced in one go. The encodings must be the same bytes. */
TEST(assembled_level_1_equals_direct_reduction) {
    heap_region_t region;
    REQUIRE(heap_reserve(render_octant_arena_bytes(4u) + MC_CPU_CACHE_LINE_BYTES, &region));
    arena_t arena;
    arena_init(&arena, "octant_test", region.owned_base, region.bytes);
    render_octant_t *const store = test_alloc(t, sizeof(render_octant_t));
    render_octant_init(store, &arena, 4u);
    uint16_t (*const blocks)[SECTION_VOXELS] = test_alloc(t, 8u * SECTION_VOXELS * sizeof(uint16_t));
    uint8_t (*const light)[SECTION_VOXELS] = test_alloc(t, 8u * SECTION_VOXELS);
    uint16_t *const cells = test_alloc(t, (WORLD_BUFFERS_COLUMN_SECTIONS / 2u) * SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const cell_light = test_alloc(t, (WORLD_BUFFERS_COLUMN_SECTIONS / 2u) * SECTION_VOXELS);
    render_octant_section_t sections[WORLD_BUFFERS_COLUMN_SECTIONS / 2u];
    uint16_t *const direct = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const direct_light = test_alloc(t, SECTION_VOXELS);
    render_encode_octant_t *const octant = test_alloc(t, sizeof(render_encode_octant_t));
    uint8_t *const encoded_a = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    uint8_t *const encoded_b = test_alloc(t, WORLD_BUFFERS_SECTION_BYTES_MAX);
    uint8_t uniform[8];
    uint64_t rng = 0x5DEECE66Dull;
    uint32_t differing = 0u, assembled = 0u, uniform_sections = 0u;
    for (uint32_t cube = 0u; cube < LOD_TEST_ASSEMBLE_CUBES; cube++) {
        lod_test_cube_fill(&rng, blocks, light, uniform, cube % 6u == 0u);
        /* The store's way: each section alone, as the upload's job reduces it, into a fresh load of each column. */
        for (uint32_t slot = 0u; slot < 4u; slot++) {
            render_octant_column_set(store, slot, (int32_t)(slot & 1u), (int32_t)(slot >> 1), cube + 1u);
        }
        for (uint32_t part = 0u; part < 8u; part++) {
            const render_encode_source_t source =
                uniform[part] != 0u ? (render_encode_source_t){nullptr, nullptr, blocks[part][0], light[part][0], {0}}
                                    : (render_encode_source_t){blocks[part], light[part], 0u, 0u, {0}};
            (void)render_encode_section_octant(&source, octant);
            render_octant_section_set(store, 0u, part & 3u, part >> 2, octant);
        }
        const uint32_t slots[4] = {0u, 1u, 2u, 3u};
        const int32_t top = render_octant_column_assemble(store, 0u, slots, cells, cell_light, sections);
        const render_encode_source_t from_store =
            sections[0].assembled != 0u
                ? (render_encode_source_t){cells, cell_light, 0u, 0u, {0}}
                : (render_encode_source_t){nullptr, nullptr, sections[0].block_uniform, sections[0].light_uniform, {0}};
        assembled += sections[0].assembled;
        uniform_sections += sections[0].assembled == 0u ? 1u : 0u;
        const uint32_t bytes_a = render_encode_section(&from_store, encoded_a);
        /* Every other section of the column is air, as nothing was given for them. */
        for (uint32_t s = 1u; s < WORLD_BUFFERS_COLUMN_SECTIONS / 2u; s++) {
            CHECK(sections[s].assembled == 0u && sections[s].block_uniform == 0u);
        }
        /* The direct way: every voxel of the level 1 section from its 8 blocks. */
        int32_t direct_top = -1;
        for (uint32_t z = 0u; z < 16u; z++) {
            for (uint32_t y = 0u; y < 16u; y++) {
                for (uint32_t x = 0u; x < 16u; x++) {
                    uint16_t group[8];
                    uint8_t group_light[8];
                    for (uint32_t i = 0u; i < 8u; i++) {
                        const uint32_t bx = 2u * x + (i & 1u), by = 2u * y + ((i >> 1) & 1u), bz = 2u * z + (i >> 2);
                        const uint32_t part = lod_test_part(bx, by, bz);
                        const uint32_t index = section_voxel_index(bx & 15u, by & 15u, bz & 15u);
                        group[i] = blocks[part][index];
                        group_light[i] = light[part][index];
                    }
                    const uint32_t index = section_voxel_index(x, y, z);
                    render_encode_octant_voxel(group, group_light, &direct[index], &direct_light[index]);
                    direct_top = direct[index] != 0u ? (int32_t)z : direct_top;
                }
            }
        }
        const render_encode_source_t from_blocks = {direct, direct_light, 0u, 0u, {0}};
        const uint32_t bytes_b = render_encode_section(&from_blocks, encoded_b);
        const bool same = bytes_a == bytes_b && memcmp(encoded_a, encoded_b, bytes_a) == 0 && top == direct_top;
        if (!same && differing < 4u) {
            printf("  cube %" PRIu32 ": %" PRIu32 " bytes assembled, %" PRIu32 " direct; tops %" PRId32 ", %" PRId32
                   "\n",
                   cube, bytes_a, bytes_b, top, direct_top);
        }
        differing += same ? 0u : 1u;
    }
    printf("  %" PRIu32 " cubes: %" PRIu32 " assembled from octants, %" PRIu32 " one value all through; %" PRIu32
           " blocks live, %" PRIu64 " octants stored\n",
           LOD_TEST_ASSEMBLE_CUBES, assembled, uniform_sections, store->blocks_live, store->stats.octants_stored);
    CHECK_EQ_U64(differing, 0u);
    CHECK(assembled > 0u && uniform_sections > 0u);
    /* Blocks go back as columns load again: never more live than one cube's worth. */
    CHECK(store->blocks_live <= 8u);
    arena_deinit(&arena);
    heap_release(&region);
}

/* ---- Plan 008 phase 06: the turn cache's addressing ------------------------------------------------------------------------ */

static float64_t lod_test_unit(uint64_t *state) {
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (float64_t)(*state >> 11) * (1.0f64 / 9007199254740992.0f64) * 2.0f64 - 1.0f64;
}

/* A million random unit directions, at the game's side and at a small one: each falls in a texel of the cube, and the middle of that texel is
 * within one texel's angle of it (2 / side radians, a texel's width at a face's middle, where texels are widest). */
TEST(every_direction_has_one_texel) {
    static const uint32_t sides[2] = {RENDER_LOD_TURN_CACHE_SIDE, 7u};
    for (uint32_t s = 0u; s < 2u; s++) {
        const uint32_t side = sides[s];
        const uint64_t texels = (uint64_t)LOD_TURN_FACES * side * side;
        uint64_t state = 0x5EEDull + s, outside = 0u, far = 0u;
        float64_t worst = 0.0f64;
        for (uint32_t i = 0u; i < 1000000u; i++) {
            float64_t d[3], length = 0.0f64;
            do {
                length = 0.0f64;
                for (uint32_t a = 0u; a < 3u; a++) {
                    d[a] = lod_test_unit(&state);
                    length += d[a] * d[a];
                }
            } while (length > 1.0f64 || length < 1e-6f64);
            float32_t direction[3];
            for (uint32_t a = 0u; a < 3u; a++) {
                direction[a] = (float32_t)(d[a] / sqrt(length));
            }
            const uint32_t texel = lod_turn_texel_of(direction, side);
            if (texel >= texels) {
                outside++;
                continue;
            }
            float32_t middle[3];
            lod_turn_texel_direction(texel, side, middle);
            const float64_t dot = (float64_t)direction[0] * (float64_t)middle[0] +
                                  (float64_t)direction[1] * (float64_t)middle[1] +
                                  (float64_t)direction[2] * (float64_t)middle[2];
            const float64_t angle = acos(dot > 1.0f64 ? 1.0f64 : dot);
            worst = angle > worst ? angle : worst;
            far += angle > 2.0f64 / (float64_t)side ? 1u : 0u;
        }
        printf("  side %" PRIu32 ": %" PRIu64 " outside, %" PRIu64
               " further than a texel, at worst %.5f rad (a texel %.5f)\n",
               side, outside, far, (libc_double_t)worst, (libc_double_t)(2.0f64 / (float64_t)side));
        CHECK_EQ_U64(outside, 0u);
        CHECK_EQ_U64(far, 0u);
    }
}

/* The six axes go to their own faces, at the face's middle, and the middle texels point back along them. */
TEST(the_six_face_centres_map_to_their_own_face) {
    const uint32_t side = RENDER_LOD_TURN_CACHE_SIDE;
    for (uint32_t face = 0u; face < LOD_TURN_FACES; face++) {
        float32_t direction[3] = {0.0f32, 0.0f32, 0.0f32};
        direction[face >> 1] = (face & 1u) != 0u ? -1.0f32 : 1.0f32;
        const uint32_t texel = lod_turn_texel_of(direction, side);
        CHECK_EQ_U64(texel / (side * side), face);
        CHECK_EQ_U64(texel % (side * side), (side / 2u) * side + side / 2u);
        float32_t middle[3];
        lod_turn_texel_direction(texel, side, middle);
        CHECK(middle[face >> 1] * direction[face >> 1] > 0.999999f32);
        /* A long way down one axis, a hair off the others, stays on that face. */
        direction[(face >> 1) == 0u ? 1u : 0u] = 1e-7f32;
        CHECK_EQ_U64(lod_turn_texel_of(direction, side) / (side * side), face);
    }
}

TEST_MAIN()
