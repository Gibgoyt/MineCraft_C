/* ctest run_climate_cell. See README.md. Tests the cells through the Owner, run/src/climate/, on real workers. */
#include "harness/index.h"

#include "climate/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint64_t CELL_SEED = 0x1234567890ABCDEFull;
/* Pinned: an FNV-1a digest over the 16,384 cells of the 8 by 8 tiles round 0 for CELL_SEED, as run_terrain_density pins its
 * blocks. Any change to the height estimate, the sea rule, the distance transform, the ground samples or kappa moves it, on
 * purpose: say why here when it does. */
/* Plan 004 phase 03, first pin 0xE2FA8EB862075544 with the window-edge sea rule: 59 sea cells, 3361 large lake. Moved by the
 * sea-floor rule (the phase file's Issues found 17): 4055 sea cells, no large lake. */
/* Moved by plan 004 phase 04, which fills currents, swing, lag, wind and pressure (from 0x61708389BBF3789B). */
/* Moved by plan 004 phase 05, which fills t_mean, rain, snow balance, class and seasons, and sets the monsoon and
 * hurricane-coast flags (from 0x4950918E191FF004). */
static const uint64_t CELL_DIGEST_PIN = 0xB667CA61EBBB4C6Full;
/* Phase 03's own flags, what these tests check: phase 05 adds the monsoon and hurricane-coast flags beside them. */
static const uint8_t CELL_KIND_FLAGS = CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE | CLIMATE_CELL_LAKE_SMALL;

/* A job system and a climate, for one test. */
typedef struct cell_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} cell_rig_t;
static_assert(sizeof(cell_rig_t) == 24);

static void *cell_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool cell_rig_start(test_context_t *t, cell_rig_t *rig, uint32_t workers) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = cell_aligned(t, sizeof(job_system_t));
    rig->climate = cell_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, workers)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void cell_rig_stop(cell_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

/* The cell whose centre is at cell (cx, cy). */
static climate_cell_t cell_at(test_context_t *t, const climate_t *climate, int32_t cx, int32_t cy) {
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    CHECK(climate_cell_get(climate, cx * 64 + 32, cy * 64 + 32, &cell));
    return cell;
}

static void cell_terrain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                uint32_t count_x, uint32_t count_y, float32_t *heights) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights);
}

/* ---- The worked examples ------------------------------------------------------------------------------------------- */

TEST(kappa_and_distances_are_the_worked_examples) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t corner = {.kind = GROUND_CORNER};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &corner));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    /* Cell (3, 3): the nearest sea centre is (0, -1), 3 west and 4 south: D = 25, so 5 * 64 - 32 = 288 blocks. */
    const climate_cell_t inland = cell_at(t, rig.climate, 3, 3);
    CHECK_NEAR(inland.sea_distance, 288.0f32, 0.0f32);
    CHECK_NEAR(inland.sea_direction[0], -0.6f32, 1e-6f32);
    CHECK_NEAR(inland.sea_direction[1], -0.8f32, 1e-6f32);
    CHECK_NEAR(inland.kappa, 0.1747f32, 1e-4f32);
    CHECK_EQ_U64(inland.flags & CELL_KIND_FLAGS, 0u);
    CHECK_NEAR(inland.land_distance, 0.0f32, 0.0f32);
    /* Cell (0, 0) is next to the sea: D = 1, 32 blocks. */
    const climate_cell_t coast = cell_at(t, rig.climate, 0, 0);
    CHECK_NEAR(coast.sea_distance, 32.0f32, 0.0f32);
    CHECK_NEAR(coast.kappa, 0.0211f32, 1e-4f32);
    CHECK_NEAR(coast.sea_direction[1], -1.0f32, 0.0f32);
    CHECK_EQ_U64(coast.hurricane_days[0], CLIMATE_DAY_NONE);
    CHECK_EQ_U64(coast.hurricane_days[1], CLIMATE_DAY_NONE);
    climate_close(rig.climate, rig.job);

    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    const climate_cell_t far = cell_at(t, rig.climate, 7, 7);
    CHECK_NEAR(far.sea_distance, 8192.0f32, 0.0f32);
    CHECK_NEAR(far.kappa, 0.99575f32, 1e-4f32);
    CHECK_NEAR(far.sea_direction[0], 0.0f32, 0.0f32);
    CHECK_NEAR(far.sea_direction[1], 0.0f32, 0.0f32);
    cell_rig_stop(&rig);
}

