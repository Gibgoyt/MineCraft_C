/* ctest run_climate_layer. See README.md. */
#include "harness/index.h"

#include "climate/layer/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct layer_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} layer_rig_t;
static_assert(sizeof(layer_rig_t) == 24);

static void *layer_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool layer_rig_start(test_context_t *t, layer_rig_t *rig) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = layer_aligned(t, sizeof(job_system_t));
    rig->climate = layer_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void layer_rig_stop(layer_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

static uint32_t layer_pixel(const uint8_t *pixels, uint32_t pitch, uint32_t x, uint32_t y) {
    uint32_t colour = 0u;
    memcpy(&colour, pixels + (uint64_t)y * pitch + (uint64_t)x * 4u, sizeof(colour));
    return colour;
}

TEST(each_layer_is_the_colour_the_readme_says) {
    climate_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_SEA_DISTANCE, &sample), 0x000000u);
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_CONTINENTALITY, &sample), 0x2060C0u);
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_WATER, &sample), 0x406040u);
    sample.sea_distance = 8192.0f32;
    sample.kappa = 1.0f32;
    sample.z_ground = 454.0f32;
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_SEA_DISTANCE, &sample), 0xFFFFFFu);
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_CONTINENTALITY, &sample), 0xA06030u);
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_WATER, &sample), 0xF0F0F0u);
    sample.sea_distance = 4096.0f32; /* half way: 127.5 rounds to 128 */
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_SEA_DISTANCE, &sample), 0x808080u);
    sample.flags = CLIMATE_CELL_SEA;
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_WATER, &sample), 0x103070u);
    sample.flags = CLIMATE_CELL_LAKE;
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_WATER, &sample), 0x3080E0u);
    sample.flags = CLIMATE_CELL_LAKE_SMALL;
    CHECK_EQ_U64(climate_layer_colour(CLIMATE_LAYER_WATER, &sample), 0x70B0F0u);

    /* On a straight coast at x = 0, drawn: deep sea to the west is black, blue and dark blue; land to the east is not. */
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t coast = {.kind = GROUND_COAST_X, .coast = 0};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &coast));
    climate_prepare_area(rig.climate, rig.job, -1024, 0, 1023, 1023, nullptr, nullptr);
    enum : uint32_t { WIDE = 64u, HIGH = 32u };
    uint8_t *const pixels = test_alloc(t, WIDE * HIGH * 4u);
    climate_layer_draw(rig.climate, CLIMATE_LAYER_SEA_DISTANCE, -1024, 1023, 32, WIDE, HIGH, 0u, pixels, WIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 4u, 10u), 0x000000u);
    CHECK(layer_pixel(pixels, WIDE * 4u, 60u, 10u) != 0x000000u);
    climate_layer_draw(rig.climate, CLIMATE_LAYER_CONTINENTALITY, -1024, 1023, 32, WIDE, HIGH, 0u, pixels, WIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 4u, 10u), 0x2060C0u);
    climate_layer_draw(rig.climate, CLIMATE_LAYER_WATER, -1024, 1023, 32, WIDE, HIGH, 0u, pixels, WIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 4u, 10u), 0x103070u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 60u, 10u),
                 climate_layer_colour(CLIMATE_LAYER_WATER, &(climate_sample_t){.z_ground = 20.0f32}));
    layer_rig_stop(&rig);
}

