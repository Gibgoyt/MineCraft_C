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

/* ---- Phase 04's layers ---------------------------------------------------------------------------------------------- */

TEST(current_arrows_are_red_blue_and_grey_and_point_along_the_flow) {
    enum : uint32_t { SIDE = 64u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t ground = ground_continent(-2048, -14766, 2048, -10766);
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &ground));
    climate_prepare_area(rig.climate, rig.job, -2600, -13100, 2600, -10400, nullptr, nullptr);
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    /* 8 blocks a pixel, the arrow point at pixel (16, 16) on the sea cell just off the west coast: centre (-2080, -12768). */
    climate_layer_draw(rig.climate, CLIMATE_LAYER_CURRENTS, -2080 - 132, -12768 + 132, 8, SIDE, SIDE, 196u, pixels,
                       SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 16u), 0x3060E0u); /* blue, cold */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 6u), 0x3060E0u);  /* its head is north, 15 pixels up */
    CHECK(layer_pixel(pixels, SIDE * 4u, 16u, 24u) != 0x3060E0u);
    /* Land east of it is the water layer's colour, unchanged. */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 60u, 16u),
                 climate_layer_colour(CLIMATE_LAYER_WATER, &(climate_sample_t){.z_ground = 30.0f32}));
    /* Off the east coast: red, head south. */
    climate_layer_draw(rig.climate, CLIMATE_LAYER_CURRENTS, 2080 - 132, -12768 + 132, 8, SIDE, SIDE, 196u, pixels,
                       SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 16u), 0xE03030u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 26u), 0xE03030u);
    CHECK(layer_pixel(pixels, SIDE * 4u, 16u, 8u) != 0xE03030u);
    /* Off the north coast, at 30 S, the current is neutral: a grey cross, no arrow. */
    climate_layer_draw(rig.climate, CLIMATE_LAYER_CURRENTS, -132, -10766 + 32 + 132, 8, SIDE, SIDE, 196u, pixels,
                       SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 16u), 0x909090u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 15u, 16u), 0x909090u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 17u), 0x909090u);
    layer_rig_stop(&rig);
}

TEST(pressure_lines_fall_on_multiples_of_four) {
    enum : uint32_t { WIDE = 8u, HIGH = 400u };
    uint8_t *const pixels = test_alloc(t, WIDE * HIGH * 4u);
    /* Rising linearly toward the north, row 0: 1022.01 at row 0, down 0.05 a row, so 1020, 1016, ... 1004 are crossed. */
    for (uint32_t r = 0u; r < HIGH; r++) {
        const float32_t p = 1022.01f32 - 0.05f32 * (float32_t)r;
        for (uint32_t c = 0u; c < WIDE; c++) {
            memcpy(pixels + ((uint64_t)r * WIDE + c) * 4u, &p, sizeof(p));
        }
    }
    climate_layer_pressure_finish(WIDE, HIGH, pixels, WIDE * 4u);
    uint32_t lines = 0u;
    for (uint32_t r = 0u; r < HIGH; r++) {
        const bool line = layer_pixel(pixels, WIDE * 4u, 3u, r) == 0x202020u;
        const bool want = r == 41u || r == 121u || r == 201u || r == 281u || r == 361u;
        CHECK(line == want);
        lines += line ? 1u : 0u;
        /* The whole row alike: lines run along equal pressure. */
        CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 0u, r), layer_pixel(pixels, WIDE * 4u, WIDE - 1u, r));
    }
    CHECK_EQ_U64(lines, 5u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 0u, 0u), climate_layer_pressure_colour(1022.01f32));
    CHECK_EQ_U64(climate_layer_pressure_colour(990.0f32), 0x3050C0u);
    CHECK_EQ_U64(climate_layer_pressure_colour(1013.0f32), 0xF0F0F0u);
    CHECK_EQ_U64(climate_layer_pressure_colour(1030.0f32), 0xC04030u);
    CHECK_EQ_U64(climate_layer_pressure_colour(1050.0f32), 0xC04030u);
    CHECK_EQ_U64(climate_layer_wind_colour(0.0f32), 0x202040u);
    CHECK_EQ_U64(climate_layer_wind_colour(15.0f32), 0xF0E040u);
    /* Unbuilt: grey, and no line next to it. */
    const float32_t none = nanf("");
    memcpy(pixels, &none, sizeof(none));
    const float32_t p = 1010.5f32;
    memcpy(pixels + 4u, &p, sizeof(p));
    memcpy(pixels + WIDE * 4u, &p, sizeof(p));
    climate_layer_pressure_finish(2u, 2u, pixels, WIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 0u, 0u), 0x808080u);
    CHECK_EQ_U64(layer_pixel(pixels, WIDE * 4u, 1u, 0u), climate_layer_pressure_colour(1010.5f32));
}

