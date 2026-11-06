/* ctest run_climate_air. See README.md. The phase 04 file's tests for run/src/climate/air/ and the Owner's reads of it. */
#include "harness/index.h"

#include "climate/air/index.h"
#include "climate/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint64_t AIR_REAL_SEED = 0x1234567890ABCDEFull;

/* ---- Rigs ----------------------------------------------------------------------------------------------------------- */

/* A job system and a climate, for the tests that go through the Owner. */
typedef struct air_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} air_rig_t;
static_assert(sizeof(air_rig_t) == 24);

static void *air_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool air_rig_start(test_context_t *t, air_rig_t *rig) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = air_aligned(t, sizeof(job_system_t));
    rig->climate = air_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void air_rig_stop(air_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

/* Every tile a read in the block rectangle touches, on all workers, through the Owner. */
static void air_prepare(air_rig_t *rig, int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max) {
    climate_prepare_area(rig->climate, rig->job, x_min, y_min, x_max, y_max, nullptr, nullptr);
}

/* One tile built straight from its window on this thread, with the build's 12 days traced: what the Owner's tile job does,
 * without the cache. Memory is lent once per test and reused tile after tile. */
typedef struct air_direct_t {
    float32_t *heights;         /* 0: the window, 272 by 272 */
    uint8_t *arena_memory;      /* 8 */
    climate_air_trace_t *trace; /* 16 */
    climate_normals_t *normals; /* 24 */
    uint8_t _pad0[32];          /* 32 */
    arena_t arena;              /* 64 */
} air_direct_t;
static_assert(sizeof(air_direct_t) == 128);

enum : uint64_t { AIR_DIRECT_ARENA_BYTES = 8ull << 20 };

static air_direct_t *air_direct_start(test_context_t *t) {
    air_direct_t *const direct = air_aligned(t, sizeof(air_direct_t));
    memset(direct, 0, sizeof(*direct));
    direct->heights = air_aligned(t, (uint64_t)CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t));
    direct->arena_memory = air_aligned(t, AIR_DIRECT_ARENA_BYTES);
    direct->trace = air_aligned(t, sizeof(climate_air_trace_t));
    direct->normals = air_aligned(t, sizeof(climate_normals_t));
    climate_normals_fill(direct->normals);
    arena_init(&direct->arena, "air_test", direct->arena_memory, AIR_DIRECT_ARENA_BYTES);
    return direct;
}

static void air_direct_tile(air_direct_t *direct, climate_ground_fn_t ground, const void *context, int32_t tile_x,
                            int32_t tile_y, bool steer, climate_cell_t *cells) {
    arena_reset(&direct->arena);
    const int32_t half = CLIMATE_WINDOW_TILES / 2;
    ground(context, (tile_x - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2,
           (tile_y - half) * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2, CLIMATE_CELL_BLOCKS,
           (uint32_t)CLIMATE_WINDOW_CELLS, (uint32_t)CLIMATE_WINDOW_CELLS, direct->heights);
    climate_window_t window;
    climate_cell_build_tile(&direct->arena, ground, context, tile_x, tile_y, direct->heights, cells, &window);
    direct->trace->steer_off = steer ? 0u : 1u;
    climate_air_build_tile(direct->normals, &direct->arena, &window, cells, direct->trace);
}

static void air_terrain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                               uint32_t count_x, uint32_t count_y, float32_t *heights) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights);
}

static float32_t air_length(const float32_t v[2]) {
    return sqrtf(v[0] * v[0] + v[1] * v[1]);
}

/* A cell's centre, from its tile and index. */
static float64_t air_centre_x(int32_t tile_x, uint32_t index) {
    return (float64_t)tile_x * CLIMATE_TILE_BLOCKS + (float64_t)(index % CLIMATE_TILE_CELLS) * CLIMATE_CELL_BLOCKS +
           32.0f64;
}
static float64_t air_centre_y(int32_t tile_y, uint32_t index) {
    return (float64_t)tile_y * CLIMATE_TILE_BLOCKS + (float64_t)(index / CLIMATE_TILE_CELLS) * CLIMATE_CELL_BLOCKS +
           32.0f64;
}

/* ---- The mirror and the belts ---------------------------------------------------------------------------------------- */

TEST(the_mirror_sign_is_plus_near_the_equator_and_minus_past_the_pole) {
    CHECK(climate_air_mirror_sign(20000.0f64) == 1.0f32);
    CHECK(climate_air_mirror_sign(60000.0f64) == -1.0f32);
    CHECK(climate_air_mirror_sign(-20000.0f64) == 1.0f32);
    CHECK(climate_air_mirror_sign(-60000.0f64) == -1.0f32);
    CHECK(climate_air_mirror_sign(0.0f64) == 1.0f32);
    float32_t poleward[2];
    climate_air_poleward(60000.0f64, poleward);
    CHECK(poleward[0] == 0.0f32 && poleward[1] == -1.0f32);
    climate_air_poleward(20000.0f64, poleward);
    CHECK(poleward[1] == 1.0f32);
    climate_air_poleward(-20000.0f64, poleward);
    CHECK(poleward[1] == -1.0f32);
    /* Far out, a whole period away, the same. */
    CHECK(climate_air_mirror_sign(60000.0f64 + 10.0f64 * CLIMATE_LATITUDE_PERIOD) == -1.0f32);
}

TEST(the_belt_wind_is_the_worked_examples) {
    static const float32_t PSI[6] = {15.0f32, -15.0f32, 4.0f32, 30.0f32, 45.0f32, 75.0f32};
    static const float32_t WANT[6][2] = {
        {-5.571f32, -2.228f32}, {-5.571f32, 2.228f32}, {-2.228f32, -0.891f32},
        {0.523f32, 0.018f32},   {7.663f32, 2.299f32},  {-4.903f32, -0.981f32},
    };
    for (uint32_t i = 0u; i < 6u; i++) {
        float32_t wind[2];
        climate_air_belt_wind(PSI[i], 1.0f32, wind);
        printf("psi %5.1f: (%.4f, %.4f)\n", (libc_double_t)PSI[i], (libc_double_t)wind[0], (libc_double_t)wind[1]);
        CHECK_NEAR(wind[0], WANT[i][0], 1.0e-3f32);
        CHECK_NEAR(wind[1], WANT[i][1], 1.0e-3f32);
    }
    /* The worked example of psi: 20 degrees north is in the trades in July and near the westerlies in January. */
    CHECK_NEAR(climate_air_wind_latitude(20.0f64, 197.0f32), 8.27f32, 0.01f32);
    CHECK_NEAR(climate_air_wind_latitude(20.0f64, 15.0f32), 31.74f32, 0.01f32);
}

