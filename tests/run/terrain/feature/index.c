/* ctest run_terrain_feature. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */

enum : uint32_t {
    FEA_CHUNK = TERRAIN_FEATURE_CHUNK,
    FEA_COLUMNS = TERRAIN_FEATURE_CHUNK_COLUMNS,
    FEA_WORKERS_MANY = 16u,
    /* The search for places: this many chunks a side, this many chunks apart. */
    FEA_SEARCH_SIDE = 256u,
    FEA_SEARCH_STEP = 16u,
    FEA_PLACES_EACH = 5u,
    /* How many layers round the ground a place's box holds. */
    FEA_BELOW = 16u,
    FEA_ABOVE = 40u,
    FEA_TALL = FEA_BELOW + FEA_ABOVE,
    FEA_MAP_SIDE = 5u,
    FEA_MAP_CHUNKS = FEA_MAP_SIDE * FEA_MAP_SIDE,
    FEA_MAP_ACROSS = FEA_MAP_SIDE * FEA_CHUNK,
    /* A whole column of the world, for the worker comparison. */
    FEA_LAYERS = 1501u,
};
static const uint64_t FEA_WORLD = 0x1234567890ABCDEFull;

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 8 stage 5 brought spaghetti closer together and dried out most aquifer cells. */
/* Plan 003 phase 8 stage 6 moved the biomes' boxes to the coast and the height the terrain now has. */
static const uint64_t FEA_DIGEST_PIN = 0x83D8E5C1176914F4ull;
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
/* Plan 003 phase 8 stage 4 raised the land and made its ranges, valleys and wetlands. */
/* Plan 003 phase 8 stage 5 brought spaghetti closer together and dried out most aquifer cells. */
static const uint64_t FEA_FOREST_PIN = 0xAB5C1A83C5FB475Dull;

static uint64_t fea_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static uint64_t fea_at(const terrain_density_box_t *box, int32_t x, int32_t y, int32_t z) {
    return (uint64_t)(x - box->origin_x) +
           (uint64_t)box->count_x *
               ((uint64_t)(y - box->origin_y) + (uint64_t)box->count_y * (uint64_t)(z - box->origin_z));
}

/* The stages before this one, as the Owner runs them. */
static void fea_stages(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    const uint64_t voxels = terrain_density_box_voxels(box);
    for (uint64_t i = 0u; i < voxels; i++) {
        const uint16_t block = blocks[i];
        const bool rock =
            block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA && block != BLOCK_ID_BEDROCK;
        opened[i] = opened[i] != 0u && rock ? 1u : 0u;
    }
    (void)terrain_cave_carve_box(terrain->borrowed_cave, box, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    terrain_surface_line_caves_box(terrain->borrowed_surface, box, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
}

/* ---- Places to look ----------------------------------------------------------------------------------------------- */

typedef struct fea_place_t {
    int32_t chunk_x;  /* 0 */
    int32_t chunk_y;  /* 4 */
    int32_t ground;   /* 8 */
    uint8_t found;    /* 12 */
    uint8_t _pad0[3]; /* 13 */
} fea_place_t;
static_assert(sizeof(fea_place_t) == 16);

/* Each biome's first chunk whose middle and corners hold it, so a box round it is mostly it. */
static void fea_find_places_after(test_context_t *t, const terrain_t *terrain, fea_place_t *places, uint32_t skip) {
    memset(places, 0, BIOME_COUNT * sizeof(*places));
    uint32_t passed[BIOME_COUNT] = {0u};
    terrain_biome_column_t *const columns = test_alloc(t, FEA_SEARCH_SIDE * sizeof(*columns));
    const int32_t half = (int32_t)(FEA_SEARCH_SIDE * FEA_SEARCH_STEP / 2u);
    for (uint32_t row = 0u; row < FEA_SEARCH_SIDE; row++) {
        const int32_t chunk_y = (int32_t)(row * FEA_SEARCH_STEP) - half;
        const terrain_climate_area_t area = {
            .origin_x = -half * (int32_t)FEA_CHUNK + (int32_t)FEA_CHUNK / 2,
            .origin_y = chunk_y * (int32_t)FEA_CHUNK + (int32_t)FEA_CHUNK / 2,
            .step_x = (int32_t)(FEA_SEARCH_STEP * FEA_CHUNK),
            .step_y = 1,
            .count_x = FEA_SEARCH_SIDE,
            .count_y = 1u,
        };
        terrain_biome_columns_area(terrain->borrowed_biome, &area, columns);
        for (uint32_t i = 0u; i < FEA_SEARCH_SIDE; i++) {
            fea_place_t *const place = &places[columns[i].biome];
            if (place->found != 0u) {
                continue;
            }
            if (passed[columns[i].biome] < skip) {
                passed[columns[i].biome]++;
                continue;
            }
            *place = (fea_place_t){
                .chunk_x = (int32_t)(i * FEA_SEARCH_STEP) - half,
                .chunk_y = chunk_y,
                .ground = (int32_t)columns[i].ground,
                .found = 1u,
            };
        }
    }
}

static void fea_find_places(test_context_t *t, const terrain_t *terrain, fea_place_t *places) {
    fea_find_places_after(t, terrain, places, 0u);
}

/* A box of whole chunks round a place, from under its ground to over anything on it. */
static terrain_density_box_t fea_map_box(const fea_place_t *place) {
    const int32_t half = (int32_t)(FEA_MAP_SIDE / 2u);
    int32_t ground = place->ground;
    ground = ground > MC_WORLD_Z_SEA ? ground : MC_WORLD_Z_SEA;
    return (terrain_density_box_t){
        .origin_x = (place->chunk_x - half) * (int32_t)FEA_CHUNK,
        .origin_y = (place->chunk_y - half) * (int32_t)FEA_CHUNK,
        .origin_z = ground - 120,
        .count_x = FEA_MAP_ACROSS,
        .count_y = FEA_MAP_ACROSS,
        .count_z = 220u,
    };
}

/* ---- Any split, any order ----------------------------------------------------------------------------------------- */

/* Cuts a box where it is told, generates the parts in the order given, and they must come to the whole. */
TEST(any_split_of_a_box_and_any_order_of_chunks_give_the_same_bytes) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const biome_id_t biomes[6] = {BIOME_ID_JUNGLE,      BIOME_ID_DARK_FOREST, BIOME_ID_MANGROVE_SWAMP,
                                  BIOME_ID_SNOWY_TAIGA, BIOME_ID_DESERT,      BIOME_ID_OCEAN};
    for (uint32_t b = 0u; b < 6u; b++) {
        const fea_place_t *const place = &places[biomes[b]];
        REQUIRE(place->found != 0u);
        const terrain_density_box_t box = {
            .origin_x = (place->chunk_x - 1) * (int32_t)FEA_CHUNK + 5,
            .origin_y = (place->chunk_y - 1) * (int32_t)FEA_CHUNK + 11,
            .origin_z = (place->ground > 0 ? place->ground : 0) - 40,
            .count_x = 2u * FEA_CHUNK + 7u,
            .count_y = 2u * FEA_CHUNK + 3u,
            .count_z = 90u,
        };
        const uint64_t voxels = terrain_density_box_voxels(&box);
        uint16_t *const whole = test_alloc(t, voxels * sizeof(*whole));
        uint8_t *const opened = test_alloc(t, voxels);
        terrain_generate_box(&terrain, &box, opened, whole);

        /* Cut on places that are not chunk lines, on all three axes, with the last parts first. */
        const uint32_t cut_x[3] = {0u, 3u, 21u};
        const uint32_t cut_y[3] = {0u, 9u, 20u};
        const uint32_t cut_z[3] = {0u, 17u, 41u};
        for (int32_t iz = 2; iz >= 0; iz--) {
            for (int32_t iy = 2; iy >= 0; iy--) {
                for (int32_t ix = 2; ix >= 0; ix--) {
                    const uint32_t end_x = ix == 2 ? box.count_x : cut_x[ix + 1];
                    const uint32_t end_y = iy == 2 ? box.count_y : cut_y[iy + 1];
                    const uint32_t end_z = iz == 2 ? box.count_z : cut_z[iz + 1];
                    const terrain_density_box_t part = {
                        .origin_x = box.origin_x + (int32_t)cut_x[ix],
                        .origin_y = box.origin_y + (int32_t)cut_y[iy],
                        .origin_z = box.origin_z + (int32_t)cut_z[iz],
                        .count_x = end_x - cut_x[ix],
                        .count_y = end_y - cut_y[iy],
                        .count_z = end_z - cut_z[iz],
                    };
                    const uint64_t part_voxels = terrain_density_box_voxels(&part);
                    uint16_t *const piece = test_alloc(t, part_voxels * sizeof(*piece));
                    uint8_t *const part_opened = test_alloc(t, part_voxels);
                    terrain_generate_box(&terrain, &part, part_opened, piece);
                    uint64_t wrong = 0u;
                    for (uint32_t pz = 0u; pz < part.count_z; pz++) {
                        for (uint32_t py = 0u; py < part.count_y; py++) {
                            for (uint32_t px = 0u; px < part.count_x; px++) {
                                const uint64_t at_part =
                                    px + (uint64_t)part.count_x * (py + (uint64_t)part.count_y * pz);
                                const uint64_t at_whole =
                                    fea_at(&box, part.origin_x + (int32_t)px, part.origin_y + (int32_t)py,
                                           part.origin_z + (int32_t)pz);
                                wrong += piece[at_part] != whole[at_whole] ? 1u : 0u;
                            }
                        }
                    }
                    CHECK_EQ_U64(wrong, 0u);
                }
            }
        }
    }
    terrain_deinit(&terrain);
}

/* ---- Workers ------------------------------------------------------------------------------------------------------ */

typedef struct fea_run_t {
    const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_blocks;         /* 8: FEA_MAP_CHUNKS chunks, FEA_LAYERS high */
    uint8_t *borrowed_opened;          /* 16: as many bytes per worker */
    const uint32_t *borrowed_order;    /* 24 */
    _Atomic uint32_t next;             /* 32 */
    _Atomic uint32_t next_worker;      /* 36 */
    int32_t origin_x;                  /* 40 */
    int32_t origin_y;                  /* 44 */
} fea_run_t;
static_assert(sizeof(fea_run_t) == 48);

static const uint64_t FEA_CHUNK_VOXELS = (uint64_t)FEA_COLUMNS * FEA_LAYERS;

static void fea_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    fea_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * FEA_CHUNK_VOXELS;
    for (;;) {
        const uint32_t turn = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (turn >= FEA_MAP_CHUNKS) {
            return;
        }
        const uint32_t chunk = run->borrowed_order[turn];
        const terrain_density_box_t box = {
            .origin_x = run->origin_x + (int32_t)(chunk % FEA_MAP_SIDE * FEA_CHUNK),
            .origin_y = run->origin_y + (int32_t)(chunk / FEA_MAP_SIDE * FEA_CHUNK),
            .origin_z = MC_WORLD_Z_MIN,
            .count_x = FEA_CHUNK,
            .count_y = FEA_CHUNK,
            .count_z = FEA_LAYERS,
        };
        terrain_generate_box(run->borrowed_terrain, &box, opened,
                             run->borrowed_blocks + (uint64_t)chunk * FEA_CHUNK_VOXELS);
    }
}