TEST(wind_arrows_point_downwind) {
    enum : uint32_t { SIDE = 64u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t sea = {.kind = GROUND_OPEN_SEA};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &sea));
    /* 15 degrees north is y = 6383. */
    climate_prepare_area(rig.climate, rig.job, -300, 6000, 300, 6700, nullptr, nullptr);
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    climate_layer_draw(rig.climate, CLIMATE_LAYER_WIND, -132, 6383 + 132, 8, SIDE, SIDE, 105u, pixels, SIDE * 4u);
    uint32_t west = 0u, east = 0u, north = 0u, south = 0u;
    for (uint32_t r = 0u; r < 32u; r++) {
        for (uint32_t c = 0u; c < 32u; c++) {
            if (layer_pixel(pixels, SIDE * 4u, c, r) != 0xFFFFFFu) {
                continue;
            }
            west += c < 16u ? 1u : 0u;
            east += c > 16u ? 1u : 0u;
            north += r < 16u ? 1u : 0u;
            south += r > 16u ? 1u : 0u;
        }
    }
    printf("15 N open sea arrow: %u pixels west of its tail, %u east, %u south, %u north\n", west, east, south, north);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 16u, 16u), 0xFFFFFFu);
    CHECK(west >= 8u && east == 0u); /* west-south-west: mostly west, a little south */
    CHECK(south >= 2u && north == 0u);
    CHECK(west > south);
    /* The background is the speed's colour: the trade at 15 degrees, about 6 blocks a second. */
    climate_air_t air;
    REQUIRE(climate_air_at(rig.climate, -132.0f64 + 8.0f64 * 2.5f64, 6383.0f64 + 132.0f64 - 8.0f64 * 2.5f64, 105.0f32,
                           &air));
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 2u, 2u),
                 climate_layer_wind_colour(sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1])));
    layer_rig_stop(&rig);
}

/* Phase 04's three layers on the real seed, on the plan's four days. Printed and held to themselves until Ahmed has seen
 * the pictures (bench_climate --pictures): the hashes are pinned only after he says yes. */
TEST(each_new_layer_is_held_to_a_hash) {
    enum : uint32_t { SIDE = 256u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, -2048, -2048, 2047, 2047, nullptr, nullptr);
    uint8_t *const first = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const second = test_alloc(t, SIDE * SIDE * 4u);
    static const uint32_t DAYS[4] = {15u, 105u, 196u, 288u};
    static const char *const NAMES[3] = {"currents", "pressure", "wind"};
    for (uint32_t layer = CLIMATE_LAYER_CURRENTS; layer <= CLIMATE_LAYER_WIND; layer++) {
        for (uint32_t d = 0u; d < 4u; d++) {
            if (layer == CLIMATE_LAYER_CURRENTS && d > 0u) {
                break; /* currents do not change through the year */
            }
            climate_layer_draw(rig.climate, (climate_layer_t)layer, -2048, 2048, 16, SIDE, SIDE, DAYS[d], first,
                               SIDE * 4u);
            climate_layer_draw(rig.climate, (climate_layer_t)layer, -2048, 2048, 16, SIDE, SIDE, DAYS[d], second,
                               SIDE * 4u);
            const uint64_t hash = layer_digest(first, SIDE * SIDE * 4u);
            printf("%s, day %u: 0x%016" PRIX64 "\n", NAMES[layer - CLIMATE_LAYER_CURRENTS], DAYS[d], hash);
            CHECK_EQ_U64(hash, layer_digest(second, SIDE * SIDE * 4u));
            uint64_t grey = 0u;
            for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
                grey += layer_pixel(first, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
            }
            CHECK_EQ_U64(grey, 0u);
        }
    }
    layer_rig_stop(&rig);
    terrain_deinit(&terrain);
}