TEST(trades_blow_from_the_east_between_0_and_30_degrees) {
    /* (a) The belt alone: the blend into the westerlies starts at 27 degrees. */
    for (int32_t tenth = -269; tenth <= 269; tenth++) {
        float32_t wind[2];
        climate_air_belt_wind((float32_t)tenth / 10.0f32, 1.0f32, wind);
        if (!CHECK(wind[0] < 0.0f32)) {
            printf("psi %.1f: u %.4f\n", (libc_double_t)((float32_t)tenth / 10.0f32), (libc_double_t)wind[0]);
            break;
        }
    }
    /* (b) and (c): open sea from 22 south to 22 north, built by the Owner, read back from the stored waves. */
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t sea = {.kind = GROUND_OPEN_SEA};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &sea));
    const int32_t y_reach = 9400; /* 22.1 degrees */
    air_prepare(&rig, 0, -y_reach, 1023, y_reach);
    uint32_t checked_b = 0u, checked_c = 0u;
    float32_t u0_at[4] = {0.0f32, 0.0f32, 0.0f32, 0.0f32};
    static const float32_t PRINT_AT[4] = {1.0f32, 10.0f32, 20.0f32, 25.0f32};
    float32_t print_gap[4] = {1.0e9f32, 1.0e9f32, 1.0e9f32, 1.0e9f32};
    for (int32_t cy = -y_reach / 64 - 1; cy <= y_reach / 64; cy++) {
        climate_cell_t cell;
        if (!climate_cell_get(rig.climate, 32, cy * 64 + 32, &cell)) {
            continue;
        }
        const float32_t lat = fabsf(cell.lat_deg);
        if (lat <= 15.0f32) {
            for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
                CHECK(climate_harmonic_at1(&cell.wind[0], climate_harmonic_day(k)) < 0.0f32);
            }
            checked_b++;
        }
        if (lat <= 22.0f32) {
            CHECK(cell.wind[0] < 0.0f32);
            checked_c++;
        }
        for (uint32_t p = 0u; p < 4u; p++) {
            if (cell.lat_deg > 0.0f32 && fabsf(cell.lat_deg - PRINT_AT[p]) < print_gap[p]) {
                print_gap[p] = fabsf(cell.lat_deg - PRINT_AT[p]);
                u0_at[p] = cell.wind[0];
            }
        }
    }
    printf("open sea: %u cells within 15 degrees, %u within 22; u0 at about 1, 10, 20 degrees: %.2f %.2f %.2f\n",
           checked_b, checked_c, (libc_double_t)u0_at[0], (libc_double_t)u0_at[1], (libc_double_t)u0_at[2]);
    CHECK(checked_b >= 199u && checked_c >= 290u); /* one column of cells: 15 degrees is 6383 blocks */
    /* 25 degrees is past the prepared rows: worked out from the belt itself, as the open sea has no other wind. */
    float32_t u25[CLIMATE_FIT_DAYS];
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        float32_t wind[2];
        climate_air_belt_wind(climate_air_wind_latitude(25.0f64, climate_harmonic_day(k)), 1.0f32, wind);
        u25[k] = wind[0];
    }
    float32_t fit25[3];
    climate_harmonic_fit1(u25, fit25);
    printf("u0 at 25 degrees from the belt's 12 days: %.2f (the phase file expects about -4.4, -4.3, -3.6, -1.0)\n",
           (libc_double_t)fit25[0]);
    air_rig_stop(&rig);
}

TEST(westerlies_blow_from_the_west_and_polar_winds_from_the_east) {
    for (int32_t tenth = 350; tenth <= 550; tenth++) {
        float32_t north[2], south[2];
        climate_air_belt_wind((float32_t)tenth / 10.0f32, 1.0f32, north);
        climate_air_belt_wind(-(float32_t)tenth / 10.0f32, 1.0f32, south);
        CHECK(north[0] > 0.0f32 && south[0] > 0.0f32);
    }
    for (int32_t tenth = 630; tenth <= 900; tenth++) {
        float32_t north[2], south[2];
        climate_air_belt_wind((float32_t)tenth / 10.0f32, 1.0f32, north);
        climate_air_belt_wind(-(float32_t)tenth / 10.0f32, 1.0f32, south);
        CHECK(north[0] < 0.0f32 && south[0] < 0.0f32);
    }
}

TEST(the_doldrums_and_horse_latitudes_slow_the_wind) {
    float32_t wind[2];
    climate_air_belt_wind(4.0f32, 1.0f32, wind);
    CHECK_NEAR(air_length(wind), 2.400f32, 1.0e-3f32);
    CHECK_NEAR(climate_air_horse(30.0f32), 0.5f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_horse(28.0f32), 0.5f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_horse(-32.0f32), 0.5f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_horse(36.0f32), 1.0f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_doldrums(8.0f32), 1.0f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_doldrums(-5.0f32), 0.4f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_smoothstep(0.25f32), 0.15625f32, 1.0e-6f32);
    CHECK_NEAR(climate_air_smoothstep(0.5f32), 0.5f32, 1.0e-6f32);
}

TEST(the_belt_wind_has_no_jumps) {
    float32_t before[2];
    climate_air_belt_wind(-90.0f32, 1.0f32, before);
    float32_t worst = 0.0f32;
    for (int32_t hundredth = -8999; hundredth <= 9000; hundredth++) {
        float32_t wind[2];
        climate_air_belt_wind((float32_t)hundredth / 100.0f32, 1.0f32, wind);
        worst = fmaxf(worst, fmaxf(fabsf(wind[0] - before[0]), fabsf(wind[1] - before[1])));
        before[0] = wind[0];
        before[1] = wind[1];
    }
    printf("largest change between 0.01 degree steps: %.4f blocks a second\n", (libc_double_t)worst);
    CHECK(worst <= 0.1f32);
}