TEST(a_straight_coast_gives_distance_rising_by_sixty_four_a_cell) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t coast = {.kind = GROUND_COAST_X, .coast = 0};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &coast));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    climate_prepare_tile(rig.climate, rig.job, -1, 0);
    uint64_t right = 0u;
    for (int32_t k = 0; k < 16; k++) {
        const climate_cell_t land = cell_at(t, rig.climate, k, 5);
        const climate_cell_t sea = cell_at(t, rig.climate, -1 - k, 5);
        const float32_t expected = 32.0f32 + 64.0f32 * (float32_t)k;
        right += (land.sea_distance == expected && land.sea_direction[0] == -1.0f32 &&
                  land.sea_direction[1] == 0.0f32 && (land.flags & CELL_KIND_FLAGS) == 0u)
                     ? 1u
                     : 0u;
        right +=
            (sea.land_distance == expected && sea.land_direction[0] == 1.0f32 && sea.land_direction[1] == 0.0f32 &&
             (sea.flags & CELL_KIND_FLAGS) == CLIMATE_CELL_SEA && sea.sea_distance == 0.0f32 && sea.kappa == 0.0f32)
                ? 1u
                : 0u;
    }
    CHECK_EQ_U64(right, 32u);
    cell_rig_stop(&rig);
}

TEST(flags_mark_sea_large_lakes_and_small_lakes_and_nothing_else) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    /* A sea west of 0; a lake of 27 by 28 cells, 3.1 km^2, two cells of land from it; one of 16 by 16, 1.05 km^2. */
    ground_t lakes = {.kind = GROUND_LAKES, .coast = 0, .rect_count = 2u};
    lakes.rects[0] = ground_cells(2, 2, 27, 28);
    lakes.rects[1] = ground_cells(4, -40, 16, 16);
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &lakes));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    climate_prepare_tile(rig.climate, rig.job, 0, -3);
    climate_prepare_tile(rig.climate, rig.job, -1, 0);
    const climate_cell_t large = cell_at(t, rig.climate, 5, 5);
    CHECK_EQ_U64(large.flags & CELL_KIND_FLAGS, CLIMATE_CELL_LAKE);
    CHECK(large.land_distance > 0.0f32 && large.sea_distance > 0.0f32 && large.kappa > 0.0f32);
    CHECK_EQ_U64(cell_at(t, rig.climate, 6, -35).flags & CELL_KIND_FLAGS, CLIMATE_CELL_LAKE_SMALL);
    CHECK_NEAR(cell_at(t, rig.climate, 6, -35).land_distance, 0.0f32, 0.0f32);
    CHECK_EQ_U64(cell_at(t, rig.climate, 1, 5).flags & CELL_KIND_FLAGS, 0u);
    CHECK_EQ_U64(cell_at(t, rig.climate, 0, 15).flags & CELL_KIND_FLAGS, 0u);
    CHECK_EQ_U64(cell_at(t, rig.climate, -3, 5).flags & CELL_KIND_FLAGS, CLIMATE_CELL_SEA);
    /* Lakes never lower kappa: the land cell beside the large lake is as far from the sea as its x says. */
    CHECK_NEAR(cell_at(t, rig.climate, 1, 5).sea_distance, 96.0f32, 0.0f32);
    cell_rig_stop(&rig);
}

TEST(z_ground_is_the_mean_of_sixteen_samples) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t ramp = {.kind = GROUND_RAMP_X};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &ramp));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    /* Samples at 8, 24, 40 and 56 along x. */
    CHECK_NEAR(cell_at(t, rig.climate, 0, 0).z_ground, 32.0f32, 0.0f32);
    CHECK_NEAR(cell_at(t, rig.climate, 15, 3).z_ground, 992.0f32, 0.0f32);
    cell_rig_stop(&rig);
}

TEST(latitude_is_phase_two_s_at_each_cell_centre) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    climate_prepare_area(rig.climate, rig.job, -100, 38198, 100, 38398, nullptr, nullptr);
    climate_prepare_tile(rig.climate, rig.job, -3, 18);
    uint64_t right = 0u;
    for (int32_t cy = 0; cy < 16; cy++) {
        const int32_t cell_y = 18 * 16 + cy;
        const climate_cell_t cell = cell_at(t, rig.climate, -3 * 16 + 4, cell_y);
        right += cell.lat_deg == (float32_t)climate_latitude_deg((float64_t)(cell_y * 64 + 32)) ? 1u : 0u;
    }
    CHECK_EQ_U64(right, 16u);
    /* At the pole, where no cell centre is, a read is still exactly 90: latitude is never blended. */
    climate_sample_t sample;
    REQUIRE(climate_sample_at(rig.climate, 0.0f64, 38298.0f64, &sample));
    CHECK_NEAR(sample.lat_deg, 90.0f32, 1e-3f32);
    REQUIRE(climate_sample_at(rig.climate, 17.0f64, 38251.5f64, &sample));
    CHECK_NEAR(sample.lat_deg, (float32_t)climate_latitude_deg(38251.5f64), 0.0f32);
    cell_rig_stop(&rig);
}

