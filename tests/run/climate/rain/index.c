/* ctest run_climate_rain. See README.md. The phase 05 file's tests for run/src/climate/rain/: the belts, the upwind walk,
 * moisture, the orographic factor, Ahmed's South Africa sketch and a ridge's rain shadow. */
#include "harness/index.h"

#include "climate/air/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/rain/index.h"
#include "climate/season/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void *rain_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* One tile built straight from its window on this thread, as the Owner's tile job builds it but with the test's hooks:
 * the window stays, so a test can walk from any of the tile's cells. */
typedef struct rain_direct_t {
    float32_t *heights;         /* 0: the window, 272 by 272 */
    uint8_t *arena_memory;      /* 8 */
    climate_normals_t *normals; /* 16 */
    climate_cell_t *cells;      /* 24: 256 */
    climate_window_t window;    /* 32 */
    uint8_t _pad0[40];          /* 88 */
    arena_t arena;              /* 128 */
} rain_direct_t;
static_assert(sizeof(rain_direct_t) == 192);

enum : uint64_t { RAIN_DIRECT_ARENA_BYTES = 8ull << 20 };

static rain_direct_t *rain_direct_start(test_context_t *t) {
    rain_direct_t *const direct = rain_aligned(t, sizeof(rain_direct_t));
    memset(direct, 0, sizeof(*direct));
    direct->heights = rain_aligned(t, (uint64_t)CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t));
    direct->arena_memory = rain_aligned(t, RAIN_DIRECT_ARENA_BYTES);
    direct->normals = rain_aligned(t, sizeof(climate_normals_t));
    direct->cells = rain_aligned(t, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    climate_normals_fill(direct->normals);
    arena_init(&direct->arena, "rain_test", direct->arena_memory, RAIN_DIRECT_ARENA_BYTES);
    return direct;
}