/* ---- Pressure wind, steering, altitude ------------------------------------------------------------------------------ */

TEST(the_rotation_turns_downhill_seventy_degrees_clockwise_in_the_north) {
    const float32_t west[2] = {-1.0f32, 0.0f32};
    float32_t out[2];
    climate_air_rotate(west, -70.0f32, out);
    CHECK_NEAR(out[0], -0.3420f32, 1.0e-3f32);
    CHECK_NEAR(out[1], 0.9397f32, 1.0e-3f32);
    climate_air_rotate(west, 70.0f32, out);
    CHECK_NEAR(out[0], -0.3420f32, 1.0e-3f32);
    CHECK_NEAR(out[1], -0.9397f32, 1.0e-3f32);
    /* The worked example: -grad = (0, 0.0078), so grad = (0, -0.0078), in the real north. */
    const float32_t gradient[2] = {0.0f32, -0.0078f32};
    climate_air_pressure_wind(gradient, climate_air_turn_deg(45.0f64, 1.0f32), out);
    CHECK_NEAR(out[0], 6.597f32, 1.0e-3f32);
    CHECK_NEAR(out[1], 2.401f32, 1.0e-3f32);
    CHECK(climate_air_turn_deg(45.0f64, 1.0f32) == -70.0f32);
    CHECK(climate_air_turn_deg(-45.0f64, 1.0f32) == 70.0f32);
    /* At y = 60,000 phi is north and the strip mirrored: +70. */
    CHECK(climate_latitude_deg(60000.0f64) > 0.0f64);
    CHECK(climate_air_turn_deg(climate_latitude_deg(60000.0f64), climate_air_mirror_sign(60000.0f64)) == 70.0f32);
    /* On the equator, no turn and no wind. */
    climate_air_pressure_wind(gradient, climate_air_turn_deg(0.0f64, 1.0f32), out);
    CHECK(out[0] == 0.0f32 && out[1] == 0.0f32);
}

/* The spin of the pressure wind round each spin island's centre, on one summer and one winter fit day, for every land cell
 * 1000 to 3500 blocks from the centre. The fit days nearest the plan's days 197 and 15: day 212.9 (k = 7), when the heat low
 * of a cell some km inland peaks (170 + l, l about 27 to 35), and day 0 (k = 0), when its cold high does. */
static void air_island_spin(test_context_t *t, air_direct_t *direct, climate_cell_t *cells, int32_t centre_y,
                            int32_t *out_positive_summer, int32_t *out_negative_summer, uint32_t *out_counted) {
    const ground_t island = {.kind = GROUND_ISLAND, .radius = 4000, .centre_x = 0, .centre_y = centre_y};
    int32_t positive[2] = {0, 0}, negative[2] = {0, 0};
    uint32_t counted = 0u;
    static const uint32_t DAYS[2] = {7u, 0u};
    for (int32_t ty = (centre_y - 3500) >> 10; ty <= (centre_y + 3500) >> 10; ty++) {
        for (int32_t tx = -4; tx <= 3; tx++) {
            air_direct_tile(direct, ground_fn, &island, tx, ty, true, cells);
            for (uint32_t i = 0u; i < (uint32_t)CLIMATE_TILE_CELL_COUNT; i++) {
                const float64_t r_u = air_centre_x(tx, i), r_v = air_centre_y(ty, i) - (float64_t)centre_y;
                const float64_t r = sqrt(r_u * r_u + r_v * r_v);
                if (cells[i].flags != 0u || r < 1000.0f64 || r > 3500.0f64) {
                    continue;
                }
                counted++;
                for (uint32_t d = 0u; d < 2u; d++) {
                    const float32_t *const w = direct->trace->pressure_wind[i][DAYS[d]];
                    const float64_t spin = r_u * (float64_t)w[1] - r_v * (float64_t)w[0];
                    positive[d] += spin > 0.0f64 ? 1 : 0;
                    negative[d] += spin < 0.0f64 ? 1 : 0;
                }
            }
        }
    }
    CHECK(counted > 1000u);
    out_positive_summer[0] = positive[0];
    out_positive_summer[1] = positive[1];
    out_negative_summer[0] = negative[0];
    out_negative_summer[1] = negative[1];
    *out_counted = counted;
}