static uint64_t fea_run_all(job_system_t *system, fea_run_t *run, uint32_t workers, const uint32_t *order) {
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
    run->borrowed_order = order;
    memset(run->borrowed_blocks, 0, FEA_MAP_CHUNKS * FEA_CHUNK_VOXELS * sizeof(uint16_t));
    const int64_t started = time_now_ns();
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, fea_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    const int64_t took = time_now_ns() - started;
    printf("    %u workers: %.0f chunk columns/s\n", workers,
           (libc_double_t)((float64_t)FEA_MAP_CHUNKS / ((float64_t)took / 1e9f64)));
    return fea_digest(0xCBF29CE484222325ull, run->borrowed_blocks,
                      FEA_MAP_CHUNKS * FEA_CHUNK_VOXELS * sizeof(uint16_t));
}

TEST(one_and_sixteen_workers_give_the_same_bytes_in_any_order) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const fea_place_t *const place = &places[BIOME_ID_JUNGLE];
    REQUIRE(place->found != 0u);

    fea_run_t run = {
        .borrowed_terrain = &terrain,
        .borrowed_blocks = test_alloc(t, FEA_MAP_CHUNKS * FEA_CHUNK_VOXELS * sizeof(uint16_t)),
        .borrowed_opened = test_alloc(t, FEA_WORKERS_MANY * FEA_CHUNK_VOXELS),
        .origin_x = (place->chunk_x - 2) * (int32_t)FEA_CHUNK,
        .origin_y = (place->chunk_y - 2) * (int32_t)FEA_CHUNK,
    };
    uint32_t forwards[FEA_MAP_CHUNKS];
    uint32_t backwards[FEA_MAP_CHUNKS];
    uint32_t mixed[FEA_MAP_CHUNKS];
    for (uint32_t i = 0u; i < FEA_MAP_CHUNKS; i++) {
        forwards[i] = i;
        backwards[i] = FEA_MAP_CHUNKS - 1u - i;
        mixed[i] = (i * 7u + 3u) % FEA_MAP_CHUNKS;
    }

    job_system_t system;
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));
    const uint64_t one = fea_run_all(&system, &run, 1u, forwards);
    const uint64_t back = fea_run_all(&system, &run, 1u, backwards);
    const uint64_t many = fea_run_all(&system, &run, FEA_WORKERS_MANY, forwards);
    const uint64_t scattered = fea_run_all(&system, &run, FEA_WORKERS_MANY, mixed);
    job_deinit(&system);

    CHECK_EQ_U64(back, one);
    CHECK_EQ_U64(many, one);
    CHECK_EQ_U64(scattered, one);
    printf("    digest: 0x%016" PRIX64 "\n", one);
    CHECK_EQ_U64(one, FEA_DIGEST_PIN);
    terrain_deinit(&terrain);
}

/* ---- The phase's gate, on what the game asks for ---------------------------------------------------------------------- */

enum : uint32_t {
    FEA_WHOLE_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    FEA_WHOLE_VOXELS = FEA_COLUMNS * FEA_WHOLE_LAYERS,
};

typedef struct fea_whole_run_t {
    const terrain_t *borrowed_terrain;  /* 0 */
    const fea_place_t *borrowed_places; /* 8: the chunks to make, BIOME_COUNT of them, some not found */
    uint16_t *borrowed_blocks;          /* 16: FEA_WHOLE_VOXELS per biome */
    uint8_t *borrowed_opened;           /* 24: FEA_WHOLE_VOXELS per worker */
    _Atomic uint64_t worker_mask;       /* 32: who ran a job */
    _Atomic uint32_t next;              /* 40: the next biome to take, backwards or forwards */
    _Atomic uint32_t arrived;           /* 44: jobs at the barrier; each takes its share of memory from this */
    uint32_t barrier;                   /* 48: jobs that must all be running before any does its work */
    uint8_t backwards;                  /* 52 */
    uint8_t _pad0[3];                   /* 53 */
} fea_whole_run_t;
static_assert(sizeof(fea_whole_run_t) == 56);

static void fea_whole_job(job_worker_t *worker, void *borrowed_argument) {
    fea_whole_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t me = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    /* Every job holds its worker here until all have one, so that many workers really are at it. */
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        (void)sched_yield();
    }
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * FEA_WHOLE_VOXELS;
    for (;;) {
        const uint32_t turn = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (turn >= BIOME_COUNT) {
            return;
        }
        const uint32_t b = run->backwards != 0u ? BIOME_COUNT - 1u - turn : turn;
        const fea_place_t *const place = &run->borrowed_places[b];
        if (place->found == 0u) {
            continue;
        }
        const terrain_density_box_t box = {
            .origin_x = place->chunk_x * (int32_t)FEA_CHUNK,
            .origin_y = place->chunk_y * (int32_t)FEA_CHUNK,
            .origin_z = COLUMN_Z_STORAGE_MIN,
            .count_x = FEA_CHUNK,
            .count_y = FEA_CHUNK,
            .count_z = FEA_WHOLE_LAYERS,
        };
        terrain_generate_box(run->borrowed_terrain, &box, opened,
                             run->borrowed_blocks + (uint64_t)b * FEA_WHOLE_VOXELS);
    }
}

/* How many workers ran a job. */
static uint64_t fea_whole_run_all(job_system_t *system, fea_whole_run_t *run, uint32_t workers, bool backwards,
                                  uint16_t *blocks) {
    atomic_store_explicit(&run->worker_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->arrived, 0u, memory_order_relaxed);
    run->barrier = workers;
    run->backwards = backwards ? 1u : 0u;
    run->borrowed_blocks = blocks;
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, fea_whole_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    uint64_t mask = atomic_load_explicit(&run->worker_mask, memory_order_relaxed);
    uint64_t count = 0u;
    while (mask != 0u) {
        mask &= mask - 1u;
        count++;
    }
    return count;
}

/* The gate as the plan says it: whole columns, all the storage the game asks for, in a chunk of every biome on the surface, with
 * 16 workers shown to have been at it, and what those columns must be whatever the seed. Underground biomes are under these. */