/* ---- The blend ----------------------------------------------------------------------------------------------------- */

TEST(the_blend_is_the_worked_example_with_the_warp_turned_off) {
    climate_cell_t cells[4];
    memset(cells, 0, sizeof(cells));
    cells[0].kappa = 0.10f32;
    cells[1].kappa = 0.30f32;
    cells[2].kappa = 0.20f32;
    cells[3].kappa = 0.60f32;
    cells[3].flags = CLIMATE_CELL_SEA;
    const climate_cell_t *const corners[4] = {&cells[0], &cells[1], &cells[2], &cells[3]};
    climate_sample_t sample;
    /* (48, 80) between centres at 32 and 96: t_x = 0.25, t_y = 0.75. */
    climate_cell_blend(corners, 0.25f32, 0.75f32, &sample);
    CHECK_NEAR(sample.kappa, 0.2625f32, 1e-6f32);
    CHECK_EQ_U64(sample.flags, 0u); /* the nearest corner is north-west */

    /* Through the Owner, with the warp off: the same blend of the real four cells. */
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t corner = {.kind = GROUND_CORNER};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &corner));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    climate_warp_set(rig.climate, false);
    const climate_cell_t real[4] = {cell_at(t, rig.climate, 0, 0), cell_at(t, rig.climate, 1, 0),
                                    cell_at(t, rig.climate, 0, 1), cell_at(t, rig.climate, 1, 1)};
    const climate_cell_t *const real_corners[4] = {&real[0], &real[1], &real[2], &real[3]};
    climate_sample_t expected;
    climate_cell_blend(real_corners, 0.25f32, 0.75f32, &expected);
    REQUIRE(climate_sample_at(rig.climate, 48.0f64, 80.0f64, &sample));
    CHECK_NEAR(sample.kappa, expected.kappa, 0.0f32);
    CHECK_NEAR(sample.sea_distance, expected.sea_distance, 0.0f32);
    CHECK(sample.kappa > real[0].kappa && sample.kappa < real[3].kappa);
    cell_rig_stop(&rig);
}

TEST(the_warp_moves_reads_by_at_most_thirty_two_blocks) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, 1u));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, CELL_SEED, ground_fn, &land));
    float64_t largest = 0.0f64;
    uint64_t outside = 0u;
    for (uint32_t i = 0u; i < 100000u; i++) {
        const float64_t x = (float64_t)(int32_t)(i * 7919u % 200000u) - 100000.0f64 + 0.25f64;
        const float64_t y = (float64_t)(int32_t)(i * 104729u % 200000u) - 100000.0f64;
        float64_t wx = 0.0f64, wy = 0.0f64;
        climate_warp_at(rig.climate, x, y, &wx, &wy);
        const float64_t dx = fabs(wx - x), dy = fabs(wy - y);
        outside += (dx > 32.0f64 || dy > 32.0f64) ? 1u : 0u;
        largest = dx > largest ? dx : largest;
        largest = dy > largest ? dy : largest;
    }
    printf("the warp's largest move: %.2f blocks\n", (libc_double_t)largest);
    CHECK_EQ_U64(outside, 0u);
    CHECK(largest > 20.0f64);
    cell_rig_stop(&rig);
}

/* ---- Determinism ---------------------------------------------------------------------------------------------------- */

static void cell_tile_copy(test_context_t *t, const climate_t *climate, int32_t tile_x, int32_t tile_y,
                           climate_cell_t *out) {
    for (int32_t j = 0; j < 16; j++) {
        for (int32_t i = 0; i < 16; i++) {
            out[j * 16 + i] = cell_at(t, climate, tile_x * 16 + i, tile_y * 16 + j);
        }
    }
}