/* ---- Phase 05's layers ---------------------------------------------------------------------------------------------- */

TEST(each_phase_05_layer_is_the_colour_the_readme_says) {
    climate_normals_at_t n;
    memset(&n, 0, sizeof(n));
    n.t_day_c = 0.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_TEMPERATURE_DAY, &n), 0xF0F0F0u);
    n.t_mean_c = 15.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_TEMPERATURE_MEAN, &n), 0xF0D060u);
    n.t_max_month_c = 60.0f32; /* held past the last stop */
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_TEMPERATURE_WARMEST, &n), 0x801010u);
    n.t_min_month_c = -50.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_TEMPERATURE_COLDEST, &n), 0x3040A0u);
    CHECK_EQ_U64(climate_layer_temperature_colour(-20.0f32), 0x4870C0u); /* halfway from -30 to -10 */
    n.rain_mm_year = 1000.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_RAIN_YEAR, &n), 0x40A040u);
    n.rain_mm_day = 2000.0f32 / 365.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_RAIN_DAY, &n), 0x2070C0u);
    n.climate_class = CLIMATE_CLASS_DESERT;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_CLIMATE_CLASS, &n), 0xF0D080u);
    n.climate_class = CLIMATE_CLASS_BOREAL;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_CLIMATE_CLASS, &n), 0x305060u);
    /* Snow balance: above it white, 50 below light grey, 2000 below dark. */
    n.z = 900.0f32;
    n.snow_balance_z = 800.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_SNOW_BALANCE, &n), 0xFFFFFFu);
    n.snow_balance_z = 950.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_SNOW_BALANCE, &n), 0xA0A0A0u);
    n.snow_balance_z = 9999.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_SNOW_BALANCE, &n), 0x404040u);
    /* Seasons over the water layer. */
    n.flags = CLIMATE_CELL_SEA;
    n.hurricane_s = 1.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_HURRICANE_SEASON, &n), 0xFF2020u);
    n.hurricane_s = 0.0f32;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_HURRICANE_SEASON, &n), 0x103070u);
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_SNOW_BALANCE, &n), 0x103070u);
    n.climate_class = CLIMATE_CLASS_NONE;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_CLIMATE_CLASS, &n), 0x103070u);
    n.flags = CLIMATE_CELL_HURRICANE_COAST;
    n.hurricane_season = 1u;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_HURRICANE_SEASON, &n), 0xFF9040u);
    n.flags = CLIMATE_CELL_MONSOON;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_MONSOON, &n), 0x208080u);
    n.monsoon_today = 1u;
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_MONSOON, &n), 0x30E0E0u);
    CHECK_EQ_U64(climate_layer_normals_colour(CLIMATE_LAYER_WATER, &n), 0x808080u);
}

