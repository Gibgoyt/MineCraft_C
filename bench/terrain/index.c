/* bench_terrain: chunk columns a second through every stage of terrain. See README.md. */
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

enum : uint32_t {
    BENCH_CHUNK = TERRAIN_FEATURE_CHUNK,
    /* A whole column_t's voxels, exactly what the game asks for. */
    BENCH_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    BENCH_VOXELS = TERRAIN_FEATURE_CHUNK_COLUMNS * BENCH_LAYERS,
    BENCH_SIDE = 6u, /* chunks a side of what one round makes */
    BENCH_CHUNKS = BENCH_SIDE * BENCH_SIDE,
    BENCH_WORKERS = 16u,
    BENCH_ROUNDS = 3u,
    BENCH_PLACES = 9u,
    /* The plan's number, on 16 workers. A column there is a chunk's, 16 by 16: "Chunk = 16x16 column". */
    BENCH_TARGET = 1000u,
    /* The search for a place: this many columns a side, this far apart. */
    BENCH_SEARCH_SIDE = 512u,
    BENCH_SEARCH_STEP = 128u,
};
static_assert(MC_THREADS_WORKERS >= BENCH_WORKERS);

/* What goes on the ground differs most between these, and the slowest place is what counts. */
static const biome_id_t BENCH_BIOMES[BENCH_PLACES] = {
    BIOME_ID_PLAINS, BIOME_ID_FOREST,      BIOME_ID_JUNGLE,       BIOME_ID_DARK_FOREST, BIOME_ID_MANGROVE_SWAMP,
    BIOME_ID_DESERT, BIOME_ID_SNOWY_TAIGA, BIOME_ID_JAGGED_PEAKS, BIOME_ID_DEEP_OCEAN,
};

typedef struct bench_run_t {
    const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_blocks;         /* 8: one chunk per worker */
    uint8_t *borrowed_opened;          /* 16: the same */
    _Atomic uint32_t next;             /* 24 */
    _Atomic uint32_t next_worker;      /* 28 */
    int32_t origin_x;                  /* 32 */
    int32_t origin_y;                  /* 36 */
} bench_run_t;
static_assert(sizeof(bench_run_t) == 40);

static void bench_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    bench_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    for (;;) {
        const uint32_t chunk = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (chunk >= BENCH_CHUNKS) {
            return;
        }
        const terrain_density_box_t box = {
            .origin_x = run->origin_x + (int32_t)(chunk % BENCH_SIDE * BENCH_CHUNK),
            .origin_y = run->origin_y + (int32_t)(chunk / BENCH_SIDE * BENCH_CHUNK),
            .origin_z = COLUMN_Z_STORAGE_MIN,
            .count_x = BENCH_CHUNK,
            .count_y = BENCH_CHUNK,
            .count_z = BENCH_LAYERS,
        };
        terrain_generate_box(run->borrowed_terrain, &box, run->borrowed_opened + (uint64_t)me * BENCH_VOXELS,
                             run->borrowed_blocks + (uint64_t)me * BENCH_VOXELS);
    }
}

/* The best of a few rounds, in chunk columns a second: the one the least else interfered with. */
static float64_t bench_place(job_system_t *system, bench_run_t *run, uint32_t workers) {
    float64_t best = 0.0f64;
    for (uint32_t round = 0u; round < BENCH_ROUNDS; round++) {
        atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
        atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
        const int64_t started = time_now_ns();
        job_group_t group;
        job_group_init(&group);
        for (uint32_t i = 0u; i < workers; i++) {
            job_submit(system, &group, bench_job, run);
        }
        job_group_wait(system, &group);
        job_group_deinit(&group);
        const float64_t seconds = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
        const float64_t rate = (float64_t)BENCH_CHUNKS / seconds;
        best = rate > best ? rate : best;
    }
    return best;
}