TEST(a_tile_s_cells_are_the_same_whichever_neighbours_were_worked_out_first) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CELL_SEED));
    climate_cell_t *const alone = test_alloc(t, 256u * sizeof(climate_cell_t));
    climate_cell_t *const after = test_alloc(t, 256u * sizeof(climate_cell_t));
    climate_cell_t *const prebuilt = test_alloc(t, 256u * sizeof(climate_cell_t));

    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    cell_tile_copy(t, rig.climate, 0, 0, alone);
    climate_close(rig.climate, rig.job);

    /* The 8 neighbours first, from the north-east round, building most of the window's pages as they go. */
    REQUIRE(climate_open_ground(rig.climate, CELL_SEED, cell_terrain_ground, &terrain));
    for (int32_t k = 8; k >= 0; k--) {
        if (k != 4) {
            climate_prepare_tile(rig.climate, rig.job, k % 3 - 1, k / 3 - 1);
        }
    }
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    cell_tile_copy(t, rig.climate, 0, 0, after);
    climate_close(rig.climate, rig.job);

    /* Every page built first by page jobs on many workers, then the tiles. */
    REQUIRE(climate_open(rig.climate, &terrain));
    climate_prepare_area(rig.climate, rig.job, 0, 0, 1023, 1023, nullptr, nullptr);
    cell_tile_copy(t, rig.climate, 0, 0, prebuilt);

    CHECK_MEM_EQ(alone, after, 256u * sizeof(climate_cell_t));
    CHECK_MEM_EQ(alone, prebuilt, 256u * sizeof(climate_cell_t));
    cell_rig_stop(&rig);
    terrain_deinit(&terrain);
}

static uint64_t cell_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

/* 8 by 8 tiles, -4 to 3 each way: a read anywhere in this rectangle touches exactly those. */
static void cell_eight_by_eight(test_context_t *t, uint32_t workers, const terrain_t *terrain, climate_cell_t *out,
                                int64_t *elapsed_ns) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, workers));
    REQUIRE(climate_open(rig.climate, terrain));
    const int64_t started = time_now_ns();
    climate_prepare_area(rig.climate, rig.job, -4096 + 96, -4096 + 96, 4095 - 96, 4095 - 96, nullptr, nullptr);
    *elapsed_ns = time_now_ns() - started;
    for (int32_t ty = -4; ty < 4; ty++) {
        for (int32_t tx = -4; tx < 4; tx++) {
            cell_tile_copy(t, rig.climate, tx, ty, out + ((uint64_t)(ty + 4) * 8u + (uint64_t)(tx + 4)) * 256u);
        }
    }
    cell_rig_stop(&rig);
}

/* Cell (cx, cy), each from -64 to 63, of what cell_eight_by_eight copied out. */
static const climate_cell_t *cell_of_eight(const climate_cell_t *cells, int32_t cx, int32_t cy) {
    const uint64_t tile = (uint64_t)((cy >> 4) + 4) * 8u + (uint64_t)((cx >> 4) + 4);
    return &cells[tile * 256u + (uint64_t)(cy & 15) * 16u + (uint64_t)(cx & 15)];
}

TEST(one_and_sixteen_workers_give_the_same_bytes) {
    enum : uint64_t { EIGHT_CELLS = 64u * 256u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CELL_SEED));
    climate_cell_t *const one = test_alloc(t, EIGHT_CELLS * sizeof(climate_cell_t));
    climate_cell_t *const many = test_alloc(t, EIGHT_CELLS * sizeof(climate_cell_t));
    int64_t one_ns = 0, many_ns = 0;
    cell_eight_by_eight(t, 1u, &terrain, one, &one_ns);
    cell_eight_by_eight(t, 16u, &terrain, many, &many_ns);
    CHECK_MEM_EQ(one, many, EIGHT_CELLS * sizeof(climate_cell_t));
    const uint64_t digest = cell_digest(0xCBF29CE484222325ull, many, EIGHT_CELLS * sizeof(climate_cell_t));
    uint64_t sea = 0u, lake = 0u;
    for (uint64_t i = 0u; i < EIGHT_CELLS; i++) {
        sea += (many[i].flags & CLIMATE_CELL_SEA) != 0u ? 1u : 0u;
        lake += (many[i].flags & CLIMATE_CELL_LAKE) != 0u ? 1u : 0u;
    }
    printf("cells digest 0x%016" PRIX64 " (pinned 0x%016" PRIX64 "); %" PRIu64 " sea, %" PRIu64
           " large lake of %" PRIu64 "; 1 worker %.1f ms, 16 workers %.1f ms\n",
           digest, CELL_DIGEST_PIN, sea, lake, (uint64_t)EIGHT_CELLS, (libc_double_t)time_ns_to_ms(one_ns),
           (libc_double_t)time_ns_to_ms(many_ns));
    CHECK_EQ_U64(digest, CELL_DIGEST_PIN);
    /* No steps at tile borders: a distance to one set of sea cells moves by at most a cell, 64 blocks, between neighbours.
     * Two tiles that disagreed on what is sea would break that along their border (Issues found 16 and 17). */
    uint64_t steps = 0u, border_steps = 0u;
    for (int32_t cy = -64; cy < 64; cy++) {
        for (int32_t cx = -64; cx < 64; cx++) {
            const climate_cell_t *const here = cell_of_eight(many, cx, cy);
            const climate_cell_t *const east = cx + 1 < 64 ? cell_of_eight(many, cx + 1, cy) : here;
            const climate_cell_t *const north = cy + 1 < 64 ? cell_of_eight(many, cx, cy + 1) : here;
            const bool step_east = fabsf(here->sea_distance - east->sea_distance) > 64.0f32 + 1e-3f32;
            const bool step_north = fabsf(here->sea_distance - north->sea_distance) > 64.0f32 + 1e-3f32;
            steps += (step_east ? 1u : 0u) + (step_north ? 1u : 0u);
            border_steps +=
                (step_east && ((cx + 1) & 15) == 0 ? 1u : 0u) + (step_north && ((cy + 1) & 15) == 0 ? 1u : 0u);
        }
    }
    printf("neighbouring cells more than 64 blocks apart in sea distance: %" PRIu64 ", %" PRIu64
           " of them at tile borders\n",
           steps, border_steps);
    CHECK_EQ_U64(steps, 0u);
    terrain_deinit(&terrain);
}