TEST(the_day_moves_the_day_layers_and_not_the_others) {
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    climate_prepare_area(rig.climate, rig.job, 0, 17000, 1023, 18023, nullptr, nullptr); /* 40 N */
    enum : uint32_t { SIDE = 16u };
    uint8_t *const january = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const july = test_alloc(t, SIDE * SIDE * 4u);
    static const climate_layer_t LAYERS[4] = {CLIMATE_LAYER_TEMPERATURE_DAY, CLIMATE_LAYER_TEMPERATURE_MEAN,
                                              CLIMATE_LAYER_TEMPERATURE_WARMEST, CLIMATE_LAYER_RAIN_YEAR};
    static const bool MOVES[4] = {true, false, false, false};
    for (uint32_t l = 0u; l < 4u; l++) {
        climate_layer_draw(rig.climate, LAYERS[l], 100, 17900, 32, SIDE, SIDE, 15u, january, SIDE * 4u);
        climate_layer_draw(rig.climate, LAYERS[l], 100, 17900, 32, SIDE, SIDE, 197u, july, SIDE * 4u);
        CHECK(MOVES[l] == (memcmp(january, july, SIDE * SIDE * 4u) != 0));
    }
    layer_rig_stop(&rig);
}

TEST(phase_05_layers_are_grey_where_unbuilt) {
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    enum : uint32_t { SIDE = 16u };
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    for (uint32_t layer = CLIMATE_LAYER_TEMPERATURE_DAY; layer < CLIMATE_LAYER_COUNT; layer++) {
        climate_layer_draw(rig.climate, (climate_layer_t)layer, 0, 1024, 64, SIDE, SIDE, 100u, pixels, SIDE * 4u);
        uint64_t grey = 0u;
        for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
            grey += layer_pixel(pixels, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
        }
        CHECK_EQ_U64(grey, SIDE * SIDE);
    }
    layer_rig_stop(&rig);
}

/* Phase 05's ten layers on the real seed, the day ones on days 15 and 197. Printed and held to themselves until Ahmed has
 * seen the pictures (bench_climate --pictures): the hashes are pinned only after he says yes. */
TEST(each_phase_05_layer_is_held_to_a_hash) {
    enum : uint32_t { SIDE = 512u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, -4096, -4096, 4095, 4095, nullptr, nullptr);
    uint8_t *const first = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const second = test_alloc(t, SIDE * SIDE * 4u);
    static const char *const NAMES[10] = {
        "temperature day",  "temperature mean", "temperature warmest", "temperature coldest", "rain year", "rain day",
        "hurricane season", "monsoon",          "climate class",       "snow balance"};
    static const bool BY_DAY[10] = {true, false, false, false, false, true, true, true, false, false};
    static const uint32_t DAYS[2] = {15u, 197u};
    for (uint32_t l = 0u; l < 10u; l++) {
        const climate_layer_t layer = (climate_layer_t)(CLIMATE_LAYER_TEMPERATURE_DAY + l);
        for (uint32_t d = 0u; d < (BY_DAY[l] ? 2u : 1u); d++) {
            climate_layer_draw(rig.climate, layer, -4096, 4096, 16, SIDE, SIDE, DAYS[d], first, SIDE * 4u);
            climate_layer_draw(rig.climate, layer, -4096, 4096, 16, SIDE, SIDE, DAYS[d], second, SIDE * 4u);
            const uint64_t hash = layer_digest(first, SIDE * SIDE * 4u);
            printf("%s, day %u: 0x%016" PRIX64 "\n", NAMES[l], DAYS[d], hash);
            CHECK_EQ_U64(hash, layer_digest(second, SIDE * SIDE * 4u));
            uint64_t grey = 0u;
            for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
                grey += layer_pixel(first, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
            }
            CHECK_EQ_U64(grey, 0u);
        }
    }
    layer_rig_stop(&rig);
    terrain_deinit(&terrain);
}

/* ---- Phase 06's now layers ------------------------------------------------------------------------------------------ */