/* The first column of a biome from the middle of the world out, row by row. Returns false if there is none in the search. */
static bool bench_find(const terrain_t *terrain, biome_id_t biome, int32_t *out_x, int32_t *out_y) {
    terrain_biome_column_t columns[256];
    const int32_t half = (int32_t)(BENCH_SEARCH_SIDE * BENCH_SEARCH_STEP / 2u);
    for (uint32_t row = 0u; row < BENCH_SEARCH_SIDE; row++) {
        for (uint32_t part = 0u; part < BENCH_SEARCH_SIDE / 256u; part++) {
            const terrain_fields_area_t area = {
                .origin_x = -half + (int32_t)(part * 256u * BENCH_SEARCH_STEP),
                .origin_y = -half + (int32_t)(row * BENCH_SEARCH_STEP),
                .step_x = (int32_t)BENCH_SEARCH_STEP,
                .step_y = 1,
                .count_x = 256u,
                .count_y = 1u,
            };
            terrain_biome_columns_area(terrain->borrowed_biome, &area, columns);
            for (uint32_t i = 0u; i < 256u; i++) {
                if (columns[i].biome == (uint8_t)biome) {
                    *out_x = area.origin_x + (int32_t)i * area.step_x;
                    *out_y = area.origin_y;
                    return true;
                }
            }
        }
    }
    return false;
}

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    terrain_t terrain;
    if (!terrain_init(&terrain, TERRAIN_SEED_DEFAULT)) {
        return 2;
    }
    heap_region_t memory = {0};
    const uint64_t bytes = (uint64_t)BENCH_WORKERS * BENCH_VOXELS * 3u;
    if (!heap_reserve(bytes, &memory)) {
        terrain_deinit(&terrain);
        return 2;
    }
    thread_topology_t topology;
    job_system_t system;
    if (!thread_topology_read(&topology) || !job_init(&system, &topology)) {
        heap_release(&memory);
        terrain_deinit(&terrain);
        return 2;
    }

    bench_run_t run = {
        .borrowed_terrain = &terrain,
        .borrowed_blocks = (uint16_t *)memory.owned_base,
        .borrowed_opened = (uint8_t *)memory.owned_base + (uint64_t)BENCH_WORKERS * BENCH_VOXELS * 2u,
    };
    printf("%u chunk columns of %u layers, every stage, best of %u rounds, seed 0x%" PRIX64 "\n", BENCH_CHUNKS,
           BENCH_LAYERS, BENCH_ROUNDS, (uint64_t)TERRAIN_SEED_DEFAULT);
    printf("chunk columns a second\n");
    printf("%-18s %12s %12s %8s\n", "place", "1 worker", "16 workers", "scaling");
    int32_t status = 0;
    float64_t slowest = -1.0f64;
    for (uint32_t p = 0u; p < BENCH_PLACES; p++) {
        int32_t x = 0;
        int32_t y = 0;
        if (!bench_find(&terrain, BENCH_BIOMES[p], &x, &y)) {
            printf("%-18s not found\n", biome_defs[BENCH_BIOMES[p]].name);
            status = 1;
            continue;
        }
        run.origin_x =
            (x >> TERRAIN_FEATURE_CHUNK_SHIFT) * (int32_t)BENCH_CHUNK - (int32_t)(BENCH_SIDE / 2u * BENCH_CHUNK);
        run.origin_y =
            (y >> TERRAIN_FEATURE_CHUNK_SHIFT) * (int32_t)BENCH_CHUNK - (int32_t)(BENCH_SIDE / 2u * BENCH_CHUNK);
        const float64_t one = bench_place(&system, &run, 1u);
        const float64_t many = bench_place(&system, &run, BENCH_WORKERS);
        printf("%-18s %12.0f %12.0f %7.1fx\n", biome_defs[BENCH_BIOMES[p]].name, (libc_double_t)one,
               (libc_double_t)many, (libc_double_t)(many / one));
        if (slowest < 0.0f64 || many < slowest) {
            slowest = many;
        }
        if (many < (float64_t)BENCH_TARGET) {
            status = 1;
        }
    }
    printf("slowest on 16 workers: %.0f chunk columns/s. target: %u: %s\n", (libc_double_t)slowest, BENCH_TARGET,
           status == 0 ? "every place beat it" : "NOT MET");
    job_deinit(&system);
    heap_release(&memory);
    terrain_deinit(&terrain);
    return status;
}