/* What the loading screen does: begin, then a step a frame, never waiting for a job. The same bytes as climate_prepare_area,
 * and done only goes up, to the total: every page of the 24 by 24 round the 8 by 8 tiles, and the tiles. */
TEST(a_precompute_stepped_a_frame_at_a_time_gives_the_same_bytes_and_counts_up_to_its_total) {
    enum : uint64_t { EIGHT_CELLS = 64u * 256u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CELL_SEED));
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, 16u));
    REQUIRE(climate_open(rig.climate, &terrain));
    uint32_t done = 0u, total = 0u;
    CHECK(climate_prepare_step(rig.climate, rig.job, &done, &total)); /* nothing begun: done at once */
    CHECK_EQ_U64(total, 0u);
    climate_prepare_begin(rig.climate, rig.job, -4096 + CLIMATE_READ_REACH_BLOCKS, -4096 + CLIMATE_READ_REACH_BLOCKS,
                          4095 - CLIMATE_READ_REACH_BLOCKS, 4095 - CLIMATE_READ_REACH_BLOCKS);
    uint32_t last = 0u, steps = 0u;
    bool rising = true, within = true;
    for (;;) {
        const bool finished = climate_prepare_step(rig.climate, rig.job, &done, &total);
        rising = rising && done >= last;
        within = within && done <= total && total == 24u * 24u + 64u;
        last = done;
        if (finished) {
            break;
        }
        steps++;
        time_sleep_ns(TIME_NS_PER_MS / 4);
    }
    printf("stepped to the end in %" PRIu32 " unfinished steps; %" PRIu32 " of %" PRIu32 "\n", steps, done, total);
    CHECK(rising);
    CHECK(within);
    CHECK(steps > 0u);
    CHECK_EQ_U64(done, total);
    CHECK_EQ_U64(total, 24u * 24u + 64u);
    CHECK(climate_area_ready(rig.climate, -4096 + CLIMATE_READ_REACH_BLOCKS, -4096 + CLIMATE_READ_REACH_BLOCKS,
                             4095 - CLIMATE_READ_REACH_BLOCKS, 4095 - CLIMATE_READ_REACH_BLOCKS));
    climate_cell_t *const cells = test_alloc(t, EIGHT_CELLS * sizeof(climate_cell_t));
    for (int32_t ty = -4; ty < 4; ty++) {
        for (int32_t tx = -4; tx < 4; tx++) {
            cell_tile_copy(t, rig.climate, tx, ty, cells + ((uint64_t)(ty + 4) * 8u + (uint64_t)(tx + 4)) * 256u);
        }
    }
    CHECK_EQ_U64(cell_digest(0xCBF29CE484222325ull, cells, EIGHT_CELLS * sizeof(climate_cell_t)), CELL_DIGEST_PIN);
    CHECK(climate_prepare_step(rig.climate, rig.job, &done, &total)); /* over: done at once again */
    cell_rig_stop(&rig);
    terrain_deinit(&terrain);
}

/* ---- A real tile, checked by brute force -------------------------------------------------------------------------- */