TEST(whole_columns_of_every_biome_are_the_same_from_1_and_16_workers_and_sound) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t bytes = (uint64_t)BIOME_COUNT * FEA_WHOLE_VOXELS * sizeof(uint16_t);
    uint16_t *const one = test_alloc(t, bytes);
    uint16_t *const many = test_alloc(t, bytes);
    uint16_t *const scattered = test_alloc(t, bytes);
    memset(one, 0, bytes);
    memset(many, 0, bytes);
    memset(scattered, 0, bytes);
    fea_whole_run_t run = {
        .borrowed_terrain = &terrain,
        .borrowed_places = places,
        .borrowed_opened = test_alloc(t, (uint64_t)FEA_WORKERS_MANY * FEA_WHOLE_VOXELS),
    };
    job_system_t system;
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));
    heap_counters_t heap_before;
    heap_counters_read(&heap_before);
    CHECK_EQ_U64(fea_whole_run_all(&system, &run, 1u, false, one), 1u);
    CHECK_EQ_U64(fea_whole_run_all(&system, &run, FEA_WORKERS_MANY, false, many), FEA_WORKERS_MANY);
    CHECK_EQ_U64(fea_whole_run_all(&system, &run, FEA_WORKERS_MANY, true, scattered), FEA_WORKERS_MANY);
    heap_counters_t heap_after;
    heap_counters_read(&heap_after);
    CHECK_EQ_U64(heap_after.reserve_calls, heap_before.reserve_calls);
    job_deinit(&system);
    CHECK(memcmp(one, many, bytes) == 0);
    CHECK(memcmp(one, scattered, bytes) == 0);

    const uint64_t layer = FEA_COLUMNS;
    uint64_t made = 0u;
    uint64_t fluid = 0u;
    uint64_t fluid_on_air = 0u;
    uint64_t fluid_by_air = 0u;
    uint64_t water_by_lava = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        if (places[b].found == 0u) {
            CHECK(biome_defs[b].where == BIOME_WHERE_UNDERGROUND);
            continue;
        }
        CHECK(biome_defs[b].where != BIOME_WHERE_UNDERGROUND);
        made++;
        const uint16_t *const blocks = one + (uint64_t)b * FEA_WHOLE_VOXELS;
        uint64_t b_on_air = 0u;
        uint64_t b_by_air = 0u;
        uint64_t b_by_lava = 0u;
        for (uint32_t iz = 0u; iz < FEA_WHOLE_LAYERS; iz++) {
            const int32_t z = COLUMN_Z_STORAGE_MIN + (int32_t)iz;
            for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                const uint64_t at = i + layer * iz;
                const uint16_t block = blocks[at];
                if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
                    CHECK_EQ_U64(block, BLOCK_ID_AIR);
                    continue;
                }
                CHECK((z == MC_WORLD_Z_MIN) == (block == BLOCK_ID_BEDROCK));
                if (block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA) {
                    continue;
                }
                fluid++;
                b_on_air += blocks[at - layer] == BLOCK_ID_AIR ? 1u : 0u;
                const uint32_t x = i % FEA_CHUNK;
                const uint32_t y = i / FEA_CHUNK;
                const uint16_t sides[4] = {
                    x > 0u ? blocks[at - 1u] : BLOCK_ID_STONE,
                    x + 1u < FEA_CHUNK ? blocks[at + 1u] : BLOCK_ID_STONE,
                    y > 0u ? blocks[at - FEA_CHUNK] : BLOCK_ID_STONE,
                    y + 1u < FEA_CHUNK ? blocks[at + FEA_CHUNK] : BLOCK_ID_STONE,
                };
                for (uint32_t s = 0u; s < 4u; s++) {
                    b_by_air += sides[s] == BLOCK_ID_AIR ? 1u : 0u;
                    b_by_lava += block == BLOCK_ID_WATER && sides[s] == BLOCK_ID_LAVA ? 1u : 0u;
                }
            }
        }
        if (b_on_air + b_by_air + b_by_lava != 0u) {
            printf("    %s at chunk %" PRId32 ", %" PRId32 ": %" PRIu64 " fluid on air, %" PRIu64 " by air, %" PRIu64
                   " water by lava\n",
                   biome_defs[b].name, places[b].chunk_x, places[b].chunk_y, b_on_air, b_by_air, b_by_lava);
        }
        fluid_on_air += b_on_air;
        fluid_by_air += b_by_air;
        water_by_lava += b_by_lava;
    }
    printf("    %" PRIu64 " whole chunks, %" PRIu64 " fluid voxels\n", made, fluid);
    CHECK(made != 0u);
    CHECK(fluid != 0u);
    CHECK_EQ_U64(fluid_on_air, 0u);
    CHECK_EQ_U64(fluid_by_air, 0u);
    CHECK_EQ_U64(water_by_lava, 0u);
    terrain_deinit(&terrain);
}

/* ---- The trees ---------------------------------------------------------------------------------------------------- */

typedef struct fea_tree_t {
    terrain_feature_tree_t kind; /* 0 */
    uint8_t _pad0[1];            /* 1 */
    uint16_t log;                /* 2 */
    uint16_t leaf;               /* 4 */
    uint8_t _pad1[2];            /* 6 */
} fea_tree_t;
static_assert(sizeof(fea_tree_t) == 8);

static const fea_tree_t FEA_TREES[TERRAIN_FEATURE_TREES] = {
    {.kind = TERRAIN_FEATURE_TREE_OAK, .log = BLOCK_ID_OAK_LOG, .leaf = BLOCK_ID_OAK_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_BIRCH, .log = BLOCK_ID_BIRCH_LOG, .leaf = BLOCK_ID_BIRCH_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_SPRUCE, .log = BLOCK_ID_SPRUCE_LOG, .leaf = BLOCK_ID_SPRUCE_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_JUNGLE, .log = BLOCK_ID_JUNGLE_LOG, .leaf = BLOCK_ID_JUNGLE_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_ACACIA, .log = BLOCK_ID_ACACIA_LOG, .leaf = BLOCK_ID_ACACIA_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_DARK_OAK, .log = BLOCK_ID_DARK_OAK_LOG, .leaf = BLOCK_ID_DARK_OAK_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_MANGROVE, .log = BLOCK_ID_MANGROVE_LOG, .leaf = BLOCK_ID_MANGROVE_LEAVES},
    {.kind = TERRAIN_FEATURE_TREE_CHERRY, .log = BLOCK_ID_CHERRY_LOG, .leaf = BLOCK_ID_CHERRY_LEAVES},
};

/* Every kind drawn in the air, over and over: only its own blocks, inside its reach and height, with a trunk on its site, and
 * leaves on top. With all 5 layers of water a mangrove can have under it, and vines. */
TEST(each_tree_stays_within_its_reach_and_is_its_own_kind) {
    enum : uint32_t { ACROSS = 32u, TALL = 48u, VOXELS = ACROSS * ACROSS * TALL, SEEDS = 500u };
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = -16,
        .origin_y = -16,
        .origin_z = 0,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    const terrain_feature_canvas_t canvas = terrain_feature_canvas_of(&box, blocks);
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        const fea_tree_t *const tree = &FEA_TREES[k];
        const int32_t reach = terrain_feature_tree_reach(tree->kind);
        CHECK(reach > 0 && reach < (int32_t)FEA_CHUNK);
        int32_t reached = 0;
        int32_t tallest = 0;
        uint32_t wide = 0u;
        for (uint32_t s = 0u; s < SEEDS; s++) {
            memset(blocks, 0, VOXELS * sizeof(uint16_t));
            for (uint32_t i = 0u; i < VOXELS; i++) {
                blocks[i] = BLOCK_ID_AIR;
            }
            const uint64_t seed = 0x51ull * s + k;
            const uint8_t lift = tree->kind == TERRAIN_FEATURE_TREE_MANGROVE ? 5u : 0u;
            const terrain_feature_tree_site_t site = {
                .seed = seed,
                .x = 0,
                .y = 0,
                .z = 4,
                .foot = {3, 3, 3, 3},
                .kind = (uint8_t)tree->kind,
                .strands = 3u,
                .lift = lift,
            };
            terrain_feature_paint_tree(&canvas, &site);
            uint32_t logs = 0u;
            uint32_t leaves = 0u;
            for (uint32_t i = 0u; i < VOXELS; i++) {
                const uint16_t block = blocks[i];
                if (block == BLOCK_ID_AIR) {
                    continue;
                }
                const int32_t x = (int32_t)(i % ACROSS) + box.origin_x;
                const int32_t y = (int32_t)(i / ACROSS % ACROSS) + box.origin_y;
                const int32_t z = (int32_t)(i / (ACROSS * ACROSS));
                const int32_t ax = x < 0 ? -x : x;
                const int32_t ay = y < 0 ? -y : y;
                reached = ax > reached ? ax : reached;
                reached = ay > reached ? ay : reached;
                tallest = z - 3 > tallest ? z - 3 : tallest;
                logs += block == tree->log ? 1u : 0u;
                leaves += block == tree->leaf ? 1u : 0u;
                CHECK(block == tree->log || block == tree->leaf || block == BLOCK_ID_VINE);
            }
            CHECK(logs >= 4u);
            CHECK(leaves >= 4u);
            CHECK_EQ_U64(blocks[fea_at(&box, 0, 0, 4)], tree->log);
            CHECK(blocks[fea_at(&box, 0, 0, 3)] == BLOCK_ID_AIR);
            wide += terrain_feature_tree_is_wide(tree->kind, seed) ? 1u : 0u;
            if (terrain_feature_tree_is_wide(tree->kind, seed)) {
                CHECK_EQ_U64(blocks[fea_at(&box, 1, 1, 4)], tree->log);
            }
        }
        printf("    kind %u: reach %d of %d, height %d of %d, wide %u of %u\n", k, reached, reach, tallest,
               (int32_t)TERRAIN_FEATURE_TALLEST, wide, SEEDS);
        CHECK(reached <= reach);
        CHECK(tallest <= TERRAIN_FEATURE_TALLEST);
    }
}