/* A system placed by hand, still for its whole life, as the systems test places them. */
static void layer_hand(climate_system_t *system, uint8_t kind, float64_t x, float64_t y, float32_t peak,
                       float32_t radius, float32_t core) {
    memset(system, 0, sizeof(*system));
    system->kind = kind;
    system->spawn_day = 0.0f64;
    system->start_x = x;
    system->start_y = y;
    system->life_days = 8.0f32;
    system->peak_hpa = peak;
    system->radius = radius;
    system->core_radius = core;
    system->front_length = 3000.0f32;
    system->shield_mm_hour = 2.0f32;
    system->sample_count = 33u;
    for (uint32_t k = 0u; k < system->sample_count; k++) {
        system->samples[k].x = x;
        system->samples[k].y = y;
        system->samples[k].weaken = 1.0f32;
    }
}

TEST(each_now_layer_is_the_colour_the_readme_says) {
    enum : uint32_t { SIDE = 256u };
    uint32_t colour = 0u;
    CHECK(!climate_layer_radar_colour(0.05f32, &colour));
    static const float32_t RATES[7] = {0.1f32, 1.0f32, 4.0f32, 10.0f32, 25.0f32, 50.0f32, 100.0f32};
    static const uint32_t WANT[7] = {0x80C0FFu, 0x40A040u, 0xE0E040u, 0xF08020u, 0xE02020u, 0xC020C0u, 0xC020C0u};
    for (uint32_t k = 0u; k < 7u; k++) {
        CHECK(climate_layer_radar_colour(RATES[k], &colour));
        CHECK_EQ_U64(colour, WANT[k]);
    }
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, 5u, ground_fn, &sea));
    /* 256 pixels of 16 blocks: 4096 blocks round (0, 19149), 45 N, every tile built. */
    const int32_t west = -2048, north = 19149 + 2048, step = 16;
    climate_prepare_area(rig.climate, rig.job, west, north - 4096, west + 4095, north - 1, nullptr, nullptr);
    climate_system_t *const systems = test_alloc(t, 3u * sizeof(climate_system_t));
    layer_hand(&systems[0], CLIMATE_SYSTEM_HURRICANE, 0.0f64, 19149.0f64, -60.0f32, 640.0f32, 100.0f32);
    layer_hand(&systems[1], CLIMATE_SYSTEM_LOW, -1200.0f64, 20200.0f64, -20.0f32, 800.0f32, 400.0f32);
    layer_hand(&systems[2], CLIMATE_SYSTEM_HIGH, 1200.0f64, 18100.0f64, 14.0f32, 800.0f32, 400.0f32);
    climate_systems_day_t day;
    memset(&day, 0, sizeof(day));
    day.borrowed_systems = systems;
    day.system_count = 3u;
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    const float64_t moment = 4.0f64;
    /* Pixel of a block: column floor((x - west) / 16), row floor((north - y) / 16). */
    const uint32_t hx = (uint32_t)((0 - west) / step), hy = (uint32_t)((north - 19149) / step);
    const uint32_t lx = (uint32_t)((-1200 - west) / step), ly = (uint32_t)((north - 20200) / step);
    const uint32_t gx = (uint32_t)((1200 - west) / step), gy = (uint32_t)((north - 18100) / step);
    climate_layer_draw_now(rig.climate, &day, CLIMATE_LAYER_PRESSURE_NOW, moment, west, north, step, SIDE, SIDE, pixels,
                           SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, gx, gy), CLIMATE_LAYER_HIGH_MARK);     /* the H's crossbar */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, lx - 2u, ly), CLIMATE_LAYER_LOW_MARK); /* the L's upright */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx - 2u, hy), CLIMATE_LAYER_LOW_MARK); /* a hurricane is an L too */
    climate_layer_draw_now(rig.climate, &day, CLIMATE_LAYER_SYSTEMS, moment, west, north, step, SIDE, SIDE, pixels,
                           SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx + 40u, hy), CLIMATE_LAYER_HURRICANE_RING); /* 640 / 16 = 40 */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, lx, ly - 50u), CLIMATE_LAYER_LOW_RING);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, gx - 50u, gy), CLIMATE_LAYER_HIGH_RING);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx, hy), CLIMATE_LAYER_TRACK); /* where it is now ends its track */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx + 5u, hy), CLIMATE_LAYER_HURRICANE_RING); /* the eye, 100 blocks */
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, 2u, 2u), CLIMATE_LAYER_SEA);
    uint64_t front = 0u;
    for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
        front += layer_pixel(pixels, SIDE * 4u, i % SIDE, i / SIDE) == CLIMATE_LAYER_FRONT ? 1u : 0u;
    }
    CHECK(front > 100u); /* the low's 3000-block front, 187 pixels and its teeth */
    /* A cell at the hurricane's place, alive at the moment: a disc and the lightning mark. */
    climate_thunder_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.spawn_day = moment - 0.05f64;
    cell.start_x = 0.0f64;
    cell.start_y = 19149.0f64;
    cell.radius = 320.0f32;
    cell.peak_mm_hour = 40.0f32;
    cell.life_days = 0.125f32;
    climate_systems_day_t cells;
    memset(&cells, 0, sizeof(cells));
    cells.borrowed_thunder = &cell;
    cells.thunder_count = 1u;
    climate_layer_draw_now(rig.climate, &cells, CLIMATE_LAYER_THUNDER, moment, west, north, step, SIDE, SIDE, pixels,
                           SIDE * 4u);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx, hy), CLIMATE_LAYER_LIGHTNING);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx + 10u, hy), CLIMATE_LAYER_THUNDER_DISC);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx + 30u, hy), CLIMATE_LAYER_SEA);
    /* The radar there: the cell's rain times the sea's wetness, plus the background, in its step's colour. */
    float32_t rain = 0.0f32;
    REQUIRE(climate_layer_rain_now(rig.climate, &cells, 0.0f64, 19149.0f64, moment, &rain));
    climate_layer_draw_now(rig.climate, &cells, CLIMATE_LAYER_RAIN_RADAR, moment, west, north, step, SIDE, SIDE, pixels,
                           SIDE * 4u);
    uint32_t want = CLIMATE_LAYER_SEA;
    (void)climate_layer_radar_colour(rain, &want);
    printf("radar at the cell: %.2f mm an hour\n", (libc_double_t)rain);
    CHECK(rain > 0.1f32);
    CHECK_EQ_U64(layer_pixel(pixels, SIDE * 4u, hx, hy), want);
    layer_rig_stop(&rig);
}