TEST(latitude_lines_land_on_the_rows_of_their_latitudes) {
    enum : uint32_t { WIDE = 4u, HIGH = 5000u };
    static const int32_t NORTH = 40000, STEP = 16;
    uint8_t *const pixels = test_alloc(t, WIDE * HIGH * 4u);
    climate_layer_draw(nullptr, CLIMATE_LAYER_LATITUDE, 0, NORTH, STEP, WIDE, HIGH, 0u, pixels, WIDE * 4u);
    /* Rows whose y range [40000 - 16 (r + 1), 40000 - 16 r) holds 0, +-10,000 and +-28,297.87. */
    const float64_t ys[5] = {0.0f64, 10000.0f64, -10000.0f64, 66.5f64 * CLIMATE_POLE_Y / 90.0f64,
                             -66.5f64 * CLIMATE_POLE_Y / 90.0f64};
    const uint32_t colours[5] = {0xFF2020u, 0xFF9020u, 0xFF9020u, 0x20E0FFu, 0x20E0FFu};
    uint32_t rows[5];
    for (uint32_t k = 0u; k < 5u; k++) {
        rows[k] = (uint32_t)(ceil(((float64_t)NORTH - ys[k]) / (float64_t)STEP) - 1.0f64);
    }
    CHECK_EQ_U64(rows[0], 2499u);
    CHECK_EQ_U64(rows[1], 1874u);
    uint64_t coloured = 0u, wrong = 0u;
    for (uint32_t r = 0u; r < HIGH; r++) {
        const uint32_t colour = layer_pixel(pixels, WIDE * 4u, 0u, r);
        uint32_t expected = 0u;
        for (uint32_t k = 0u; k < 5u; k++) {
            expected = rows[k] == r ? colours[k] : expected;
        }
        coloured += colour != 0u ? 1u : 0u;
        wrong += colour != expected || layer_pixel(pixels, WIDE * 4u, WIDE - 1u, r) != expected ? 1u : 0u;
    }
    CHECK_EQ_U64(coloured, 5u);
    CHECK_EQ_U64(wrong, 0u);
}

TEST(unbuilt_tiles_are_grey) {
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    enum : uint32_t { SIDE = 32u };
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    climate_layer_draw(rig.climate, CLIMATE_LAYER_WATER, 0, 1024, 32, SIDE, SIDE, 0u, pixels, SIDE * 4u);
    uint64_t grey = 0u;
    for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
        grey += layer_pixel(pixels, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
    }
    CHECK_EQ_U64(grey, SIDE * SIDE);
    layer_rig_stop(&rig);
}

static uint64_t layer_digest(const uint8_t *data, uint64_t bytes) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

/* Each layer at 512 by 512 on a real seed. The hashes are pinned only after Ahmed has looked at the pictures and said yes,
 * as run_ui_menu does (plans/REPO_STRUCTURE.md section 10). Until then this prints them and holds each to itself: two
 * draws give the same bytes. */
TEST(a_layer_picture_is_held_to_a_hash) {
    enum : uint32_t { SIDE = 512u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, -4096, -4096, 4095, 4095, nullptr, nullptr);
    uint8_t *const first = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const second = test_alloc(t, SIDE * SIDE * 4u);
    static const char *const NAMES[3] = {"sea distance", "continentality", "water and latitude"};
    for (uint32_t layer = 0u; layer < 3u; layer++) {
        climate_layer_draw(rig.climate, (climate_layer_t)layer, -4096, 4096, 16, SIDE, SIDE, 0u, first, SIDE * 4u);
        climate_layer_draw(rig.climate, (climate_layer_t)layer, -4096, 4096, 16, SIDE, SIDE, 0u, second, SIDE * 4u);
        if (layer == CLIMATE_LAYER_WATER) {
            climate_layer_draw(rig.climate, CLIMATE_LAYER_LATITUDE, -4096, 4096, 16, SIDE, SIDE, 0u, first, SIDE * 4u);
            climate_layer_draw(rig.climate, CLIMATE_LAYER_LATITUDE, -4096, 4096, 16, SIDE, SIDE, 0u, second, SIDE * 4u);
        }
        const uint64_t hash = layer_digest(first, SIDE * SIDE * 4u);
        printf("%s: 0x%016" PRIX64 "\n", NAMES[layer], hash);
        CHECK_EQ_U64(hash, layer_digest(second, SIDE * SIDE * 4u));
        uint64_t grey = 0u;
        for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
            grey += layer_pixel(first, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
        }
        /* Every tile it reads was built. Not asked of sea distance, where 4096 blocks is that very grey. */
        CHECK(layer == CLIMATE_LAYER_SEA_DISTANCE || grey == 0u);
    }
    layer_rig_stop(&rig);
    terrain_deinit(&terrain);
}

TEST_MAIN()