/* A tree is the same tree on either side of any line through it: drawn whole, and drawn on the canvases of two chunks. */
TEST(a_tree_is_whole_across_chunk_lines) {
    enum : uint32_t { ACROSS = 32u, TALL = 48u, VOXELS = ACROSS * ACROSS * TALL };
    uint16_t *const whole = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const parts = test_alloc(t, VOXELS * sizeof(uint16_t));
    const terrain_density_box_t box = {
        .origin_x = -16,
        .origin_y = -16,
        .origin_z = 0,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    for (uint32_t k = 0u; k < TERRAIN_FEATURE_TREES; k++) {
        for (uint32_t i = 0u; i < VOXELS; i++) {
            whole[i] = BLOCK_ID_AIR;
            parts[i] = BLOCK_ID_AIR;
        }
        const terrain_feature_tree_site_t site = {
            .seed = 0x777ull + k,
            .x = 0,
            .y = 0,
            .z = 4,
            .foot = {3, 3, 3, 3},
            .kind = (uint8_t)k,
            .strands = 2u,
        };
        const terrain_feature_canvas_t all = terrain_feature_canvas_of(&box, whole);
        terrain_feature_paint_tree(&all, &site);
        terrain_feature_canvas_t canvas = terrain_feature_canvas_of(&box, parts);
        const terrain_feature_canvas_t bounds = canvas;
        /* Four quarters, meeting at the tree's own site. */
        for (uint32_t q = 0u; q < 4u; q++) {
            canvas = bounds;
            canvas.x_low = (q & 1u) == 0u ? bounds.x_low : 0;
            canvas.x_high = (q & 1u) == 0u ? -1 : bounds.x_high;
            canvas.y_low = (q & 2u) == 0u ? bounds.y_low : 0;
            canvas.y_high = (q & 2u) == 0u ? -1 : bounds.y_high;
            terrain_feature_paint_tree(&canvas, &site);
        }
        CHECK_MEM_EQ(parts, whole, VOXELS * sizeof(uint16_t));
    }
}

/* ---- The columns -------------------------------------------------------------------------------------------------- */

/* What a chunk's walk says against the stages walked from the sky, in every place the search finds. */
TEST(a_columns_ground_is_what_the_stages_leave) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t layer = FEA_COLUMNS;
    uint16_t *const blocks = test_alloc(t, layer * FEA_LAYERS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, layer * FEA_LAYERS);
    uint32_t chunks = 0u;
    uint32_t holes = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        if (places[b].found == 0u || biome_defs[b].where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        chunks++;
        const terrain_density_box_t box = {
            .origin_x = places[b].chunk_x * (int32_t)FEA_CHUNK,
            .origin_y = places[b].chunk_y * (int32_t)FEA_CHUNK,
            .origin_z = MC_WORLD_Z_MIN,
            .count_x = FEA_CHUNK,
            .count_y = FEA_CHUNK,
            .count_z = FEA_LAYERS,
        };
        fea_stages(&terrain, &box, opened, blocks);
        terrain_feature_column_t columns[FEA_COLUMNS];
        terrain_feature_chunk_columns(terrain.borrowed_feature, places[b].chunk_x, places[b].chunk_y, columns);
        for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
            /* From the sky: the first that is not air, and how much of it is water. */
            int32_t z = (int32_t)FEA_LAYERS - 1;
            while (z >= 0 && blocks[i + layer * (uint32_t)z] == BLOCK_ID_AIR) {
                z--;
            }
            uint32_t water = 0u;
            while (z >= 0 && blocks[i + layer * (uint32_t)z] == BLOCK_ID_WATER) {
                water++;
                z--;
            }
            const int32_t top = z + MC_WORLD_Z_MIN;
            if (columns[i].top == TERRAIN_FEATURE_NO_GROUND) {
                holes++;
                continue;
            }
            CHECK_EQ_I64(columns[i].top, top);
            CHECK_EQ_U64(columns[i].water, water);
            /* Lining and ore change only rock, and the walk runs neither: rock is all it can differ in. */
            const uint16_t real = blocks[i + layer * (uint32_t)z];
            CHECK(columns[i].block == real || real == BLOCK_ID_MOSS_BLOCK || columns[i].block == BLOCK_ID_STONE ||
                  columns[i].block == BLOCK_ID_DEEPSLATE);
        }
    }
    printf("    %u chunks; %u of %u columns have their ground cut away\n", chunks, holes, chunks * FEA_COLUMNS);
    CHECK(chunks >= 45u);
    CHECK(holes < chunks * FEA_COLUMNS / 20u);
    terrain_deinit(&terrain);
}

/* ---- What stands where -------------------------------------------------------------------------------------------- */

/* What a place's real voxels come to, with each column's ground from the walk, row by row. */
typedef struct fea_count_t {
    uint64_t by_block[BLOCK_COUNT];
} fea_count_t;
static_assert(sizeof(fea_count_t) == 8u * BLOCK_COUNT);

static bool fea_is_soil(uint16_t block) {
    return block == BLOCK_ID_GRASS_BLOCK || block == BLOCK_ID_DIRT || block == BLOCK_ID_PODZOL ||
           block == BLOCK_ID_COARSE_DIRT || block == BLOCK_ID_ROOTED_DIRT || block == BLOCK_ID_MOSS_BLOCK ||
           block == BLOCK_ID_MUD || block == BLOCK_ID_MYCELIUM;
}

static bool fea_is_land_plant(uint16_t block) {
    return block == BLOCK_ID_TALL_GRASS || block == BLOCK_ID_FERN || block == BLOCK_ID_DANDELION ||
           block == BLOCK_ID_ROSE || block == BLOCK_ID_ALLIUM || block == BLOCK_ID_AZURE_BLUET ||
           block == BLOCK_ID_BLUE_ORCHID || block == BLOCK_ID_TULIP || block == BLOCK_ID_OXEYE_DAISY ||
           block == BLOCK_ID_CORNFLOWER || block == BLOCK_ID_LILY_OF_THE_VALLEY || block == BLOCK_ID_BROWN_MUSHROOM ||
           block == BLOCK_ID_RED_MUSHROOM || block == BLOCK_ID_MELON || block == BLOCK_ID_PUMPKIN;
}

/* What a crown leaves beside a trunk: its leaves, or the ground of a hill it could not grow into. Air there is a crown cut off at the line. */
static bool fea_crown_holds(uint16_t block) {
    return block != BLOCK_ID_AIR && (block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u;
}

static bool fea_is_dry_log(uint16_t block) {
    return block == BLOCK_ID_OAK_LOG || block == BLOCK_ID_BIRCH_LOG || block == BLOCK_ID_SPRUCE_LOG ||
           block == BLOCK_ID_JUNGLE_LOG || block == BLOCK_ID_ACACIA_LOG || block == BLOCK_ID_DARK_OAK_LOG ||
           block == BLOCK_ID_CHERRY_LOG;
}

static bool fea_is_leaf(uint16_t block) {
    return block == BLOCK_ID_OAK_LEAVES || block == BLOCK_ID_BIRCH_LEAVES || block == BLOCK_ID_SPRUCE_LEAVES ||
           block == BLOCK_ID_JUNGLE_LEAVES || block == BLOCK_ID_ACACIA_LEAVES || block == BLOCK_ID_DARK_OAK_LEAVES ||
           block == BLOCK_ID_MANGROVE_LEAVES || block == BLOCK_ID_CHERRY_LEAVES;
}

/* Trees standing right by a chunk line, in real worlds. A trunk's top has leaves on all 4 sides whichever chunk those are in, so
 * a trunk on a chunk's edge has its leaves across the line, in a chunk generated by itself. */
TEST(trees_in_the_world_have_their_crowns_across_chunk_lines) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    enum : uint32_t { CHUNKS = 9u, LAYERS = 80u };
    const uint64_t voxels = (uint64_t)FEA_COLUMNS * LAYERS;
    uint16_t *const blocks = test_alloc(t, CHUNKS * CHUNKS * voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    const fea_place_t *const place = &places[BIOME_ID_BIRCH_FOREST];
    REQUIRE(place->found != 0u);
    const int32_t z_low = place->ground - 30;
    /* A chunk at a time, each knowing nothing of what the last one made. */
    for (uint32_t cy = 0u; cy < CHUNKS; cy++) {
        for (uint32_t cx = 0u; cx < CHUNKS; cx++) {
            const terrain_density_box_t box = {
                .origin_x = (place->chunk_x - 4 + (int32_t)cx) * (int32_t)FEA_CHUNK,
                .origin_y = (place->chunk_y - 4 + (int32_t)cy) * (int32_t)FEA_CHUNK,
                .origin_z = z_low,
                .count_x = FEA_CHUNK,
                .count_y = FEA_CHUNK,
                .count_z = LAYERS,
            };
            terrain_generate_box(&terrain, &box, opened, blocks + (uint64_t)(cx + CHUNKS * cy) * voxels);
        }
    }
    uint32_t edges = 0u;
    for (uint32_t cy = 1u; cy + 1u < CHUNKS; cy++) {
        for (uint32_t cx = 1u; cx + 1u < CHUNKS; cx++) {
            const uint16_t *const here = blocks + (uint64_t)(cx + CHUNKS * cy) * voxels;
            for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                const uint32_t ox = i % FEA_CHUNK;
                const uint32_t oy = i / FEA_CHUNK;
                if (ox != 0u && ox != 15u && oy != 0u && oy != 15u) {
                    continue;
                }
                for (uint32_t z = 1u; z + 1u < LAYERS; z++) {
                    /* The top log of a trunk, standing alone: a birch's plan, not a bough. */
                    if (here[i + FEA_COLUMNS * z] != BLOCK_ID_BIRCH_LOG ||
                        here[i + FEA_COLUMNS * (z + 1u)] != BLOCK_ID_BIRCH_LEAVES) {
                        continue;
                    }
                    /* Each side off this chunk, in the chunk over the line. */
                    if (ox == 15u) {
                        const uint16_t *const over = blocks + (uint64_t)(cx + 1u + CHUNKS * cy) * voxels;
                        CHECK(fea_crown_holds(over[0u + FEA_CHUNK * oy + FEA_COLUMNS * z]));
                        edges++;
                    }
                    if (ox == 0u) {
                        const uint16_t *const over = blocks + (uint64_t)(cx - 1u + CHUNKS * cy) * voxels;
                        CHECK(fea_crown_holds(over[15u + FEA_CHUNK * oy + FEA_COLUMNS * z]));
                        edges++;
                    }
                    if (oy == 15u) {
                        const uint16_t *const over = blocks + (uint64_t)(cx + CHUNKS * (cy + 1u)) * voxels;
                        CHECK(fea_crown_holds(over[ox + FEA_COLUMNS * z]));
                        edges++;
                    }
                    if (oy == 0u) {
                        const uint16_t *const over = blocks + (uint64_t)(cx + CHUNKS * (cy - 1u)) * voxels;
                        CHECK(fea_crown_holds(over[ox + 15u * FEA_CHUNK + FEA_COLUMNS * z]));
                        edges++;
                    }
                }
            }
        }
    }
    printf("    %u trunks on a chunk line\n", edges);
    CHECK(edges >= 5u);
    terrain_deinit(&terrain);
}