/* The window of tile (tile_x, tile_y) worked out again without the climate's code: each cell centre one
 * terrain_height_estimate call, the sea rule redone by a fill of its own, then every cell of the tile checked against a
 * search over every sea and land cell of that window. Fails if the window is put together wrong, the rule or the
 * transform drifts from what the phase file says, or the half cell, the cap or kappa are off, on real ground. */
static void cell_brute_force_tile(test_context_t *t, const terrain_t *terrain, const climate_t *climate, int32_t tile_x,
                                  int32_t tile_y, uint64_t *out_sea, uint64_t *out_land) {
    enum : uint32_t { SIDE = (uint32_t)CLIMATE_WINDOW_CELLS, COUNT = SIDE * SIDE, TILE = 16u };
    float32_t *const heights = test_alloc(t, COUNT * sizeof(float32_t));
    uint32_t *const labels = test_alloc(t, COUNT * sizeof(uint32_t));
    uint32_t *const queue = test_alloc(t, COUNT * sizeof(uint32_t));
    uint8_t *const sea = test_alloc(t, COUNT); /* the kind of each cell, as below */
    const int64_t west = ((int64_t)tile_x - 8) * 1024 + 32;
    const int64_t south = ((int64_t)tile_y - 8) * 1024 + 32;
    for (uint32_t j = 0u; j < SIDE; j++) {
        for (uint32_t i = 0u; i < SIDE; i++) {
            heights[j * SIDE + i] =
                terrain_height_estimate(terrain, (int32_t)(west + 64 * (int64_t)i), (int32_t)(south + 64 * (int64_t)j));
        }
    }
    /* Water at or below 0; a body is sea if it has a cell at or below -30 or 8192 cells (Issues found 17). */
    memset(labels, 0, COUNT * sizeof(uint32_t));
    memset(sea, 0, COUNT);
    uint32_t label = 0u;
    for (uint32_t first = 0u; first < COUNT; first++) {
        if (heights[first] > 0.0f32 || labels[first] != 0u) {
            continue;
        }
        label++;
        uint32_t head = 0u, tail = 0u;
        queue[tail++] = first;
        labels[first] = label;
        bool deep = false;
        while (head < tail) {
            const uint32_t at = queue[head++];
            deep = deep || heights[at] <= -30.0f32;
            const int64_t x = (int64_t)(at % SIDE), y = (int64_t)(at / SIDE);
            const int64_t nx[4] = {x + 1, x - 1, x, x};
            const int64_t ny[4] = {y, y, y + 1, y - 1};
            for (uint32_t n = 0u; n < 4u; n++) {
                if (nx[n] < 0 || ny[n] < 0 || nx[n] >= (int64_t)SIDE || ny[n] >= (int64_t)SIDE) {
                    continue;
                }
                const uint32_t to = (uint32_t)(ny[n] * (int64_t)SIDE + nx[n]);
                if (heights[to] <= 0.0f32 && labels[to] == 0u) {
                    labels[to] = label;
                    queue[tail++] = to;
                }
            }
        }
        /* 1 sea, 2 a large lake (489 cells, 2 km²), 3 a small lake, which counts as land. */
        const uint8_t kind = (deep || tail >= 8192u) ? 1u : (tail >= 489u ? 2u : 3u);
        for (uint32_t k = 0u; k < tail; k++) {
            sea[queue[k]] = kind;
        }
    }
    uint64_t wrong = 0u, sea_cells = 0u, land_cells = 0u;
    for (uint32_t iy = 0u; iy < TILE; iy++) {
        for (uint32_t ix = 0u; ix < TILE; ix++) {
            const uint32_t at = (128u + iy) * SIDE + 128u + ix;
            /* The nearest cell of the other kind: sea for a non-sea cell; land (land or small lake) for a sea cell. */
            uint64_t best = UINT64_MAX;
            for (uint32_t k = 0u; k < COUNT; k++) {
                const bool target = sea[at] == 1u ? (sea[k] == 0u || sea[k] == 3u) : sea[k] == 1u;
                if (!target) {
                    continue;
                }
                const int64_t dx = (int64_t)(k % SIDE) - (int64_t)(at % SIDE);
                const int64_t dy = (int64_t)(k / SIDE) - (int64_t)(at / SIDE);
                const uint64_t d2 = (uint64_t)(dx * dx + dy * dy);
                best = d2 < best ? d2 : best;
            }
            float64_t blocks = best == UINT64_MAX ? 8192.0f64 : 64.0f64 * sqrt((float64_t)best) - 32.0f64;
            blocks = blocks < 0.0f64 ? 0.0f64 : (blocks > 8192.0f64 ? 8192.0f64 : blocks);
            climate_cell_t cell;
            memset(&cell, 0, sizeof(cell));
            const int64_t cx = (int64_t)tile_x * 16 + ix, cy = (int64_t)tile_y * 16 + iy;
            REQUIRE(climate_cell_get(climate, (int32_t)(cx * 64 + 32), (int32_t)(cy * 64 + 32), &cell));
            float32_t z_sum = 0.0f32;
            for (int64_t sy = 0; sy < 4; sy++) {
                for (int64_t sx = 0; sx < 4; sx++) {
                    z_sum += terrain_height_estimate(terrain, (int32_t)(cx * 64 + 8 + 16 * sx),
                                                     (int32_t)(cy * 64 + 8 + 16 * sy));
                }
            }
            bool ok = cell.z_ground == z_sum / 16.0f32;
            ok = ok && cell.lat_deg == (float32_t)climate_latitude_deg((float64_t)(cy * 64 + 32));
            if (sea[at] == 1u) {
                sea_cells++;
                ok = ok && (cell.flags & CLIMATE_CELL_SEA) != 0u && cell.sea_distance == 0.0f32 && cell.kappa == 0.0f32;
                ok = ok && fabs((float64_t)cell.land_distance - blocks) < 1e-3f64;
            } else {
                land_cells++;
                const float64_t kappa = 1.0f64 - exp(-blocks / 1500.0f64);
                static const uint8_t FLAGS[4] = {0u, 0u, CLIMATE_CELL_LAKE, CLIMATE_CELL_LAKE_SMALL};
                ok = ok && (cell.flags & CELL_KIND_FLAGS) == FLAGS[sea[at]];
                ok = ok && fabs((float64_t)cell.sea_distance - blocks) < 1e-3f64;
                ok = ok && fabs((float64_t)cell.kappa - kappa) < 1e-5f64;
                const float64_t length = hypot((float64_t)cell.sea_direction[0], (float64_t)cell.sea_direction[1]);
                ok = ok && (best == UINT64_MAX ? length == 0.0f64 : fabs(length - 1.0f64) < 1e-5f64);
            }
            wrong += ok ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    *out_sea += sea_cells;
    *out_land += land_cells;
}

TEST(a_real_tile_s_cells_match_a_brute_force_over_its_window) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CELL_SEED));
    REQUIRE(climate_open(rig.climate, &terrain));
    static const int32_t TILES[3][2] = {{0, 0}, {-2, 1}, {3, -4}};
    uint64_t sea = 0u, land = 0u;
    for (uint32_t k = 0u; k < 3u; k++) {
        climate_prepare_tile(rig.climate, rig.job, TILES[k][0], TILES[k][1]);
        cell_brute_force_tile(t, &terrain, rig.climate, TILES[k][0], TILES[k][1], &sea, &land);
    }
    printf("brute force over 3 real tiles: %" PRIu64 " sea and %" PRIu64 " other cells, all as the cells say\n", sea,
           land);
    /* Both kinds were met, so both distances were checked. */
    CHECK(sea > 0u);
    CHECK(land > 0u);
    cell_rig_stop(&rig);
    terrain_deinit(&terrain);
}

