/* bench_climate_census: plan 004 phase 07's census over 100 km^2 of generated land. See README.md. */
#include "climate/index.h"
#include "climate/weather/index.h"
#include "game/census/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    BENCH_SIDE = 10u, /* generated tiles on a side: 100 km^2 */
    BENCH_RUNS = 5u,  /* censuses timed, after the first */
    BENCH_STAGES = 6u,
};
/* The budget: plan 004 phase 07, a census of 100 km^2 on 16 workers. */
static const float64_t BENCH_TARGET_MS = 250.0f64;
static const char *const BENCH_STAGE_NAMES[BENCH_STAGES] = {"start", "prepare", "blocks", "flow", "outlook", "file"};

/* The next piece of the bench's one region, on its own cache lines. */
static void *bench_place(uint8_t *base, uint64_t *offset, uint64_t bytes) {
    void *const at = base + *offset;
    *offset += (bytes + 63u) / 64u * 64u;
    return at;
}

/* One census, from the trigger to idle: its time, and each stage's as the sim saw it. */
static float64_t bench_week(game_census_t *census, uint64_t week, float64_t stage_ms[BENCH_STAGES]) {
    memset(stage_ms, 0, BENCH_STAGES * sizeof(float64_t));
    const int64_t started = time_now_ns();
    int64_t since = started;
    uint8_t stage = census->stage;
    game_census_week(census, week, false);
    while (!game_census_idle(census)) {
        game_census_step(census, 0.0f64, 0.0f64, 2048u);
        const int64_t now = time_now_ns();
        if (census->stage != stage) {
            stage_ms[stage < BENCH_STAGES ? stage : 0u] += time_ns_to_ms(now - since);
            since = now;
            stage = census->stage;
        }
        time_sleep_ns(20000);
    }
    stage_ms[stage < BENCH_STAGES ? stage : 0u] += time_ns_to_ms(time_now_ns() - since);
    return time_ns_to_ms(time_now_ns() - started);
}

/* What the blocks' jobs spent, summed over the workers, for the census just run: each plans its blocks afresh. */
static void bench_report(const game_census_t *census, const char *what, float64_t ms,
                         const float64_t stage_ms[BENCH_STAGES]) {
    uint64_t lists = 0u, cells = 0u, spinup = 0u, reads = 0u, noise = 0u, days = 0u;
    for (uint32_t j = 0u; j < census->job_count; j++) {
        const climate_week_block_t *const block = &census->borrowed_jobs[j].block;
        lists += block->ns_lists;
        cells += block->ns_cells;
        spinup += block->ns_spinup;
        reads += block->ns_reads;
        noise += block->ns_noise;
        days += block->ns_days;
    }
    printf("%s: %.1f ms (", what, (libc_double_t)ms);
    for (uint32_t s = 1u; s < BENCH_STAGES; s++) {
        printf("%s%s %.1f", s == 1u ? "" : ", ", BENCH_STAGE_NAMES[s], (libc_double_t)stage_ms[s]);
    }
    printf(
        "); the jobs: day lists %.0f, spin-up %.0f, normals and wind %.0f, noise %.0f, days %.0f, all but lists %.0f "
        "ms\n",
        (libc_double_t)time_ns_to_ms((int64_t)lists), (libc_double_t)time_ns_to_ms((int64_t)spinup),
        (libc_double_t)time_ns_to_ms((int64_t)reads), (libc_double_t)time_ns_to_ms((int64_t)noise),
        (libc_double_t)time_ns_to_ms((int64_t)days), (libc_double_t)time_ns_to_ms((int64_t)cells));
}

static int32_t bench_compare(const void *a, const void *b) {
    const float64_t x = *(const float64_t *)a, y = *(const float64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    heap_region_t region;
    if (!heap_reserve(GAME_CENSUS_ARENA_BYTES + (2u << 20), &region)) {
        return 2;
    }
    memset(region.owned_base, 0, 1u << 20);
    uint64_t offset = 0u;
    thread_topology_t *const topology = bench_place(region.owned_base, &offset, sizeof(thread_topology_t));
    job_system_t *const job = bench_place(region.owned_base, &offset, sizeof(job_system_t));
    climate_t *const climate = bench_place(region.owned_base, &offset, sizeof(climate_t));
    terrain_t *const terrain = bench_place(region.owned_base, &offset, sizeof(terrain_t));
    game_census_t *const census = bench_place(region.owned_base, &offset, sizeof(game_census_t));
    arena_t *const arena = bench_place(region.owned_base, &offset, sizeof(arena_t));
    if (offset > (1u << 20) || !thread_topology_read(topology) || !job_init(job, topology) ||
        !terrain_init(terrain, TERRAIN_SEED_DEFAULT) || !climate_init(climate) || !climate_open(climate, terrain)) {
        fprintf(stderr, "bench_climate_census: could not start\n");
        return 2;
    }
    arena_init(arena, "bench_census", region.owned_base + (1u << 20), GAME_CENSUS_ARENA_BYTES);
    game_census_open(census, arena, climate, job, nullptr, TERRAIN_SEED_DEFAULT, 11u);
    /* 10 by 10 tiles of generated land round the world's middle: 196 kept with the margin. */
    climate_week_store_t *const store = climate_week_store(climate);
    const int32_t half = (int32_t)BENCH_SIDE / 2;
    for (int32_t y = -half * CLIMATE_TILE_BLOCKS; y < half * CLIMATE_TILE_BLOCKS; y += CLIMATE_CELL_BLOCKS) {
        for (int32_t x = -half * CLIMATE_TILE_BLOCKS; x < half * CLIMATE_TILE_BLOCKS; x += CLIMATE_CELL_BLOCKS) {
            climate_week_store_mark(store, x, y);
        }
    }
    printf("seed %016" PRIX64 ", %u workers: %u by %u generated tiles, %u kept with the margin, in %u blocks\n",
           (uint64_t)TERRAIN_SEED_DEFAULT, job->worker_count, BENCH_SIDE, BENCH_SIDE, store->tile_count,
           (store->tile_count + 3u) / 4u);
    /* The first census builds the climate tiles and starts every kept tile from its spin-up. */
    float64_t stage_ms[BENCH_STAGES];
    const float64_t first = bench_week(census, 12u, stage_ms);
    bench_report(census, "first census (climate tiles built, every cell from its spin-up)", first, stage_ms);
    float64_t runs[BENCH_RUNS];
    for (uint32_t r = 0u; r < BENCH_RUNS; r++) {
        char what[64];
        snprintf(what, sizeof(what), "census of week %u", 13u + r);
        runs[r] = bench_week(census, 13u + r, stage_ms);
        bench_report(census, what, runs[r], stage_ms);
    }
    qsort(runs, BENCH_RUNS, sizeof(runs[0]), bench_compare);
    const float64_t median = runs[BENCH_RUNS / 2u];
    printf("census of 100 km^2 (%u kept tiles), median of %u: %.1f ms (target under %.0f ms) %s; the first %.1f ms\n",
           store->tile_count, BENCH_RUNS, (libc_double_t)median, (libc_double_t)BENCH_TARGET_MS,
           median < BENCH_TARGET_MS ? "MET" : "MISSED", (libc_double_t)first);
    game_census_close(census);
    climate_close(climate, job);
    climate_deinit(climate);
    terrain_deinit(terrain);
    job_deinit(job);
    heap_release(&region);
    return median < BENCH_TARGET_MS ? 0 : 1;
}