/* Lily pads ask for water in a land biome, which only low ground has. Walks a swamp's chunks for it, and pads must be on the water
 * and nowhere else. */
TEST(lily_pads_grow_on_a_swamps_standing_water) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    terrain_biome_column_t *const columns = test_alloc(t, FEA_SEARCH_SIDE * sizeof(*columns));
    const int32_t half = (int32_t)(FEA_SEARCH_SIDE * FEA_CHUNK * 8u);
    enum : uint32_t { LAYERS = 40u };
    uint16_t *const blocks = test_alloc(t, (uint64_t)FEA_COLUMNS * LAYERS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, (uint64_t)FEA_COLUMNS * LAYERS);
    uint32_t chunks = 0u;
    uint64_t pads = 0u;
    uint64_t logs = 0u;
    const int32_t z_low = MC_WORLD_Z_SEA - 20;
    for (uint32_t row = 0u; row < FEA_SEARCH_SIDE && chunks < 600u; row++) {
        const terrain_climate_area_t area = {
            .origin_x = -half,
            .origin_y = -half + (int32_t)(row * 16u * FEA_CHUNK),
            .step_x = (int32_t)(16u * FEA_CHUNK),
            .step_y = 1,
            .count_x = FEA_SEARCH_SIDE,
            .count_y = 1u,
        };
        terrain_biome_columns_area(terrain.borrowed_biome, &area, columns);
        for (uint32_t i = 0u; i < FEA_SEARCH_SIDE && chunks < 600u; i++) {
            const bool land = biome_defs[columns[i].biome].where == BIOME_WHERE_LAND;
            if (!land || columns[i].ground >= (float32_t)MC_WORLD_Z_SEA) {
                continue;
            }
            chunks++;
            const terrain_density_box_t box = {
                .origin_x = area.origin_x + (int32_t)(i * 16u * FEA_CHUNK),
                .origin_y = area.origin_y,
                .origin_z = z_low,
                .count_x = FEA_CHUNK,
                .count_y = FEA_CHUNK,
                .count_z = LAYERS,
            };
            terrain_generate_box(&terrain, &box, opened, blocks);
            for (uint32_t c = 0u; c < FEA_COLUMNS; c++) {
                for (uint32_t z = 1u; z < LAYERS; z++) {
                    /* Land under water is where any tree could wrongly stand. Only a mangrove may. */
                    if (fea_is_dry_log(blocks[c + FEA_COLUMNS * z])) {
                        logs++;
                        CHECK(blocks[c + FEA_COLUMNS * (z - 1u)] != BLOCK_ID_WATER);
                    }
                    if (blocks[c + FEA_COLUMNS * z] == BLOCK_ID_LILY_PAD) {
                        pads++;
                        CHECK(blocks[c + FEA_COLUMNS * (z - 1u)] == BLOCK_ID_WATER);
                        CHECK(z_low + (int32_t)z == MC_WORLD_Z_SEA + 1);
                    }
                }
            }
        }
    }
    printf("    %" PRIu64 " pads and %" PRIu64 " logs on %u wet chunks of land\n", pads, logs, chunks);
    REQUIRE(chunks > 0u);
    CHECK(pads > 0u);
    terrain_deinit(&terrain);
}

/* Every voxel of every biome's map: each plant on something it can live on, nothing floating, sea plants in water, and what each
 * biome's file asks for really there. */
TEST(everything_stands_where_it_may_in_every_biome) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t voxels = (uint64_t)FEA_MAP_ACROSS * FEA_MAP_ACROSS * 220u;
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    uint16_t *const before = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    fea_count_t *const count = test_alloc(t, sizeof(*count));
    memset(count, 0, sizeof(*count));
    uint64_t checks = 0u;
    /* A biome's first place may be a sliver of it, too small for what it grows, so each is looked at in a few. */
    for (uint32_t pass = 0u; pass < FEA_PLACES_EACH; pass++) {
        if (pass > 0u) {
            fea_find_places_after(t, &terrain, places, pass);
        }
        for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
            if (places[b].found == 0u || biome_defs[b].where == BIOME_WHERE_UNDERGROUND) {
                continue;
            }
            const terrain_density_box_t box = fea_map_box(&places[b]);
            fea_stages(&terrain, &box, opened, before);
            terrain_generate_box(&terrain, &box, opened, blocks);
            const uint64_t layer = (uint64_t)box.count_x * box.count_y;
            for (uint32_t iz = 1u; iz + 1u < box.count_z; iz++) {
                for (uint32_t iy = 1u; iy + 1u < box.count_y; iy++) {
                    for (uint32_t ix = 1u; ix + 1u < box.count_x; ix++) {
                        const uint64_t at = ix + (uint64_t)box.count_x * iy + layer * iz;
                        const uint16_t block = blocks[at];
                        count->by_block[block]++;
                        const uint16_t under = blocks[at - layer];
                        if (fea_is_land_plant(block)) {
                            CHECK(fea_is_soil(under) || under == BLOCK_ID_SNOW_BLOCK);
                            checks++;
                        } else if (block == BLOCK_ID_DEAD_BUSH) {
                            CHECK(under == BLOCK_ID_SAND || under == BLOCK_ID_RED_SAND ||
                                  under == BLOCK_ID_COARSE_DIRT || under == BLOCK_ID_DIRT ||
                                  under == BLOCK_ID_TERRACOTTA);
                            checks++;
                        } else if (block == BLOCK_ID_CACTUS) {
                            CHECK(under == BLOCK_ID_SAND || under == BLOCK_ID_RED_SAND || under == BLOCK_ID_CACTUS);
                            checks++;
                        } else if (block == BLOCK_ID_SUGAR_CANE) {
                            CHECK(under == BLOCK_ID_SUGAR_CANE || fea_is_soil(under) || under == BLOCK_ID_SAND ||
                                  under == BLOCK_ID_RED_SAND);
                            if (under != BLOCK_ID_SUGAR_CANE) {
                                const bool wet = blocks[at - layer + 1u] == BLOCK_ID_WATER ||
                                                 blocks[at - layer - 1u] == BLOCK_ID_WATER ||
                                                 blocks[at - layer + box.count_x] == BLOCK_ID_WATER ||
                                                 blocks[at - layer - box.count_x] == BLOCK_ID_WATER;
                                CHECK(wet);
                            }
                            checks++;
                        } else if (block == BLOCK_ID_LILY_PAD) {
                            CHECK(under == BLOCK_ID_WATER);
                            checks++;
                        } else if (block == BLOCK_ID_SEAGRASS) {
                            CHECK(blocks[at + layer] == BLOCK_ID_WATER || blocks[at + layer] == BLOCK_ID_ICE ||
                                  blocks[at + layer] == BLOCK_ID_KELP || blocks[at + layer] == BLOCK_ID_MANGROVE_LOG);
                            checks++;
                        } else if (block == BLOCK_ID_KELP) {
                            CHECK(blocks[at + layer] == BLOCK_ID_WATER || blocks[at + layer] == BLOCK_ID_KELP ||
                                  blocks[at + layer] == BLOCK_ID_ICE);
                            checks++;
                        } else if (block == BLOCK_ID_SNOW_BLOCK && before[at] != BLOCK_ID_SNOW_BLOCK) {
                            /* What the surface lays can have a cave under it. What is put here cannot. */
                            CHECK(under != BLOCK_ID_AIR && under != BLOCK_ID_WATER && under != BLOCK_ID_LAVA);
                            CHECK(under != BLOCK_ID_SNOW_BLOCK && under != BLOCK_ID_ICE);
                            checks++;
                        } else if (fea_is_dry_log(block)) {
                            /* Only a mangrove stands in water. */
                            CHECK(under != BLOCK_ID_WATER);
                            checks++;
                        }
                    }
                }
            }
        }
    }
    printf("    %" PRIu64 " voxels checked\n", checks);
    /* Every kind the biome files can ask for has turned up somewhere. Pumpkin is in no biome's file. */
    const uint16_t wanted[] = {
        BLOCK_ID_OAK_LOG,    BLOCK_ID_BIRCH_LOG,      BLOCK_ID_SPRUCE_LOG,   BLOCK_ID_JUNGLE_LOG,
        BLOCK_ID_ACACIA_LOG, BLOCK_ID_DARK_OAK_LOG,   BLOCK_ID_MANGROVE_LOG, BLOCK_ID_CHERRY_LOG,
        BLOCK_ID_TALL_GRASS, BLOCK_ID_FERN,           BLOCK_ID_CACTUS,       BLOCK_ID_SUGAR_CANE,
        BLOCK_ID_DEAD_BUSH,  BLOCK_ID_BROWN_MUSHROOM, BLOCK_ID_RED_MUSHROOM, BLOCK_ID_VINE,
        BLOCK_ID_SEAGRASS,   BLOCK_ID_KELP,           BLOCK_ID_ICE,          BLOCK_ID_SNOW_BLOCK,
        BLOCK_ID_PACKED_ICE, BLOCK_ID_DANDELION,      BLOCK_ID_MELON,
    };
    for (uint32_t i = 0u; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        printf("    block %3u: %" PRIu64 "\n", wanted[i], count->by_block[wanted[i]]);
        CHECK(count->by_block[wanted[i]] > 0u);
    }
    terrain_deinit(&terrain);
}