TEST(systems_over_grey_land_are_outlines) {
    enum : uint32_t { SIDE = 256u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, 5u, ground_fn, &sea));
    /* Nothing built: a hurricane that moved east, its circle and track over grey, and no eye. */
    climate_system_t *const h = test_alloc(t, sizeof(climate_system_t));
    layer_hand(h, CLIMATE_SYSTEM_HURRICANE, 0.0f64, 6383.0f64, -60.0f32, 640.0f32, 100.0f32);
    for (uint32_t k = 0u; k < h->sample_count; k++) {
        h->samples[k].x = -1500.0f64 + 100.0f64 * (float64_t)k;
    }
    climate_systems_day_t day;
    memset(&day, 0, sizeof(day));
    day.borrowed_systems = h;
    day.system_count = 1u;
    uint8_t *const pixels = test_alloc(t, SIDE * SIDE * 4u);
    static const climate_layer_t LAYERS[5] = {CLIMATE_LAYER_SYSTEMS, CLIMATE_LAYER_PRESSURE_NOW,
                                              CLIMATE_LAYER_RAIN_RADAR, CLIMATE_LAYER_THUNDER, CLIMATE_LAYER_WIND_NOW};
    for (uint32_t l = 0u; l < 5u; l++) {
        climate_layer_draw_now(rig.climate, &day, LAYERS[l], 2.0f64, -2048, 6383 + 2048, 16, SIDE, SIDE, pixels,
                               SIDE * 4u);
        uint64_t grey = 0u, ring = 0u, track = 0u, other = 0u;
        for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
            const uint32_t c = layer_pixel(pixels, SIDE * 4u, i % SIDE, i / SIDE);
            grey += c == CLIMATE_LAYER_GREY ? 1u : 0u;
            ring += c == CLIMATE_LAYER_HURRICANE_RING ? 1u : 0u;
            track += c == CLIMATE_LAYER_TRACK ? 1u : 0u;
            other +=
                (c != CLIMATE_LAYER_GREY && c != CLIMATE_LAYER_HURRICANE_RING && c != CLIMATE_LAYER_TRACK) ? 1u : 0u;
        }
        printf("layer %u over grey: %" PRIu64 " grey, %" PRIu64 " circle, %" PRIu64 " track\n", (uint32_t)LAYERS[l],
               grey, ring, track);
        CHECK(ring > 100u && ring < 400u); /* a 1-pixel circle of radius 40, not a filled eye */
        CHECK_EQ_U64(other, 0u);
        CHECK(l == 0u ? track > 10u : track == 0u); /* the track only in the systems layer */
    }
    layer_rig_stop(&rig);
}