TEST(winds_circle_a_summer_heat_low_anticlockwise_in_the_north_and_clockwise_in_the_south) {
    air_direct_t *const direct = air_direct_start(t);
    climate_cell_t *const cells = air_aligned(t, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    /* [0] is day 212.9, [1] day 0. */
    int32_t positive[2], negative[2];
    uint32_t counted = 0u;
    air_island_spin(t, direct, cells, 19149, positive, negative, &counted);
    printf("45 N island: %u cells; northern summer %d anticlockwise %d clockwise; winter %d and %d\n", counted,
           positive[0], negative[0], positive[1], negative[1]);
    CHECK_EQ_I64(positive[0], (int64_t)counted); /* a heat low in the north: anticlockwise */
    CHECK_EQ_I64(negative[1], (int64_t)counted); /* its cold high: clockwise */
    air_island_spin(t, direct, cells, -19149, positive, negative, &counted);
    printf("45 S island: %u cells; day 212.9 %d anticlockwise %d clockwise; day 0 %d and %d\n", counted, positive[0],
           negative[0], positive[1], negative[1]);
    CHECK_EQ_I64(negative[1], (int64_t)counted); /* its summer is day 0: a heat low in the south, clockwise */
    CHECK_EQ_I64(positive[0], (int64_t)counted); /* its winter's cold high: anticlockwise */
    air_island_spin(t, direct, cells, 57447, positive, negative, &counted);
    printf("mirrored 45 N island: %u cells; summer %d anticlockwise %d clockwise; winter %d and %d\n", counted,
           positive[0], negative[0], positive[1], negative[1]);
    CHECK_EQ_I64(negative[0], (int64_t)counted); /* the mirror image of the real north's heat low */
    CHECK_EQ_I64(positive[1], (int64_t)counted);
}

TEST(steering_never_makes_wind_faster) {
    terrain_rng_t rng = terrain_rng_start(terrain_rng_derive(7u, terrain_rng_salt("tests/climate/air/steer")));
    float32_t worst = 0.0f32;
    for (uint32_t i = 0u; i < 1000000u; i++) {
        const float32_t wind[2] = {60.0f32 * terrain_rng_next_f32(&rng) - 30.0f32,
                                   60.0f32 * terrain_rng_next_f32(&rng) - 30.0f32};
        const float32_t length = 3.0f32 * terrain_rng_next_f32(&rng);
        const float32_t angle = 6.2831853f32 * terrain_rng_next_f32(&rng);
        const float32_t slope[2] = {length * cosf(angle), length * sinf(angle)};
        float32_t out[2];
        climate_air_steer(wind, slope, out);
        const float32_t before = air_length(wind), after = air_length(out);
        worst = fmaxf(worst, after - before);
        if (!CHECK(after <= before * (1.0f32 + 1.0e-6f32))) {
            break;
        }
    }
    printf("1,000,000 random winds: steering added at most %.3g blocks a second\n", (libc_double_t)worst);
    const float32_t east[2] = {8.0f32, 0.0f32};
    const float32_t ridge[2] = {0.8f32 * 0.70710678f32, 0.8f32 * 0.70710678f32};
    float32_t out[2];
    climate_air_steer(east, ridge, out);
    CHECK_NEAR(out[0], 5.760f32, 1.0e-3f32);
    CHECK_NEAR(out[1], -2.240f32, 1.0e-3f32);
    const float32_t along[2] = {-3.0f32, 3.0f32};
    climate_air_steer(along, ridge, out);
    CHECK_NEAR(out[0], -3.0f32, 1.0e-6f32);
    CHECK_NEAR(out[1], 3.0f32, 1.0e-6f32);
    const float32_t flat[2] = {0.0f32, 0.0f32};
    climate_air_steer(east, flat, out);
    CHECK(out[0] == 8.0f32 && out[1] == 0.0f32);

    /* On the ridge's real cells: steered never faster than unsteered, both with altitude, on every fit day. */
    air_direct_t *const direct = air_direct_start(t);
    climate_cell_t *const cells = air_aligned(t, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    float32_t *const unsteered = air_aligned(t, sizeof(direct->trace->wind));
    const ground_t ground = {.kind = GROUND_RIDGE};
    uint32_t slowed = 0u, compared = 0u;
    for (int32_t tx = 9; tx <= 10; tx++) {
        air_direct_tile(direct, ground_fn, &ground, tx, 5, false, cells);
        memcpy(unsteered, direct->trace->wind, sizeof(direct->trace->wind));
        air_direct_tile(direct, ground_fn, &ground, tx, 5, true, cells);
        for (uint32_t i = 0u; i < (uint32_t)CLIMATE_TILE_CELL_COUNT; i++) {
            for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
                const float32_t *const off = &unsteered[(i * CLIMATE_FIT_DAYS + k) * 2u];
                const float32_t on = air_length(direct->trace->wind[i][k]);
                CHECK(on <= air_length(off) * (1.0f32 + 1.0e-6f32));
                slowed += on < air_length(off) - 0.01f32 ? 1u : 0u;
                compared++;
            }
        }
    }
    printf("ridge: %u of %u cell days slowed by steering, none sped up\n", slowed, compared);
    CHECK(slowed > 0u);
}

TEST(altitude_makes_peaks_windier_up_to_two_and_a_half_times) {
    static const float32_t Z[5] = {0.0f32, 400.0f32, 1000.0f32, 1200.0f32, 1500.0f32};
    static const float32_t WANT[5] = {1.0f32, 1.5f32, 2.25f32, 2.5f32, 2.5f32};
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK_NEAR(climate_air_altitude_factor(Z[i]), WANT[i], 1.0e-6f32);
    }
    CHECK_NEAR(climate_air_altitude_factor(-60.0f32), 1.0f32, 1.0e-6f32);
    /* A sea cell is 1 whatever its ground says: open sea's wind is the belt's alone. */
    air_direct_t *const direct = air_direct_start(t);
    climate_cell_t *const cells = air_aligned(t, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    const ground_t sea = {.kind = GROUND_OPEN_SEA};
    air_direct_tile(direct, ground_fn, &sea, 0, 3, true, cells);
    for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
        CHECK(direct->trace->wind[17][k][0] == direct->trace->belt[17][k][0]);
        CHECK(direct->trace->wind[17][k][1] == direct->trace->belt[17][k][1]);
    }
}

/* ---- Currents ------------------------------------------------------------------------------------------------------- */

/* What the sea cells within 600 blocks of a continent's land say, by coast side. Only cells whose own latitude is in
 * [lat_min, lat_max) are counted: the master plan's table is by the sea cell's latitude, and 600 blocks offshore can be past
 * a band's line (Issues found 7). */
typedef struct air_coasts_t {
    uint32_t count[3];         /* 0: west coast, east coast, north or south */
    uint32_t kinds[3][3];      /* 12: [side][kind] */
    uint32_t flow_north[3][3]; /* 48: [side][kind]: flow v > 0 */
    uint32_t strength_ok[3];   /* 84: strength as the formula, weight 1 west and east, 0.5 elsewhere where warm-weak */
    uint32_t _pad0;            /* 96 */
} air_coasts_t;
static_assert(sizeof(air_coasts_t) == 100);