/* A forest's trunks against what its file asks for: each tree's lowest log, with soil under it. A file's whole number of tries is
 * exactly how many sites are drawn, so standing trees can only be fewer, or as many where wide trunks count twice, but not many
 * fewer, as nearly all of a forest's floor can take one. */
TEST(a_forest_has_about_the_trees_its_file_asks_for) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const fea_place_t *const place = &places[BIOME_ID_BIRCH_FOREST];
    REQUIRE(place->found != 0u);
    const terrain_density_box_t box = fea_map_box(place);
    const uint64_t voxels = terrain_density_box_voxels(&box);
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    terrain_generate_box(&terrain, &box, opened, blocks);
    uint32_t trunks = 0u;
    uint32_t columns_of_it = 0u;
    const uint64_t layer = (uint64_t)box.count_x * box.count_y;
    terrain_feature_column_t tops[FEA_COLUMNS];
    for (uint32_t cy = 0u; cy < FEA_MAP_SIDE; cy++) {
        for (uint32_t cx = 0u; cx < FEA_MAP_SIDE; cx++) {
            terrain_feature_chunk_columns(terrain.borrowed_feature, box.origin_x / (int32_t)FEA_CHUNK + (int32_t)cx,
                                          box.origin_y / (int32_t)FEA_CHUNK + (int32_t)cy, tops);
            for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                columns_of_it += tops[i].biome == BIOME_ID_BIRCH_FOREST ? 1u : 0u;
                if (tops[i].biome != BIOME_ID_BIRCH_FOREST || tops[i].top == TERRAIN_FEATURE_NO_GROUND) {
                    continue;
                }
                const uint64_t ix = (uint64_t)cx * FEA_CHUNK + i % FEA_CHUNK;
                const uint64_t iy = (uint64_t)cy * FEA_CHUNK + i / FEA_CHUNK;
                const int64_t iz = (int64_t)tops[i].top + 1 - (int64_t)box.origin_z;
                if (iz >= 0 && (uint64_t)iz < box.count_z &&
                    blocks[ix + (uint64_t)box.count_x * iy + layer * (uint64_t)iz] == BLOCK_ID_BIRCH_LOG) {
                    trunks++;
                }
            }
        }
    }
    const float64_t chunks = (float64_t)columns_of_it / (float64_t)FEA_COLUMNS;
    const float64_t per_chunk = (float64_t)trunks / chunks;
    const float64_t asked = (float64_t)biome_features[BIOME_ID_BIRCH_FOREST][BIOME_FEATURE_TREE_BIRCH];
    printf("    %u trunks on %.1f chunks of it: %.2f a chunk, of %.0f asked\n", trunks, (libc_double_t)chunks,
           (libc_double_t)per_chunk, (libc_double_t)asked);
    CHECK(chunks >= 5.0f64);
    CHECK(per_chunk > asked * 0.6f64);
    CHECK(per_chunk <= asked * 1.05f64);
    const uint64_t digest = fea_digest(0xCBF29CE484222325ull, blocks, voxels * sizeof(uint16_t));
    printf("    digest: 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, FEA_FOREST_PIN);
    terrain_deinit(&terrain);
}

/* ---- Lakes -------------------------------------------------------------------------------------------------------- */

static bool fea_is_ground(uint16_t block) {
    return block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA;
}

/* Every lake of a stretch of land: its voxels against the plan, with the bytes as the game gets them. Every voxel of water
 * touches only water or ground, on all sides but up, so none can flow. */
TEST(every_lake_holds_its_water) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const fea_place_t *const place = &places[BIOME_ID_PLAINS];
    REQUIRE(place->found != 0u);
    enum : int32_t { SPREAD = 40 };
    uint32_t lakes = 0u;
    uint32_t tried = 0u;
    enum : uint32_t { ACROSS = FEA_CHUNK + 2u, LAYERS = 40u };
    uint16_t *const blocks = test_alloc(t, (uint64_t)ACROSS * ACROSS * LAYERS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, (uint64_t)ACROSS * ACROSS * LAYERS);
    for (int32_t dy = -SPREAD; dy <= SPREAD; dy++) {
        for (int32_t dx = -SPREAD; dx <= SPREAD; dx++) {
            const int32_t chunk_x = place->chunk_x + dx;
            const int32_t chunk_y = place->chunk_y + dy;
            const terrain_feature_lake_t lake = terrain_feature_chunk_lake(terrain.borrowed_feature, chunk_x, chunk_y);
            tried++;
            if (lake.exists == 0u) {
                continue;
            }
            lakes++;
            CHECK(lake.level > MC_WORLD_Z_SEA);
            CHECK(lake.depth >= 2u && lake.depth <= TERRAIN_FEATURE_LAKE_DEPTH_MAX);
            const terrain_density_box_t box = {
                .origin_x = chunk_x * (int32_t)FEA_CHUNK - 1,
                .origin_y = chunk_y * (int32_t)FEA_CHUNK - 1,
                .origin_z = (int32_t)lake.level - 20,
                .count_x = ACROSS,
                .count_y = ACROSS,
                .count_z = LAYERS,
            };
            terrain_generate_box(&terrain, &box, opened, blocks);
            const uint64_t layer = (uint64_t)ACROSS * ACROSS;
            uint32_t wet = 0u;
            for (uint32_t oy = 0u; oy < FEA_CHUNK; oy++) {
                for (uint32_t ox = 0u; ox < FEA_CHUNK; ox++) {
                    const uint32_t depth = terrain_feature_lake_depth(&lake, ox, oy);
                    const uint64_t column = (uint64_t)(ox + 1u) + (uint64_t)ACROSS * (oy + 1u);
                    if (depth == 0u) {
                        continue;
                    }
                    wet++;
                    /* The level itself is water or, in the cold, ice, and over it is not ground. */
                    const uint16_t surface = blocks[column + layer * 20u];
                    CHECK(surface == BLOCK_ID_WATER || surface == BLOCK_ID_ICE);
                    const uint16_t over = blocks[column + layer * 21u];
                    CHECK(!fea_is_ground(over) || fea_is_leaf(over));
                    /* Down from the level: water, then ground, and every voxel of it held on all four sides. */
                    uint32_t iz = 20u;
                    while (blocks[column + layer * iz] == BLOCK_ID_WATER ||
                           blocks[column + layer * iz] == BLOCK_ID_ICE) {
                        const uint64_t at = column + layer * iz;
                        const uint64_t sides[4] = {at + 1u, at - 1u, at + ACROSS, at - ACROSS};
                        for (uint32_t s = 0u; s < 4u; s++) {
                            const uint16_t next = blocks[sides[s]];
                            CHECK(next == BLOCK_ID_WATER || next == BLOCK_ID_ICE || fea_is_ground(next));
                        }
                        iz--;
                        REQUIRE(iz > 0u);
                    }
                    CHECK(fea_is_ground(blocks[column + layer * iz]));
                    CHECK(20u - iz >= 1u && 20u - iz <= TERRAIN_FEATURE_LAKE_DEPTH_MAX);
                }
            }
            CHECK(wet >= 20u);
        }
    }
    printf("    %u lakes in %u chunks\n", lakes, tried);
    CHECK(lakes >= 3u);
    terrain_deinit(&terrain);
}

/* A plan is inside its chunk with its margin, however it is drawn. */
TEST(a_lake_keeps_inside_its_chunk) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    const terrain_feature_lake_t lake = {
        .level = 10,
        .exists = 1u,
        .depth = 4u,
        .centres = {8u, 8u, 7u, 9u},
        .radii = {5u, 4u, 4u, 3u},
    };
    CHECK_EQ_U64(terrain_feature_lake_depth(&lake, 8u, 8u), 4u);
    CHECK_EQ_U64(terrain_feature_lake_depth(&lake, 0u, 0u), 0u);
    const terrain_feature_lake_t none = {.exists = 0u};
    CHECK_EQ_U64(terrain_feature_lake_depth(&none, 8u, 8u), 0u);
    uint32_t found = 0u;
    for (int32_t y = -30; y < 30; y++) {
        for (int32_t x = -30; x < 30; x++) {
            const terrain_feature_lake_t got = terrain_feature_chunk_lake(terrain.borrowed_feature, x, y);
            if (got.exists == 0u) {
                continue;
            }
            found++;
            for (uint32_t i = 0u; i < FEA_CHUNK; i++) {
                CHECK_EQ_U64(terrain_feature_lake_depth(&got, 0u, i), 0u);
                CHECK_EQ_U64(terrain_feature_lake_depth(&got, 15u, i), 0u);
                CHECK_EQ_U64(terrain_feature_lake_depth(&got, i, 0u), 0u);
                CHECK_EQ_U64(terrain_feature_lake_depth(&got, i, 15u), 0u);
            }
        }
    }
    printf("    %u lakes\n", found);
    terrain_deinit(&terrain);
}