/* The five now layers on the real seed at two moments, 512 by 512, from real day lists. Printed and held to themselves
 * until Ahmed has seen the frames (bench_climate --season): pinned only after he says yes. */
TEST(a_now_layer_picture_is_held_to_a_hash) {
    enum : uint32_t { SIDE = 512u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, -4096, -4096, 4095, 4095, nullptr, nullptr);
    arena_t *const arena = layer_aligned(t, sizeof(arena_t));
    const uint64_t arena_bytes = 8ull << 20;
    arena_init(arena, "test_layer_systems", layer_aligned(t, arena_bytes), arena_bytes);
    uint8_t *const first = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const second = test_alloc(t, SIDE * SIDE * 4u);
    static const char *const NAMES[5] = {"pressure now", "systems", "rain radar", "thunder", "wind now"};
    static const float64_t MOMENTS[2] = {200.6f64, 233.3f64};
    for (uint32_t m = 0u; m < 2u; m++) {
        climate_systems_day_t day;
        arena_reset(arena);
        REQUIRE(climate_systems_day_build(rig.climate, arena, 0.0f64, 0.0f64, 6000.0f32, 4096.0f32,
                                          (int64_t)floor(MOMENTS[m]), CLIMATE_SYSTEMS_ALL, &day));
        printf("day %.1f: %u big systems, %u cells, complete %u\n", (libc_double_t)MOMENTS[m], day.system_count,
               day.thunder_count, day.thunder_complete);
        for (uint32_t l = 0u; l < 5u; l++) {
            const climate_layer_t layer = (climate_layer_t)(CLIMATE_LAYER_PRESSURE_NOW + l);
            climate_layer_draw_now(rig.climate, &day, layer, MOMENTS[m], -4096, 4096, 16, SIDE, SIDE, first, SIDE * 4u);
            climate_layer_draw_now(rig.climate, &day, layer, MOMENTS[m], -4096, 4096, 16, SIDE, SIDE, second,
                                   SIDE * 4u);
            const uint64_t hash = layer_digest(first, SIDE * SIDE * 4u);
            printf("%s at %.1f: 0x%016" PRIX64 "\n", NAMES[l], (libc_double_t)MOMENTS[m], hash);
            CHECK_EQ_U64(hash, layer_digest(second, SIDE * SIDE * 4u));
            uint64_t grey = 0u;
            for (uint32_t i = 0u; i < SIDE * SIDE; i++) {
                grey += layer_pixel(first, SIDE * 4u, i % SIDE, i / SIDE) == 0x808080u ? 1u : 0u;
            }
            CHECK_EQ_U64(grey, 0u);
        }
    }
    layer_rig_stop(&rig);
    terrain_deinit(&terrain);
}

/* ---- Plan 004 phase 09: the scaled draw the Atlas uses ------------------------------------------------------------ */