static void rain_direct_tile(rain_direct_t *direct, const ground_t *ground, int32_t tile_x, int32_t tile_y,
                             const climate_test_hooks_t *hooks) {
    arena_reset(&direct->arena);
    const int32_t half = CLIMATE_WINDOW_TILES / 2;
    ground_fn(ground, (tile_x - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2,
              (tile_y - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2, CLIMATE_CELL_BLOCKS,
              (uint32_t)CLIMATE_WINDOW_CELLS, (uint32_t)CLIMATE_WINDOW_CELLS, direct->heights, nullptr);
    climate_cell_build_tile(&direct->arena, ground_fn, ground, tile_x, tile_y, direct->heights, direct->cells,
                            &direct->window);
    climate_air_build_tile(direct->normals, &direct->arena, &direct->window, direct->cells, nullptr);
    climate_heat_build_tile(direct->normals, direct->cells);
    climate_rain_build_tile(direct->normals, hooks, &direct->window, direct->cells);
    climate_season_build_tile(direct->normals, hooks, &direct->window, direct->cells);
}

/* The tile cell holding block (x, y) of the tile last built, as window indices and the cell. */
typedef struct rain_place_t {
    uint32_t i;           /* 0 */
    uint32_t j;           /* 4 */
    climate_cell_t *cell; /* 8 */
} rain_place_t;
static_assert(sizeof(rain_place_t) == 16);

static rain_place_t rain_place(rain_direct_t *direct, int32_t x, int32_t y) {
    const int32_t ix = (x >> CLIMATE_CELL_SHIFT) - direct->window.tile_x * CLIMATE_TILE_CELLS;
    const int32_t iy = (y >> CLIMATE_CELL_SHIFT) - direct->window.tile_y * CLIMATE_TILE_CELLS;
    return (rain_place_t){
        .i = (uint32_t)(CLIMATE_MARGIN_CELLS + ix),
        .j = (uint32_t)(CLIMATE_MARGIN_CELLS + iy),
        .cell = &direct->cells[iy * CLIMATE_TILE_CELLS + ix],
    };
}

static float32_t rain_year_mm(const climate_cell_t *cell) {
    return cell->rain_mm_week[0] * 365.0f32 / 7.0f32;
}

static bool rain_within_ten_percent(float32_t value, float32_t plan) {
    return fabsf(value - plan) <= 0.1f32 * fabsf(plan);
}

/* ---- The belts --------------------------------------------------------------------------------------------------------- */

TEST(the_belt_spline_passes_through_its_knots) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    static const float64_t LAT[9] = {0.0f64, 10.0f64, 20.0f64, 30.0f64, 40.0f64, 50.0f64, 60.0f64, 75.0f64, 90.0f64};
    static const float64_t MM[9] = {2400.0f64, 1800.0f64, 700.0f64, 300.0f64, 800.0f64,
                                    1000.0f64, 800.0f64,  350.0f64, 150.0f64};
    for (uint32_t k = 0u; k < 9u; k++) {
        CHECK_NEAR(climate_normals_rain_belt(&normals, LAT[k]), MM[k], 1.0e-9f64);
        CHECK_NEAR(climate_normals_rain_belt(&normals, -LAT[k]), MM[k], 1.0e-9f64);
    }
    printf("P_belt(25) %.2f, P_belt(13.26) %.2f\n", (libc_double_t)climate_normals_rain_belt(&normals, 25.0f64),
           (libc_double_t)climate_normals_rain_belt(&normals, 13.26f64));
    CHECK_NEAR(climate_normals_rain_belt(&normals, 25.0f64), 426.7f64, 0.1f64);
    /* The phase 05 file says 1453.8 here, worked by hand; the repo's spline gives 1452.54 (Issues found in the phase
     * file): 0.09% apart. */
    CHECK_NEAR(climate_normals_rain_belt(&normals, 13.26f64), 1452.54f64, 0.05f64);
    /* The year's mean of the moving belt at 25 S, which is why the real year is not the plan's 500 (Issues found 1). */
    float64_t sum = 0.0f64;
    for (uint32_t d = 0u; d < 365u; d++) {
        sum += climate_normals_rain_belt(&normals, (float64_t)climate_rain_psi(nullptr, -25.0f32, (float32_t)d));
    }
    printf("the year's mean of P_belt at 25 S: %.1f mm\n", (libc_double_t)(sum / 365.0f64));
    CHECK_NEAR(sum / 365.0f64, 721.1f64, 2.0f64);
    CHECK_NEAR(climate_normals_rain_belt(&normals, 120.0f64), 150.0f64, 1.0e-9f64);
}

/* ---- The walk ---------------------------------------------------------------------------------------------------------- */

TEST(the_walk_counts_land_since_the_water) {
    rain_direct_t *const direct = rain_direct_start(t);
    const ground_t coast = {.kind = GROUND_COAST_X, .coast = 0};
    rain_direct_tile(direct, &coast, 0, 3, nullptr);
    climate_rain_walk_t walk;
    rain_place_t place = rain_place(direct, 992, 3100);
    climate_rain_walk(&direct->window, place.i, place.j, place.cell->z_ground, 1.0f32, 0.0f32, &walk);
    printf("x 992: water at step %u, d_up %.0f\n", walk.steps, (libc_double_t)walk.d_up_blocks);
    CHECK_EQ_U64(walk.met_water, 1u);
    CHECK_EQ_U64(walk.steps, 8u);
    CHECK_NEAR(walk.d_up_blocks, 896.0f32, 0.0f32);
    place = rain_place(direct, 32, 3100);
    climate_rain_walk(&direct->window, place.i, place.j, place.cell->z_ground, 1.0f32, 0.0f32, &walk);
    CHECK_EQ_U64(walk.steps, 1u);
    CHECK_NEAR(walk.d_up_blocks, 0.0f32, 0.0f32);
}

/* The phase 05 file asks for sea 9000 blocks upwind, met over the mask at step 71: past step 64, so never met. Its sea is
 * put at 7000 instead (Issues found in the phase file): met at step 55, past the window's 16, so with no current. */
TEST(the_walk_continues_past_the_window_over_the_mask) {
    rain_direct_t *const direct = rain_direct_start(t);
    const ground_t near = {.kind = GROUND_COAST_X, .coast = -6968};
    rain_direct_tile(direct, &near, 0, 3, nullptr);
    climate_rain_walk_t walk;
    rain_place_t place = rain_place(direct, 32, 3100);
    climate_rain_walk(&direct->window, place.i, place.j, place.cell->z_ground, 1.0f32, 0.0f32, &walk);
    printf("sea 7000 upwind: water at step %u, d_up %.0f, current %u\n", walk.steps, (libc_double_t)walk.d_up_blocks,
           walk.water_current);
    CHECK_EQ_U64(walk.met_water, 1u);
    CHECK(walk.steps == 55u || walk.steps == 56u);
    CHECK_EQ_U64(walk.water_current, CLIMATE_CURRENT_NONE);
    const ground_t far = {.kind = GROUND_COAST_X, .coast = -16000};
    rain_direct_tile(direct, &far, 0, 3, nullptr);
    place = rain_place(direct, 32, 3100);
    climate_rain_walk(&direct->window, place.i, place.j, place.cell->z_ground, 1.0f32, 0.0f32, &walk);
    CHECK_EQ_U64(walk.met_water, 0u);
    CHECK_NEAR(walk.d_up_blocks, 8192.0f32, 0.0f32);
    CHECK_NEAR(climate_rain_moisture(&walk, place.cell), 0.15f32, 1.0e-6f32);
}

TEST(moisture_is_the_worked_examples) {
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.sea_distance = 2000.0f32;
    climate_rain_walk_t walk;
    memset(&walk, 0, sizeof(walk));
    walk.water_current = CLIMATE_CURRENT_WARM;
    CHECK_NEAR(climate_rain_moisture(&walk, &cell), 1.25f32, 1.0e-6f32);
    walk.d_up_blocks = 1000.0f32;
    CHECK_NEAR(climate_rain_moisture(&walk, &cell), 0.8957f32, 1.0e-4f32);
    walk.d_up_blocks = 8192.0f32;
    walk.water_current = CLIMATE_CURRENT_NONE;
    cell.current = CLIMATE_CURRENT_COLD;
    cell.sea_distance = 500.0f32;
    CHECK_NEAR(climate_rain_moisture(&walk, &cell), 0.06f32, 1.0e-6f32);
    walk.water_current = CLIMATE_CURRENT_WARM; /* cold wins over warm */
    CHECK_NEAR(climate_rain_moisture(&walk, &cell), 0.06f32, 1.0e-6f32);
    cell.sea_distance = 900.0f32; /* past 800: the cold current no longer counts */
    CHECK_NEAR(climate_rain_moisture(&walk, &cell), 0.1875f32, 1.0e-6f32);
}

TEST(orographic_factor_is_the_worked_examples) {
    climate_rain_walk_t walk;
    memset(&walk, 0, sizeof(walk));
    walk.s_w = 0.301f32;
    walk.h_up = 100.0f32;
    CHECK_NEAR(climate_rain_orographic(&walk, 100.0f32), 2.5f32, 1.0e-6f32);
    walk.s_w = 0.0f32;
    walk.h_up = 750.0f32;
    CHECK_NEAR(climate_rain_orographic(&walk, 500.0f32), 0.375f32, 1.0e-6f32);
    walk.h_up = 1500.0f32;
    CHECK_NEAR(climate_rain_orographic(&walk, 500.0f32), 0.15f32, 1.0e-6f32);
}

/* ---- Ahmed's South Africa sketch --------------------------------------------------------------------------------------- */

enum : uint32_t { SA_PLACES = 4u };
static const int32_t SA_X[SA_PLACES] = {11936, 10912, 8992, 32}; /* east coast, escarpment, plateau, west coast */
static const char *const SA_NAMES[SA_PLACES] = {"east coast", "escarpment", "plateau", "west coast"};
static const int32_t SA_Y = -10656; /* the cell centre nearest 25 S, at 25.04 */

/* Each place's tile built with the hooks, and its walk on day 0 and its yearly rain. */
static void sa_build(test_context_t *t, rain_direct_t *direct, const climate_test_hooks_t *hooks,
                     float32_t out_rain[SA_PLACES], climate_rain_walk_t out_walk[SA_PLACES],
                     uint8_t out_class[SA_PLACES]) {
    (void)t;
    const ground_t sa = {.kind = GROUND_SOUTH_AFRICA};
    for (uint32_t p = 0u; p < SA_PLACES; p++) {
        rain_direct_tile(direct, &sa, SA_X[p] >> CLIMATE_TILE_SHIFT, SA_Y >> CLIMATE_TILE_SHIFT, hooks);
        const rain_place_t place = rain_place(direct, SA_X[p], SA_Y);
        (void)climate_rain_day_mm_week(direct->normals, hooks, &direct->window, place.i, place.j, place.cell, 0.0f32,
                                       &out_walk[p]);
        out_rain[p] = rain_year_mm(place.cell);
        out_class[p] = place.cell->climate_class;
    }
}

TEST(south_africa_with_the_plan_s_belt_gives_625_1125_69_and_30) {
    rain_direct_t *const direct = rain_direct_start(t);
    const climate_test_hooks_t hooks = {.wind = {-6.0f32, 0.0f32}, .belt_mm_year = 500.0f32, .freeze_seasons = 1u};
    float32_t rain[SA_PLACES];
    climate_rain_walk_t walk[SA_PLACES];
    uint8_t klass[SA_PLACES];
    sa_build(t, direct, &hooks, rain, walk, klass);
    for (uint32_t p = 0u; p < SA_PLACES; p++) {
        printf("%-10s: d_up %6.0f (step %2u, water current %u), h_up %6.1f, s_w %.4f; %.1f mm a year, class %u\n",
               SA_NAMES[p], (libc_double_t)walk[p].d_up_blocks, walk[p].steps, walk[p].water_current,
               (libc_double_t)walk[p].h_up, (libc_double_t)walk[p].s_w, (libc_double_t)rain[p], klass[p]);
    }
    /* The inputs first, so a wrong fixture fails loudly. */
    CHECK_NEAR(walk[0].d_up_blocks, 0.0f32, 0.0f32);
    CHECK_EQ_U64(walk[0].water_current, CLIMATE_CURRENT_WARM);
    CHECK_NEAR(walk[1].d_up_blocks, 1024.0f32, 0.0f32);
    CHECK_EQ_U64(walk[1].water_current, CLIMATE_CURRENT_WARM);
    CHECK(walk[1].s_w >= 0.3f32);
    CHECK_NEAR(walk[2].d_up_blocks, 2944.0f32, 0.0f32);
    CHECK_EQ_U64(walk[2].steps, 24u);
    CHECK_EQ_U64(walk[2].water_current, CLIMATE_CURRENT_NONE);
    CHECK_NEAR(walk[2].h_up, 750.0f32, 0.5f32);
    CHECK_NEAR(walk[2].s_w, 0.0f32, 0.0f32);
    CHECK_EQ_U64(walk[3].met_water, 0u);
    /* Then the rain, each within 10% of the master plan. */
    CHECK_NEAR(rain[0], 644.0f32, 2.0f32);
    CHECK(rain_within_ten_percent(rain[0], 625.0f32));
    CHECK_NEAR(rain[1], 1111.0f32, 5.0f32);
    CHECK(rain_within_ten_percent(rain[1], 1125.0f32));
    CHECK_NEAR(rain[2], 70.3f32, 0.5f32);
    CHECK(rain_within_ten_percent(rain[2], 69.0f32));
    CHECK_NEAR(rain[3], 4.5f32, 0.1f32); /* in the plateau's rain shadow by 6.7's own O: Issues found 1 */
    CHECK(rain[1] > rain[0] && rain[0] > rain[2] && rain[2] > rain[3]);
    CHECK_EQ_U64(klass[2], CLIMATE_CLASS_DESERT);
    CHECK_EQ_U64(klass[3], CLIMATE_CLASS_DESERT);
    /* The master plan's west coast takes O = 1. */
    const climate_test_hooks_t flat = {
        .wind = {-6.0f32, 0.0f32}, .belt_mm_year = 500.0f32, .freeze_seasons = 1u, .orographic_one = 1u};
    float32_t rain_flat[SA_PLACES];
    sa_build(t, direct, &flat, rain_flat, walk, klass);
    printf("west coast with O = 1: %.2f mm\n", (libc_double_t)rain_flat[3]);
    CHECK_NEAR(rain_flat[3], 30.0f32, 0.1f32);
}

TEST(south_africa_with_the_real_belt_frozen_scales_by_the_spline) {
    rain_direct_t *const direct = rain_direct_start(t);
    const climate_test_hooks_t plan = {.wind = {-6.0f32, 0.0f32}, .belt_mm_year = 500.0f32, .freeze_seasons = 1u};
    const climate_test_hooks_t real = {.wind = {-6.0f32, 0.0f32}, .freeze_seasons = 1u};
    float32_t with_plan[SA_PLACES], with_real[SA_PLACES];
    climate_rain_walk_t walk[SA_PLACES];
    uint8_t klass[SA_PLACES];
    sa_build(t, direct, &plan, with_plan, walk, klass);
    sa_build(t, direct, &real, with_real, walk, klass);
    for (uint32_t p = 0u; p < SA_PLACES; p++) {
        printf("%-10s: %.1f mm with the spline, %.3f of the plan's belt\n", SA_NAMES[p], (libc_double_t)with_real[p],
               (libc_double_t)(with_real[p] / with_plan[p]));
        CHECK_NEAR(with_real[p] / with_plan[p], 0.853f32, 0.01f32);
    }
}

TEST(south_africa_over_a_real_year_is_printed) {
    rain_direct_t *const direct = rain_direct_start(t);
    const ground_t sa = {.kind = GROUND_SOUTH_AFRICA};
    float32_t year[SA_PLACES];
    for (uint32_t p = 0u; p < SA_PLACES; p++) {
        rain_direct_tile(direct, &sa, SA_X[p] >> CLIMATE_TILE_SHIFT, SA_Y >> CLIMATE_TILE_SHIFT, nullptr);
        const rain_place_t place = rain_place(direct, SA_X[p], SA_Y);
        climate_season_months_t months;
        climate_season_months(place.cell, place.cell->z_ground, true, &months);
        float32_t wettest = 0.0f32;
        uint32_t wet_m = 0u, dry_m = 0u;
        for (uint32_t m = 0u; m < 12u; m++) {
            if (months.rain_mm[m] > wettest) {
                wettest = months.rain_mm[m];
                wet_m = m;
            }
            dry_m = months.rain_mm[m] < months.rain_mm[dry_m] ? m : dry_m;
        }
        float32_t w15[2], w197[2];
        climate_rain_wind(place.cell, nullptr, 15.0f32, w15);
        climate_rain_wind(place.cell, nullptr, 197.0f32, w197);
        year[p] = months.rain_mm_year;
        printf("%-10s: %.1f mm a year; wettest month %u (%.1f mm), driest %u (%.1f mm); wind day 15 (%.2f, %.2f), "
               "day 197 (%.2f, %.2f); class %u\n",
               SA_NAMES[p], (libc_double_t)year[p], wet_m + 1u, (libc_double_t)wettest, dry_m + 1u,
               (libc_double_t)months.rain_mm[dry_m], (libc_double_t)w15[0], (libc_double_t)w15[1],
               (libc_double_t)w197[0], (libc_double_t)w197[1], place.cell->climate_class);
        CHECK(isfinite(year[p]) && year[p] >= 0.0f32);
    }
    CHECK(year[1] > year[2]);
}

TEST(a_ridge_s_lee_is_drier_than_its_windward_side) {
    rain_direct_t *const direct = rain_direct_start(t);
    const ground_t ridge = {.kind = GROUND_PEAK_RIDGE};
    const climate_test_hooks_t hooks = {.wind = {-6.0f32, 0.0f32}, .freeze_seasons = 1u};
    const int32_t y = 6400; /* 15 N */
    rain_direct_tile(direct, &ridge, 10528 >> CLIMATE_TILE_SHIFT, y >> CLIMATE_TILE_SHIFT, &hooks);
    const float32_t windward = rain_year_mm(rain_place(direct, 10528, y).cell);
    rain_direct_tile(direct, &ridge, 9472 >> CLIMATE_TILE_SHIFT, y >> CLIMATE_TILE_SHIFT, &hooks);
    const float32_t lee = rain_year_mm(rain_place(direct, 9472, y).cell);
    printf("ridge at 15 N: windward %.1f mm, lee %.1f mm, %.2f times\n", (libc_double_t)windward, (libc_double_t)lee,
           (libc_double_t)(windward / lee));
    CHECK(windward >= 3.0f32 * lee);
}

/* ---- A real seed -------------------------------------------------------------------------------------------------------- */

TEST(rain_is_never_negative_when_read) {
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    job_system_t *const job = rain_aligned(t, sizeof(job_system_t));
    climate_t *const climate = rain_aligned(t, sizeof(climate_t));
    REQUIRE(thread_topology_read(topology) && job_init_count(job, topology, MC_THREADS_WORKERS));
    REQUIRE(climate_init(climate));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 2198175988018515243ull));
    REQUIRE(climate_open(climate, &terrain));
    climate_prepare_area(climate, job, -2048, -2048, 2047, 2047, nullptr, nullptr);
    uint64_t state = 0xD1B54A32D192ED03ull, negative = 0u, read = 0u;
    float32_t lowest = 1.0e9f32, highest = 0.0f32;
    for (uint32_t n = 0u; n < 10000u; n++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const float64_t x = -2000.0f64 + (float64_t)(state >> 40) / 16777216.0f64 * 4000.0f64;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const float64_t y = -2000.0f64 + (float64_t)(state >> 40) / 16777216.0f64 * 4000.0f64;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const float32_t day = (float32_t)(state >> 40) / 16777216.0f32 * 364.99f32;
        climate_normals_at_t normals;
        if (climate_normals_at(climate, x, y, nanf(""), day, &normals)) {
            read++;
            negative += normals.rain_mm_day < 0.0f32 ? 1u : 0u;
            lowest = fminf(lowest, normals.rain_mm_day);
            highest = fmaxf(highest, normals.rain_mm_day);
        }
    }
    printf("%" PRIu64 " reads: rain a day from %.3f to %.3f mm\n", read, (libc_double_t)lowest, (libc_double_t)highest);
    CHECK_EQ_U64(read, 10000u);
    CHECK_EQ_U64(negative, 0u);
    climate_close(climate, job);
    climate_deinit(climate);
    job_deinit(job);
    terrain_deinit(&terrain);
}

TEST_MAIN()