/* ---- Ice and snow ------------------------------------------------------------------------------------------------- */

/* In each biome: open water is ice exactly when the file says so, and dry ground has snow on it exactly when the file says so. */
TEST(ice_and_snow_are_where_each_biomes_file_says) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    enum : uint32_t { LAYERS = 220u };
    uint16_t *const blocks = test_alloc(t, (uint64_t)FEA_COLUMNS * LAYERS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, (uint64_t)FEA_COLUMNS * LAYERS);
    uint32_t frozen = 0u;
    uint32_t open_water = 0u;
    uint32_t snowy = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        if (places[b].found == 0u || biome_defs[b].where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        const int32_t chunk_x = places[b].chunk_x;
        const int32_t chunk_y = places[b].chunk_y;
        terrain_feature_column_t tops[FEA_COLUMNS];
        terrain_feature_chunk_columns(terrain.borrowed_feature, chunk_x, chunk_y, tops);
        const terrain_feature_lake_t lake = terrain_feature_chunk_lake(terrain.borrowed_feature, chunk_x, chunk_y);
        const int32_t ground = places[b].ground > MC_WORLD_Z_SEA ? places[b].ground : MC_WORLD_Z_SEA;
        const terrain_density_box_t box = {
            .origin_x = chunk_x * (int32_t)FEA_CHUNK,
            .origin_y = chunk_y * (int32_t)FEA_CHUNK,
            .origin_z = ground - 120,
            .count_x = FEA_CHUNK,
            .count_y = FEA_CHUNK,
            .count_z = LAYERS,
        };
        terrain_generate_box(&terrain, &box, opened, blocks);
        for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
            const terrain_feature_column_t *const column = &tops[i];
            if (column->top == TERRAIN_FEATURE_NO_GROUND ||
                terrain_feature_lake_depth(&lake, i % FEA_CHUNK, i / FEA_CHUNK) > 0u) {
                continue;
            }
            const int32_t surface = (int32_t)column->top + (int32_t)column->water;
            const int32_t iz = surface - box.origin_z;
            if (iz < 0 || iz + 1 >= (int32_t)LAYERS) {
                continue;
            }
            const uint16_t at = blocks[(uint64_t)i + (uint64_t)FEA_COLUMNS * (uint32_t)iz];
            const uint16_t over = blocks[(uint64_t)i + (uint64_t)FEA_COLUMNS * ((uint32_t)iz + 1u)];
            const float32_t *const rates = biome_features[column->biome];
            if (column->water > 0u) {
                const bool cold = rates[BIOME_FEATURE_ICE] > 0.0f32;
                const bool lily = over == BLOCK_ID_LILY_PAD;
                CHECK(at != BLOCK_ID_ICE || cold);
                if (cold) {
                    frozen += at == BLOCK_ID_ICE ? 1u : 0u;
                    /* Only a log or a sea plant could already have the surface voxel. */
                    CHECK(at == BLOCK_ID_ICE || at == BLOCK_ID_MANGROVE_LOG || at == BLOCK_ID_KELP ||
                          at == BLOCK_ID_SEAGRASS);
                } else {
                    open_water += at == BLOCK_ID_WATER ? 1u : 0u;
                }
                CHECK(!lily || !cold);
            } else if (rates[BIOME_FEATURE_SNOW] > 0.0f32 && column->block != BLOCK_ID_SNOW_BLOCK &&
                       column->block != BLOCK_ID_ICE && column->block != BLOCK_ID_PACKED_ICE &&
                       column->block != BLOCK_ID_BLUE_ICE && column->block != BLOCK_ID_LAVA) {
                snowy += over == BLOCK_ID_SNOW_BLOCK ? 1u : 0u;
                /* Snow unless something grew there first. */
                CHECK(over == BLOCK_ID_SNOW_BLOCK || over == BLOCK_ID_SPRUCE_LOG || over == BLOCK_ID_SPRUCE_LEAVES ||
                      over == BLOCK_ID_FERN || over == BLOCK_ID_PACKED_ICE);
            } else if (rates[BIOME_FEATURE_SNOW] <= 0.0f32) {
                CHECK(over != BLOCK_ID_SNOW_BLOCK);
            }
        }
    }
    printf("    ice on %u columns, open water on %u, new snow on %u\n", frozen, open_water, snowy);
    CHECK(frozen > 0u);
    CHECK(open_water > 0u);
    terrain_deinit(&terrain);
}

/* ---- The chain's ends --------------------------------------------------------------------------------------------- */

/* Only air, water and what grows may change, from what the stages before left, and only near the ground. Lakes aside, which do
 * cut. Above the world and below the bedrock nothing is written. */
TEST(only_what_is_open_changes_and_no_voxel_outside_the_world) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t voxels = (uint64_t)FEA_MAP_ACROSS * FEA_MAP_ACROSS * 220u;
    uint16_t *const before = test_alloc(t, voxels * sizeof(uint16_t));
    uint16_t *const after = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    const biome_id_t biomes[3] = {BIOME_ID_JUNGLE, BIOME_ID_SWAMP, BIOME_ID_FOREST};
    for (uint32_t b = 0u; b < 3u; b++) {
        const terrain_density_box_t box = fea_map_box(&places[biomes[b]]);
        fea_stages(&terrain, &box, opened, before);
        memcpy(after, before, voxels * sizeof(uint16_t));
        terrain_feature_fill_box(terrain.borrowed_feature, &box, after);
        const uint64_t layer = (uint64_t)box.count_x * box.count_y;
        uint64_t replaced = 0u;
        uint64_t changed = 0u;
        for (uint64_t i = 0u; i < voxels; i++) {
            if (before[i] != after[i]) {
                changed++;
                /* A lake cuts ground, and in the cold its top layer is ice with only sky over it. Where it is 1 deep, that is all of it. */
                const bool frozen_lake =
                    after[i] == BLOCK_ID_ICE && i + layer < voxels && after[i + layer] == BLOCK_ID_AIR;
                if (before[i] != BLOCK_ID_AIR && before[i] != BLOCK_ID_WATER && after[i] != BLOCK_ID_AIR &&
                    after[i] != BLOCK_ID_WATER && !frozen_lake) {
                    replaced++;
                }
            }
        }
        printf("    %" PRIu64 " voxels changed, %" PRIu64 " of solid to solid\n", changed, replaced);
        CHECK(changed > 0u);
        /* Nothing solid is ever turned to anything else solid, but for a lake's ice. */
        CHECK_EQ_U64(replaced, 0u);
    }
    /* Well above the world: all air, nothing written. */
    const terrain_density_box_t sky = {
        .origin_x = places[BIOME_ID_JUNGLE].chunk_x * (int32_t)FEA_CHUNK,
        .origin_y = places[BIOME_ID_JUNGLE].chunk_y * (int32_t)FEA_CHUNK,
        .origin_z = MC_WORLD_Z_MAX + 1,
        .count_x = FEA_CHUNK,
        .count_y = FEA_CHUNK,
        .count_z = 8u,
    };
    uint16_t *const high = test_alloc(t, (uint64_t)FEA_COLUMNS * 8u * sizeof(uint16_t));
    uint8_t *const high_opened = test_alloc(t, (uint64_t)FEA_COLUMNS * 8u);
    terrain_generate_box(&terrain, &sky, high_opened, high);
    for (uint32_t i = 0u; i < FEA_COLUMNS * 8u; i++) {
        CHECK(high[i] == BLOCK_ID_AIR);
    }
    terrain_deinit(&terrain);
}

/* Fractional tries come out as fractions. plains has 0.05 oaks a chunk: over a wide area, far more than none and far fewer than
 * one for every chunk. */
TEST(fractional_tries_come_out_as_fractions) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const fea_place_t *const place = &places[BIOME_ID_PLAINS];
    REQUIRE(place->found != 0u);
    enum : uint32_t { SIDE = 40u, HIGH = 160u };
    const uint64_t voxels = (uint64_t)FEA_COLUMNS * HIGH;
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    uint32_t chunks = 0u;
    uint32_t oaks = 0u;
    for (uint32_t cy = 0u; cy < SIDE; cy++) {
        for (uint32_t cx = 0u; cx < SIDE; cx++) {
            const int32_t chunk_x = place->chunk_x + (int32_t)cx;
            const int32_t chunk_y = place->chunk_y + (int32_t)cy;
            terrain_feature_column_t tops[FEA_COLUMNS];
            terrain_feature_chunk_columns(terrain.borrowed_feature, chunk_x, chunk_y, tops);
            uint32_t plains = 0u;
            for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                plains += tops[i].biome == BIOME_ID_PLAINS ? 1u : 0u;
            }
            if (plains != FEA_COLUMNS) {
                continue;
            }
            chunks++;
            const terrain_density_box_t box = {
                .origin_x = chunk_x * (int32_t)FEA_CHUNK,
                .origin_y = chunk_y * (int32_t)FEA_CHUNK,
                .origin_z = (int32_t)tops[0].top - 60,
                .count_x = FEA_CHUNK,
                .count_y = FEA_CHUNK,
                .count_z = HIGH,
            };
            terrain_generate_box(&terrain, &box, opened, blocks);
            for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                const int64_t iz = (int64_t)tops[i].top + 1 - (int64_t)box.origin_z;
                if (iz >= 0 && iz < (int64_t)HIGH &&
                    blocks[i + (uint64_t)FEA_COLUMNS * (uint64_t)iz] == BLOCK_ID_OAK_LOG) {
                    oaks++;
                }
            }
        }
    }
    printf("    %u oaks on %u chunks all of plains, asking for 0.05 a chunk\n", oaks, chunks);
    REQUIRE(chunks >= 100u);
    CHECK(oaks > 0u);
    CHECK(oaks < chunks / 4u);
    terrain_deinit(&terrain);
}

