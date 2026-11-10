/* ctest run_render_lod. See README.md. */
#include "harness/index.h"

#include "render/lod/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
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
    terrain_surface_fill_box(terrain->borrowed_surface, &box, out);
    if (shift <= RENDER_LOD_FEATURES_SHIFT_MAX) {
        terrain_feature_fill_box(terrain->borrowed_feature, &box, out);
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
        terrain_surface_fill_box(terrain->borrowed_surface, &box, plain);
        memcpy(cached, plain, count * sizeof(uint16_t));
        terrain_feature_fill_box(terrain->borrowed_feature, &box, plain);
        terrain_feature_fill_box_cached(terrain->borrowed_feature, &box, cached, cache);
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

TEST_MAIN()