static void air_coasts(test_context_t *t, air_rig_t *rig, const ground_t *ground, float32_t lat_min, float32_t lat_max,
                       air_coasts_t *out) {
    memset(out, 0, sizeof(*out));
    const ground_rect_t *const land = &ground->rects[0];
    const int32_t x0 = land->x_min - 700, x1 = land->x_max + 700, y0 = land->y_min - 700, y1 = land->y_max + 700;
    REQUIRE(climate_open_ground(rig->climate, 1u, ground_fn, ground));
    air_prepare(rig, x0, y0, x1, y1);
    for (int32_t cy = y0 >> 6; cy <= y1 >> 6; cy++) {
        for (int32_t cx = x0 >> 6; cx <= x1 >> 6; cx++) {
            climate_cell_t cell;
            if (!climate_cell_get(rig->climate, cx * 64 + 32, cy * 64 + 32, &cell) ||
                (cell.flags & CLIMATE_CELL_SEA) == 0u || cell.land_distance > 600.0f32 ||
                fabsf(cell.lat_deg) < lat_min || fabsf(cell.lat_deg) >= lat_max) {
                continue;
            }
            const float32_t n_x = cell.land_direction[0];
            const uint32_t side = n_x > 0.5f32 ? 0u : (n_x < -0.5f32 ? 1u : 2u);
            out->count[side]++;
            out->kinds[side][cell.current]++;
            float32_t flow[2];
            climate_current_flow(&cell, (float32_t)(cy * 64 + 32), flow);
            out->flow_north[side][cell.current] += flow[1] > 0.0f32 ? 1u : 0u;
            float32_t weight = 0.0f32;
            (void)climate_air_current_kind(cell.lat_deg, n_x, &weight);
            const uint8_t want =
                cell.current == CLIMATE_CURRENT_NONE ? 0u : climate_air_sea_strength(cell.land_distance, weight);
            out->strength_ok[side] += cell.current_strength == want ? 1u : 0u;
        }
    }
    climate_close(rig->climate, rig->job);
}

static void air_coasts_print(const char *name, const air_coasts_t *c) {
    static const char *const SIDES[3] = {"west", "east", "north/south"};
    for (uint32_t s = 0u; s < 3u; s++) {
        printf("%s %s coast: %u cells: %u none, %u warm, %u cold; flowing north: %u warm, %u cold\n", name, SIDES[s],
               c->count[s], c->kinds[s][0], c->kinds[s][1], c->kinds[s][2], c->flow_north[s][1], c->flow_north[s][2]);
    }
}

TEST(on_a_continent_at_30_south_the_west_coast_is_cold_and_the_east_coast_warm) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t ground = ground_continent(-2048, -14766, 2048, -10766);
    air_coasts_t c;
    air_coasts(t, &rig, &ground, 15.0f32, 45.0f32, &c);
    air_coasts_print("30 S", &c);
    CHECK(c.count[0] >= 30u && c.count[1] >= 30u && c.count[2] >= 30u);
    CHECK_EQ_U64(c.kinds[0][CLIMATE_CURRENT_COLD], c.count[0]);
    CHECK_EQ_U64(c.kinds[1][CLIMATE_CURRENT_WARM], c.count[1]);
    CHECK_EQ_U64(c.kinds[2][CLIMATE_CURRENT_NONE], c.count[2]);
    /* Toward the equator, which is north here; warm toward the pole, south. */
    CHECK_EQ_U64(c.flow_north[0][CLIMATE_CURRENT_COLD], c.count[0]);
    CHECK_EQ_U64(c.flow_north[1][CLIMATE_CURRENT_WARM], 0u);
    for (uint32_t s = 0u; s < 3u; s++) {
        CHECK_EQ_U64(c.strength_ok[s], c.count[s]);
    }
    air_rig_stop(&rig);
}

TEST(on_a_continent_at_50_north_the_west_coast_is_warm_and_the_east_coast_cold) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t real = ground_continent(-2048, 19277, 2048, 23277);
    air_coasts_t c;
    air_coasts(t, &rig, &real, 45.0f32, 65.0f32, &c);
    air_coasts_print("50 N", &c);
    CHECK(c.count[0] >= 30u && c.count[1] >= 30u && c.count[2] >= 30u);
    CHECK_EQ_U64(c.kinds[0][CLIMATE_CURRENT_WARM], c.count[0]);
    CHECK_EQ_U64(c.kinds[1][CLIMATE_CURRENT_COLD], c.count[1]);
    CHECK_EQ_U64(c.kinds[2][CLIMATE_CURRENT_NONE], c.count[2]);
    CHECK_EQ_U64(c.flow_north[0][CLIMATE_CURRENT_WARM], c.count[0]); /* toward the pole: north */
    CHECK_EQ_U64(c.flow_north[1][CLIMATE_CURRENT_COLD], 0u);         /* toward the equator: south */
    /* Mirrored: the same kinds, but the pole is at -y now. */
    const ground_t mirrored = ground_continent(-2048, 53319, 2048, 57319);
    air_coasts(t, &rig, &mirrored, 45.0f32, 65.0f32, &c);
    air_coasts_print("mirrored 50 N", &c);
    CHECK(c.count[0] >= 30u && c.count[1] >= 30u && c.count[2] >= 30u);
    CHECK_EQ_U64(c.kinds[0][CLIMATE_CURRENT_WARM], c.count[0]);
    CHECK_EQ_U64(c.kinds[1][CLIMATE_CURRENT_COLD], c.count[1]);
    CHECK_EQ_U64(c.kinds[2][CLIMATE_CURRENT_NONE], c.count[2]);
    CHECK_EQ_U64(c.flow_north[0][CLIMATE_CURRENT_WARM], 0u);
    CHECK_EQ_U64(c.flow_north[1][CLIMATE_CURRENT_COLD], c.count[1]);
    air_rig_stop(&rig);
}