/* As the game will ask: one section of 16 layers at a time, against the whole column. A section far above the ground still has to
 * get the top of the tallest tree, and one under it the foot. Places with mixed trees, whose leaves overlap. */
TEST(section_by_section_is_the_whole_column) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t voxels = (uint64_t)FEA_COLUMNS * FEA_LAYERS;
    uint16_t *const whole = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    uint16_t section[FEA_COLUMNS * 16u];
    const biome_id_t biomes[4] = {BIOME_ID_JUNGLE, BIOME_ID_WINDSWEPT_FOREST, BIOME_ID_FOREST, BIOME_ID_JAGGED_PEAKS};
    for (uint32_t b = 0u; b < 4u; b++) {
        REQUIRE(places[biomes[b]].found != 0u);
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                const terrain_density_box_t box = {
                    .origin_x = (places[biomes[b]].chunk_x + dx) * (int32_t)FEA_CHUNK,
                    .origin_y = (places[biomes[b]].chunk_y + dy) * (int32_t)FEA_CHUNK,
                    .origin_z = MC_WORLD_Z_MIN,
                    .count_x = FEA_CHUNK,
                    .count_y = FEA_CHUNK,
                    .count_z = FEA_LAYERS,
                };
                terrain_generate_box(&terrain, &box, opened, whole);
                uint64_t wrong = 0u;
                /* Sections from the top down, so none is helped by the one before. */
                for (uint32_t s = FEA_LAYERS / 16u + 1u; s-- > 0u;) {
                    const uint32_t first = s * 16u;
                    if (first >= FEA_LAYERS) {
                        continue;
                    }
                    const uint32_t tall = FEA_LAYERS - first < 16u ? FEA_LAYERS - first : 16u;
                    const terrain_density_box_t part = {
                        .origin_x = box.origin_x,
                        .origin_y = box.origin_y,
                        .origin_z = box.origin_z + (int32_t)first,
                        .count_x = FEA_CHUNK,
                        .count_y = FEA_CHUNK,
                        .count_z = tall,
                    };
                    uint8_t part_opened[FEA_COLUMNS * 16u];
                    terrain_generate_box(&terrain, &part, part_opened, section);
                    for (uint32_t i = 0u; i < FEA_COLUMNS * tall; i++) {
                        wrong += section[i] != whole[(uint64_t)first * FEA_COLUMNS + i] ? 1u : 0u;
                    }
                }
                CHECK_EQ_U64(wrong, 0u);
            }
        }
    }
    terrain_deinit(&terrain);
}

/* A box of one layer, right at the top of what a chunk holds: the very tip of its tallest tree. All that a box can know of a tree
 * it has only the tip of is what the bounds let through, so a bound too low loses it. */
TEST(one_layer_at_the_tip_of_a_tree_is_the_same_voxels) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const uint64_t voxels = (uint64_t)FEA_COLUMNS * FEA_LAYERS;
    uint16_t *const whole = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, voxels);
    uint16_t one[FEA_COLUMNS];
    uint8_t one_opened[FEA_COLUMNS];
    const biome_id_t biomes[5] = {BIOME_ID_JUNGLE, BIOME_ID_SPARSE_JUNGLE, BIOME_ID_BAMBOO_JUNGLE, BIOME_ID_DARK_FOREST,
                                  BIOME_ID_MANGROVE_SWAMP};
    uint32_t tips = 0u;
    for (uint32_t b = 0u; b < 5u; b++) {
        REQUIRE(places[biomes[b]].found != 0u);
        for (int32_t dy = -2; dy <= 2; dy++) {
            for (int32_t dx = -2; dx <= 2; dx++) {
                const terrain_density_box_t box = {
                    .origin_x = (places[biomes[b]].chunk_x + dx) * (int32_t)FEA_CHUNK,
                    .origin_y = (places[biomes[b]].chunk_y + dy) * (int32_t)FEA_CHUNK,
                    .origin_z = MC_WORLD_Z_MIN,
                    .count_x = FEA_CHUNK,
                    .count_y = FEA_CHUNK,
                    .count_z = FEA_LAYERS,
                };
                terrain_generate_box(&terrain, &box, opened, whole);
                uint32_t highest = FEA_LAYERS;
                for (uint32_t z = FEA_LAYERS; z-- > 0u && highest == FEA_LAYERS;) {
                    for (uint32_t i = 0u; i < FEA_COLUMNS; i++) {
                        if (whole[i + FEA_COLUMNS * z] != BLOCK_ID_AIR) {
                            highest = z;
                            break;
                        }
                    }
                }
                REQUIRE(highest < FEA_LAYERS);
                const terrain_density_box_t tip = {
                    .origin_x = box.origin_x,
                    .origin_y = box.origin_y,
                    .origin_z = box.origin_z + (int32_t)highest,
                    .count_x = FEA_CHUNK,
                    .count_y = FEA_CHUNK,
                    .count_z = 1u,
                };
                terrain_generate_box(&terrain, &tip, one_opened, one);
                CHECK_MEM_EQ(one, &whole[(uint64_t)FEA_COLUMNS * highest], FEA_COLUMNS * sizeof(uint16_t));
                tips++;
            }
        }
    }
    printf("    %u tips\n", tips);
    terrain_deinit(&terrain);
}

/* A seed makes its world, and another makes another. */
TEST(another_seed_makes_another_world) {
    terrain_t a;
    terrain_t b;
    REQUIRE(terrain_init(&a, 1ull));
    REQUIRE(terrain_init(&b, 2ull));
    bool differ = false;
    for (uint32_t row = 0u; row < TERRAIN_FEATURE_ROWS; row++) {
        CHECK(a.borrowed_feature->row_seeds[row] != b.borrowed_feature->row_seeds[row]);
        differ = differ ||
                 a.borrowed_feature->row_seeds[row] != a.borrowed_feature->row_seeds[(row + 1u) % TERRAIN_FEATURE_ROWS];
    }
    CHECK(differ);
    CHECK(a.borrowed_feature->lake_seed != b.borrowed_feature->lake_seed);
    terrain_deinit(&a);
    terrain_deinit(&b);
}

/* A chunk walked slice by slice reads what does not change with the layers once. It has to lay what the whole fill lays. */
TEST(a_tile_read_once_lays_what_the_fill_lays) {
    enum : uint32_t { LAYERS = 16u, VOXELS = 256u * LAYERS, SLICES = 6u };
    uint16_t *const whole = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const tiled = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    terrain_surface_tile_t *const tile = test_alloc(t, sizeof(*tile));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, FEA_WORLD));
    fea_place_t *const places = test_alloc(t, BIOME_COUNT * sizeof(*places));
    fea_find_places(t, &terrain, places);
    const biome_id_t biomes[4] = {BIOME_ID_FOREST, BIOME_ID_BADLANDS, BIOME_ID_MANGROVE_SWAMP, BIOME_ID_JAGGED_PEAKS};
    uint64_t changed = 0u;
    for (uint32_t b = 0u; b < 4u; b++) {
        REQUIRE(places[biomes[b]].found != 0u);
        const int32_t x = places[biomes[b]].chunk_x * (int32_t)FEA_CHUNK;
        const int32_t y = places[biomes[b]].chunk_y * (int32_t)FEA_CHUNK;
        const int32_t ground = places[biomes[b]].ground;
        terrain_surface_tile_read(terrain.borrowed_surface, x, y, tile);
        /* From above the ground down through it, in the order a walk goes, the one tile used for them all. */
        for (uint32_t slice = 0u; slice < SLICES; slice++) {
            const terrain_density_box_t box = {
                .origin_x = x,
                .origin_y = y,
                .origin_z = ((ground >> 4) + 2 - (int32_t)slice) * (int32_t)LAYERS,
                .count_x = 16u,
                .count_y = 16u,
                .count_z = LAYERS,
            };
            terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, before);
            memcpy(whole, before, VOXELS * sizeof(uint16_t));
            memcpy(tiled, before, VOXELS * sizeof(uint16_t));
            terrain_surface_fill_box(terrain.borrowed_surface, &box, whole);
            terrain_surface_fill_tile_box(terrain.borrowed_surface, tile, &box, tiled);
            CHECK(memcmp(whole, tiled, VOXELS * sizeof(uint16_t)) == 0);
            for (uint32_t i = 0u; i < VOXELS; i++) {
                changed += before[i] != tiled[i] ? 1u : 0u;
            }
        }
    }
    CHECK(changed > 0u);
    terrain_deinit(&terrain);
}

TEST_MAIN()