TEST(climate_layers_are_the_same_code_as_the_atlas) {
    enum : uint32_t { SIDE = 192u };
    layer_rig_t rig;
    REQUIRE(layer_rig_start(t, &rig));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, -4096, -4096, 4095, 4095, nullptr, nullptr);
    uint8_t *const drawn = test_alloc(t, SIDE * SIDE * 4u);
    uint8_t *const scaled = test_alloc(t, SIDE * SIDE * 4u);
    /* At 16 blocks a pixel from (-1536, 1536): world pixel column -96, a multiple of 32 less 0, so the arrows' grid falls
     * where climate_layer_draw puts it, 16 pixels in. */
    for (uint32_t layer = 0u; layer < CLIMATE_LAYER_COUNT; layer++) {
        memset(drawn, 0, SIDE * SIDE * 4u);
        memset(scaled, 0, SIDE * SIDE * 4u);
        climate_layer_draw(rig.climate, (climate_layer_t)layer, -1536, 1536, 16, SIDE, SIDE, 105u, drawn, SIDE * 4u);
        const climate_layer_frame_t frame = {
            .west_x = -1536.0f64,
            .north_y = 1536.0f64,
            .step = 16.0f64,
            .column_index = -96,
            .row_index = -96,
            .borrowed_scratch = nullptr,
            .width = SIDE,
            .height = SIDE,
            .sample = 1u,
            .parts = CLIMATE_LAYER_PARTS_ALL,
        };
        climate_layer_draw_scaled(rig.climate, (climate_layer_t)layer, &frame, 105.0f32, scaled, SIDE * 4u);
        if (!CHECK(memcmp(drawn, scaled, SIDE * SIDE * 4u) == 0)) {
            printf("  layer %u differs\n", layer);
        }
    }
    /* Sampled every 4 world pixels, a picture panned by 13 and 7 pixels is the same where the two overlap. */
    enum : uint32_t { PAN_X = 13u, PAN_Y = 7u };
    uint32_t *const grid = test_alloc(t, 2u * (SIDE / 4u + 3u) * 4u);
    for (uint32_t layer = 0u; layer < CLIMATE_LAYER_COUNT; layer++) {
        if (layer == CLIMATE_LAYER_LATITUDE) {
            continue;
        }
        climate_layer_frame_t frame = {
            .west_x = -1536.0f64,
            .north_y = 1536.0f64,
            .step = 16.0f64,
            .column_index = -96,
            .row_index = -96,
            .borrowed_scratch = grid,
            .width = SIDE,
            .height = SIDE,
            .sample = 4u,
            .parts = CLIMATE_LAYER_PART_FIELD,
        };
        climate_layer_draw_scaled(rig.climate, (climate_layer_t)layer, &frame, 105.0f32, drawn, SIDE * 4u);
        frame.west_x += 16.0f64 * PAN_X;
        frame.north_y -= 16.0f64 * PAN_Y;
        frame.column_index += PAN_X;
        frame.row_index += PAN_Y;
        climate_layer_draw_scaled(rig.climate, (climate_layer_t)layer, &frame, 105.0f32, scaled, SIDE * 4u);
        uint64_t wrong = 0u;
        /* The pressure layer's lines look north and west, so its first row and column of the second differ. */
        for (uint32_t y = PAN_Y + 1u; y < SIDE; y++) {
            for (uint32_t x = PAN_X + 1u; x < SIDE; x++) {
                wrong += layer_pixel(drawn, SIDE * 4u, x, y) == layer_pixel(scaled, SIDE * 4u, x - PAN_X, y - PAN_Y)
                             ? 0u
                             : 1u;
            }
        }
        if (!CHECK_EQ_U64(wrong, 0u)) {
            printf("  layer %u: %" PRIu64 " pixels differ after the pan\n", layer, wrong);
        }
    }
    layer_rig_stop(&rig);
    terrain_deinit(&terrain);
}

TEST_MAIN()