TEST(the_outer_bands_are_warm_near_the_equator_and_cold_near_the_poles) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t low = ground_continent(-2048, 1404, 2048, 5404);
    air_coasts_t c;
    air_coasts(t, &rig, &low, 0.0f32, 15.0f32, &c);
    air_coasts_print("8 N", &c);
    CHECK(c.count[0] >= 30u && c.count[1] >= 30u && c.count[2] >= 30u);
    for (uint32_t s = 0u; s < 3u; s++) {
        CHECK_EQ_U64(c.kinds[s][CLIMATE_CURRENT_WARM], c.count[s]);
        CHECK_EQ_U64(c.strength_ok[s], c.count[s]); /* full on west and east, half on the north and south coasts */
    }
    const ground_t high = ground_continent(-2048, 27787, 2048, 31787);
    air_coasts(t, &rig, &high, 65.0f32, 90.1f32, &c);
    air_coasts_print("70 N", &c);
    CHECK(c.count[0] >= 30u && c.count[1] >= 30u && c.count[2] >= 30u);
    for (uint32_t s = 0u; s < 3u; s++) {
        CHECK_EQ_U64(c.kinds[s][CLIMATE_CURRENT_COLD], c.count[s]);
    }
    air_rig_stop(&rig);
    /* The weights by hand: half-strength next to land is 128. */
    CHECK_EQ_U64(climate_air_sea_strength(32.0f32, 0.5f32), 128u);
    CHECK_EQ_U64(climate_air_sea_strength(32.0f32, 1.0f32), 255u);
}

TEST(a_current_fades_over_600_blocks_offshore) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t ground = ground_continent(-2048, -14766, 2048, -10766);
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &ground));
    air_prepare(&rig, -3000, -13000, -2000, -12500);
    /* The middle row, cell centre y = -12,768; d_l = 64 sqrt(D) - 32 for D = 1, 4, 64, 100. */
    static const int32_t X[4] = {-2080, -2144, -2528, -2656};
    static const uint32_t WANT[4] = {255u, 226u, 54u, 0u};
    static const float32_t DISTANCE[4] = {32.0f32, 96.0f32, 480.0f32, 608.0f32};
    for (uint32_t i = 0u; i < 4u; i++) {
        climate_cell_t cell;
        REQUIRE(climate_cell_get(rig.climate, X[i], -12768, &cell));
        printf("x %d: land %.0f blocks, kind %u, strength %u\n", X[i], (libc_double_t)cell.land_distance, cell.current,
               cell.current_strength);
        CHECK_NEAR(cell.land_distance, DISTANCE[i], 1.0e-3f32);
        CHECK_EQ_U64(cell.current_strength, WANT[i]);
        CHECK_EQ_U64(cell.current, i < 3u ? CLIMATE_CURRENT_COLD : CLIMATE_CURRENT_NONE);
    }
    air_rig_stop(&rig);
}

TEST(land_takes_the_current_of_its_nearest_sea) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t ground = ground_continent(-2048, -14766, 2048, -10766);
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &ground));
    air_prepare(&rig, -2500, -13000, -1500, -12500);
    climate_cell_t inland, coast;
    REQUIRE(climate_cell_get(rig.climate, -1760, -12768, &inland));
    REQUIRE(climate_cell_get(rig.climate, -2016, -12768, &coast));
    printf("288 blocks in: kind %u, strength %u; next to the coast: kind %u, strength %u\n", inland.current,
           inland.current_strength, coast.current, coast.current_strength);
    CHECK_NEAR(inland.sea_distance, 288.0f32, 1.0e-3f32);
    CHECK_EQ_U64(inland.current, CLIMATE_CURRENT_COLD);
    CHECK_EQ_U64(inland.current_strength, 158u);
    CHECK_EQ_U64(coast.current, CLIMATE_CURRENT_COLD);
    CHECK_EQ_U64(coast.current_strength, 242u);
    climate_warp_set(rig.climate, false);
    climate_air_t air;
    REQUIRE(climate_air_at(rig.climate, -1760.0f64, -12768.0f64, 100.0f32, &air));
    printf("current_c there: %.3f\n", (libc_double_t)air.current_c);
    CHECK_NEAR(air.current_c, -3.10f32, 0.02f32);
    CHECK_EQ_U64(air.current, CLIMATE_CURRENT_COLD);
    /* A land cell's flow is its sea's: the west coast at 30 S runs north. */
    float32_t flow[2];
    climate_current_flow(&inland, -12768.0f32, flow);
    CHECK(flow[1] > 0.99f32);
    air_rig_stop(&rig);
}

TEST(the_sea_temperature_is_the_formula) {
    climate_t *const climate = air_aligned(t, sizeof(climate_t));
    REQUIRE(climate_init(climate));
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.flags = CLIMATE_CELL_SEA;
    cell.lat_deg = 15.0f32;
    cell.current = CLIMATE_CURRENT_WARM;
    cell.current_strength = 255u;
    const float32_t peak = climate_sea_temperature(climate, &cell, 215.0f32);
    const float32_t early = climate_sea_temperature(climate, &cell, 32.0f32);
    cell.current = CLIMATE_CURRENT_NONE;
    cell.current_strength = 0u;
    const float32_t bare = climate_sea_temperature(climate, &cell, 32.0f32);
    printf("15 N: day 215 %.3f, day 32 %.3f, no current day 32 %.3f\n", (libc_double_t)peak, (libc_double_t)early,
           (libc_double_t)bare);
    CHECK_NEAR(peak, 31.95f32, 0.01f32);
    CHECK_NEAR(early, 28.05f32, 0.01f32);
    CHECK_NEAR(bare, 24.05f32, 0.01f32);
    cell.lat_deg = -15.0f32;
    CHECK_NEAR(climate_sea_temperature(climate, &cell, 32.0f32) - 26.0f32, 1.95f32, 0.01f32);
    climate_deinit(climate);
}

TEST(the_heat_low_is_the_worked_examples) {
    climate_normals_t normals;
    climate_normals_fill(&normals);
    /* kappa 0.9, swing 12, on the day the cosine is 1. */
    const float32_t lag_09 = climate_air_lag_days(0.9f32);
    CHECK_NEAR(climate_air_heat_low(0.9f32, 12.0f32, lag_09, 170.0f32 + lag_09), -6.48f32, 1.0e-4f32);
    const float32_t kappa = climate_cell_kappa(3450.0f32);
    const float32_t swing = climate_air_swing(&normals, 45.0f64, kappa);
    const float32_t lag = climate_air_lag_days(kappa);
    const float32_t summer = climate_air_heat_low(kappa, swing, lag, 197.0f32);
    const float32_t winter = climate_air_heat_low(kappa, swing, lag, 197.0f32 + 182.5f32 - 365.0f32);
    printf("45 N, 3450 blocks: kappa %.4f, A %.3f, lag %.2f: day 197 %.4f hPa, half a year on %.4f\n",
           (libc_double_t)kappa, (libc_double_t)swing, (libc_double_t)lag, (libc_double_t)summer,
           (libc_double_t)winter);
    CHECK_NEAR(summer, -6.936f32, 0.002f32);
    CHECK_NEAR(winter, 6.936f32, 0.002f32);
    /* The south's swing is negative, so its heat low comes half a year later. */
    CHECK_NEAR(climate_air_heat_low(kappa, climate_air_swing(&normals, -45.0f64, kappa), lag, 197.0f32), 6.936f32,
               0.002f32);
    for (uint32_t d = 0u; d < 365u; d += 13u) {
        CHECK(climate_air_heat_low(0.0f32, swing, climate_air_lag_days(0.0f32), (float32_t)d) == 0.0f32);
    }
}