TEST(negative_coordinates_find_the_right_cell) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t ramp = {.kind = GROUND_RAMP_X};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &ramp));
    climate_prepare_tile(rig.climate, rig.job, -1, -1);
    climate_prepare_tile(rig.climate, rig.job, -2, -2);
    climate_cell_t cell;
    REQUIRE(climate_cell_get(rig.climate, -1, -1, &cell));
    /* x = -1 is cell 15 of tile -1, the cell from -64 to -1, centred on -32. */
    CHECK_NEAR(cell.z_ground, -32.0f32, 0.0f32);
    CHECK_NEAR(cell.lat_deg, (float32_t)climate_latitude_deg(-32.0f64), 0.0f32);
    REQUIRE(climate_cell_get(rig.climate, -1024, -1024, &cell));
    CHECK_NEAR(cell.z_ground, -992.0f32, 0.0f32);
    REQUIRE(climate_cell_get(rig.climate, -1025, -1025, &cell));
    CHECK_NEAR(cell.z_ground, -1056.0f32, 0.0f32);
    CHECK(!climate_cell_get(rig.climate, 0, -1, &cell)); /* tile (0, -1) is not built */
    cell_rig_stop(&rig);
}

TEST(a_read_is_false_until_its_tiles_are_built) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    climate_cell_t cell;
    climate_sample_t sample;
    memset(&cell, 0xAB, sizeof(cell));
    memset(&sample, 0xAB, sizeof(sample));
    climate_cell_t untouched_cell;
    climate_sample_t untouched_sample;
    memset(&untouched_cell, 0xAB, sizeof(untouched_cell));
    memset(&untouched_sample, 0xAB, sizeof(untouched_sample));
    CHECK(!climate_cell_get(rig.climate, 10, 10, &cell)); /* not even open */
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    CHECK(!climate_cell_get(rig.climate, 10, 10, &cell));
    CHECK(!climate_sample_at(rig.climate, 10.0f64, 10.0f64, &sample));
    CHECK(!climate_area_ready(rig.climate, 0, 0, 15, 15));
    climate_prepare_tile(rig.climate, rig.job, 0, 0);
    /* A read at (10, 10) reaches into tiles -1 as well. */
    CHECK(climate_cell_get(rig.climate, 10, 10, &cell));
    CHECK(!climate_sample_at(rig.climate, 10.0f64, 10.0f64, &sample));
    CHECK_MEM_EQ(&sample, &untouched_sample, sizeof(sample));
    CHECK(!climate_area_ready(rig.climate, 0, 0, 15, 15));
    CHECK(climate_sample_at(rig.climate, 500.0f64, 500.0f64, &sample));
    CHECK(climate_area_ready(rig.climate, 200, 200, 800, 800));
    (void)untouched_cell;
    cell_rig_stop(&rig);
}