/* The pressure wind on any day at an inner cell of a column of tiles, from the stored pure waves of its four neighbours: the
 * build's own formula, so a day that is not a fit day can be asked. tiles[r] is tile (0, first_ty + r). */
static void air_pressure_wind_on(climate_cell_t *const tiles[3], uint32_t row, uint32_t ix, float32_t day,
                                 float32_t out[2]) {
    const uint32_t side = (uint32_t)CLIMATE_TILE_CELLS;
    const climate_cell_t *const north = &tiles[(row + 1u) / side][((row + 1u) % side) * side + ix];
    const climate_cell_t *const south = &tiles[(row - 1u) / side][((row - 1u) % side) * side + ix];
    const climate_cell_t *const east = &tiles[row / side][(row % side) * side + ix + 1u];
    const climate_cell_t *const west = &tiles[row / side][(row % side) * side + ix - 1u];
    const climate_cell_t *const here = &tiles[row / side][(row % side) * side + ix];
    const float32_t gradient[2] = {
        (climate_harmonic_at1(east->pressure, day) - climate_harmonic_at1(west->pressure, day)) / 128.0f32,
        (climate_harmonic_at1(north->pressure, day) - climate_harmonic_at1(south->pressure, day)) / 128.0f32,
    };
    climate_air_pressure_wind(gradient, climate_air_turn_deg((float64_t)here->lat_deg, 1.0f32), out);
}

TEST(a_heat_low_turns_the_summer_wind_onshore_on_a_coast_at_20_north) {
    air_direct_t *const direct = air_direct_start(t);
    const uint64_t tile_bytes = (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t);
    climate_cell_t *const tiles[3] = {air_aligned(t, tile_bytes), air_aligned(t, tile_bytes),
                                      air_aligned(t, tile_bytes)};
    const ground_t coast = {.kind = GROUND_COAST_Y, .coast = 8512};
    const uint32_t side = (uint32_t)CLIMATE_TILE_CELLS;
    const uint32_t ix = 7u;
    /* Tiles (0, 8) to (0, 10): y 8192 to 11263. The traces of tile 9's cells are kept for (c). */
    climate_air_trace_t *const trace9 = air_aligned(t, sizeof(climate_air_trace_t));
    for (uint32_t r = 0u; r < 3u; r++) {
        air_direct_tile(direct, ground_fn, &coast, 0, 8 + (int32_t)r, true, tiles[r]);
        if (r == 1u) {
            memcpy(trace9, direct->trace, sizeof(*trace9));
        }
    }
    uint32_t checked = 0u;
    float32_t at_704[2][2] = {{0.0f32, 0.0f32}, {0.0f32, 0.0f32}};
    float32_t total_704[2] = {0.0f32, 0.0f32};
    for (uint32_t row = 1u; row + 1u < 3u * side; row++) {
        const float32_t centre_y = 8192.0f32 + 64.0f32 * (float32_t)row + 32.0f32;
        const float32_t inland = centre_y - 8512.0f32;
        if (inland < 192.0f32 || inland > 2000.0f32) {
            continue;
        }
        float32_t summer[2], winter[2];
        air_pressure_wind_on(tiles, row, ix, 197.0f32, summer);
        air_pressure_wind_on(tiles, row, ix, 15.0f32, winter);
        CHECK(summer[1] > 0.0f32); /* (a) onshore, north, in summer */
        CHECK(winter[1] < 0.0f32); /* and offshore in winter */
        checked++;
        const climate_cell_t *const here = &tiles[row / side][(row % side) * side + ix];
        if (inland == 672.0f32 || inland == 736.0f32) {
            const uint32_t which = inland == 672.0f32 ? 0u : 1u;
            at_704[which][0] = summer[1];
            at_704[which][1] = winter[1];
            if (which == 0u) {
                total_704[0] = climate_harmonic_at1(&here->wind[3], 197.0f32);
                total_704[1] = climate_harmonic_at1(&here->wind[3], 15.0f32);
            }
        }
        /* (c) On a fit day, the stored wind is the belt's plus the pressure wind's, times the altitude factor: steering is
         * off on flat land. Tile 9's rows only, whose trace was kept. */
        if (row / side == 1u) {
            const uint32_t i = (row % side) * side + ix;
            const float32_t lift = climate_air_altitude_factor(here->z_ground);
            for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
                CHECK_NEAR(trace9->wind[i][k][1], lift * (trace9->belt[i][k][1] + trace9->pressure_wind[i][k][1]),
                           1.0e-5f32);
            }
        }
    }
    printf("20 N coast: %u cells checked; pressure wind's onshore part at 672 and 736 blocks inland: summer %.3f and "
           "%.3f, winter %.3f and %.3f\n",
           checked, (libc_double_t)at_704[0][0], (libc_double_t)at_704[1][0], (libc_double_t)at_704[0][1],
           (libc_double_t)at_704[1][1]);
    printf(
        "the total onshore part at 672 blocks: summer %.3f, winter %.3f (the phase file expects about -1.80 and +0.46; "
        "not a check until Ahmed decides the monsoon question)\n",
        (libc_double_t)total_704[0], (libc_double_t)total_704[1]);
    CHECK(checked >= 25u);
    /* (b) About 704 blocks inland, summer: 0.43 +- 0.05. */
    CHECK_NEAR(0.5f32 * (at_704[0][0] + at_704[1][0]), 0.43f32, 0.05f32);
}