/* ---- The game's wiring ---------------------------------------------------------------------------------------------- */

/* What game_columns_request does with each column the map offers: no column is asked for until climate_area_ready says
 * its 16 by 16 blocks have their tiles; the sim's climate_schedule builds them, nearest the player first, and the gate
 * then opens. */
TEST(a_column_is_not_asked_for_before_its_tiles_are_built) {
    cell_rig_t rig;
    REQUIRE(cell_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t land = {.kind = GROUND_ALL_LAND};
    REQUIRE(climate_open_ground(rig.climate, 1u, ground_fn, &land));
    const int32_t player_x = 5000, player_y = -3000;
    const int32_t chunk_x = player_x >> 4, chunk_y = player_y >> 4;
    const uint32_t radius = 4u * 16u + 2048u; /* render distance 4 */
    CHECK(!climate_area_ready(rig.climate, chunk_x * 16, chunk_y * 16, chunk_x * 16 + 15, chunk_y * 16 + 15));
    /* The first step queues the player's own tile first: it is the nearest. */
    climate_schedule(rig.climate, rig.job, player_x, player_y, radius);
    const climate_slot_t *const own =
        &rig.climate->borrowed_state
             ->borrowed_tile_slots[climate_cache_slot_index(player_x >> 10, player_y >> 10, CLIMATE_TILE_SIDE)];
    CHECK(atomic_load(&own->state) != CLIMATE_SLOT_EMPTY);
    CHECK(atomic_load(&rig.climate->borrowed_state->jobs_in_flight) <= CLIMATE_JOBS_IN_FLIGHT_MAX);
    uint32_t steps = 0u;
    const int64_t deadline = time_now_ns() + 120 * TIME_NS_PER_SECOND;
    while (!climate_area_ready(rig.climate, chunk_x * 16, chunk_y * 16, chunk_x * 16 + 15, chunk_y * 16 + 15) &&
           time_now_ns() < deadline) {
        time_sleep_ns(TIME_NS_PER_MS);
        climate_schedule(rig.climate, rig.job, player_x, player_y, radius);
        steps++;
    }
    CHECK(climate_area_ready(rig.climate, chunk_x * 16, chunk_y * 16, chunk_x * 16 + 15, chunk_y * 16 + 15));
    /* And in time, everything in the radius, never more than 4 queued at once. */
    while (!climate_area_ready(rig.climate, player_x - (int32_t)radius, player_y - (int32_t)radius,
                               player_x + (int32_t)radius, player_y + (int32_t)radius) &&
           time_now_ns() < deadline) {
        CHECK(atomic_load(&rig.climate->borrowed_state->jobs_in_flight) <= CLIMATE_JOBS_IN_FLIGHT_MAX);
        time_sleep_ns(TIME_NS_PER_MS);
        climate_schedule(rig.climate, rig.job, player_x, player_y, radius);
    }
    CHECK(climate_area_ready(rig.climate, player_x - (int32_t)radius, player_y - (int32_t)radius,
                             player_x + (int32_t)radius, player_y + (int32_t)radius));
    printf("the player's column was ready after %u steps\n", steps);
    cell_rig_stop(&rig);
}

TEST_MAIN()