TEST(stored_pressure_reproduces_the_twelve_days) {
    terrain_t *const terrain = air_aligned(t, sizeof(terrain_t));
    REQUIRE(terrain_init(terrain, AIR_REAL_SEED));
    air_direct_t *const direct = air_direct_start(t);
    climate_cell_t *const cells = air_aligned(t, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    air_direct_tile(direct, air_terrain_ground, terrain, 0, 0, true, cells);
    float32_t worst_pressure = 0.0f32, worst_wind = 0.0f32, worst_pressure_size = 0.0f32;
    uint32_t land = 0u;
    for (uint32_t i = 0u; i < (uint32_t)CLIMATE_TILE_CELL_COUNT; i++) {
        land += (cells[i].flags & CLIMATE_CELL_SEA) == 0u ? 1u : 0u;
        for (uint32_t k = 0u; k < CLIMATE_FIT_DAYS; k++) {
            const float32_t day = climate_harmonic_day(k);
            const float32_t p = climate_harmonic_at1(cells[i].pressure, day);
            worst_pressure = fmaxf(worst_pressure, fabsf(p - direct->trace->heat_low[i][k]));
            worst_pressure_size = fmaxf(worst_pressure_size, fabsf(direct->trace->heat_low[i][k]));
            const float32_t u = climate_harmonic_at1(&cells[i].wind[0], day);
            const float32_t v = climate_harmonic_at1(&cells[i].wind[3], day);
            worst_wind = fmaxf(worst_wind,
                               fmaxf(fabsf(u - direct->trace->wind[i][k][0]), fabsf(v - direct->trace->wind[i][k][1])));
        }
    }
    printf("real tile (0, 0), %u land cells: largest heat low %.3f hPa, its wave's largest miss %.2g hPa; the wind's "
           "largest miss on a fit day %.3f blocks a second\n",
           land, (libc_double_t)worst_pressure_size, (libc_double_t)worst_pressure, (libc_double_t)worst_wind);
    CHECK(worst_pressure <= 1.0e-4f32);
    terrain_deinit(terrain);
}

TEST(air_is_read_as_the_cells_blend) {
    air_rig_t rig;
    REQUIRE(air_rig_start(t, &rig));
    const ground_t coast = {.kind = GROUND_COAST_Y, .coast = 8512};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &coast));
    air_prepare(&rig, 0, 8192, 1023, 9215);
    climate_warp_set(rig.climate, false);
    const float64_t x = 300.0f64 + 17.25f64, y = 8600.0f64 + 5.5f64;
    const float32_t day = 201.5f32;
    /* The four centres round the point: x between 288 and 352, y between 8544 and 8608. */
    climate_cell_t corner[4];
    REQUIRE(climate_cell_get(rig.climate, 288, 8544, &corner[0]));
    REQUIRE(climate_cell_get(rig.climate, 352, 8544, &corner[1]));
    REQUIRE(climate_cell_get(rig.climate, 288, 8608, &corner[2]));
    REQUIRE(climate_cell_get(rig.climate, 352, 8608, &corner[3]));
    const float32_t t_x = (float32_t)((x - 288.0f64) / 64.0f64), t_y = (float32_t)((y - 8544.0f64) / 64.0f64);
    float32_t blended[9];
    for (uint32_t i = 0u; i < 9u; i++) {
        const float32_t *const value[4] = {
            i < 6u ? &corner[0].wind[i] : &corner[0].pressure[i - 6u],
            i < 6u ? &corner[1].wind[i] : &corner[1].pressure[i - 6u],
            i < 6u ? &corner[2].wind[i] : &corner[2].pressure[i - 6u],
            i < 6u ? &corner[3].wind[i] : &corner[3].pressure[i - 6u],
        };
        const float32_t south = *value[0] + t_x * (*value[1] - *value[0]);
        const float32_t north = *value[2] + t_x * (*value[3] - *value[2]);
        blended[i] = south + t_y * (north - south);
    }
    climate_air_t air;
    REQUIRE(climate_air_at(rig.climate, x, y, day, &air));
    CHECK_NEAR(air.wind[0], climate_harmonic_at1(&blended[0], day), 1.0e-4f32);
    CHECK_NEAR(air.wind[1], climate_harmonic_at1(&blended[3], day), 1.0e-4f32);
    CHECK_NEAR(air.pressure_heat_hpa, climate_harmonic_at1(&blended[6], day), 1.0e-4f32);
    const float32_t psi = climate_air_wind_latitude(climate_latitude_deg(y), day);
    CHECK_NEAR(air.pressure_belt_hpa, (float32_t)climate_normals_p_belt(climate_normals(rig.climate), (float64_t)psi),
               1.0e-4f32);
    CHECK_NEAR(air.pressure_hpa, air.pressure_belt_hpa + air.pressure_heat_hpa, 1.0e-4f32);
    /* The nearest corner's kind: t_x = 0.46, t_y = 0.96, so the north-west. */
    CHECK_EQ_U64(air.current, corner[2].current);
    CHECK_EQ_U64(air.current_strength, corner[2].current_strength);
    printf("at (%.2f, %.2f) on day %.1f: wind (%.3f, %.3f), pressure %.2f = belt %.2f + heat %.3f hPa\n",
           (libc_double_t)x, (libc_double_t)y, (libc_double_t)day, (libc_double_t)air.wind[0],
           (libc_double_t)air.wind[1], (libc_double_t)air.pressure_hpa, (libc_double_t)air.pressure_belt_hpa,
           (libc_double_t)air.pressure_heat_hpa);
    /* Unbuilt: false, nothing written. */
    climate_air_t untouched;
    memset(&untouched, 0x5A, sizeof(untouched));
    CHECK(!climate_air_at(rig.climate, 500000.0f64, 500000.0f64, day, &untouched));
    CHECK_EQ_U64(untouched.current, 0x5Au);
    air_rig_stop(&rig);
}

TEST_MAIN()
