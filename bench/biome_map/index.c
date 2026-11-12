/* bench_biome_map: which biome holds what ground in a seed's world, with no display and no card. See README.md. */
#include "display/headless/png/index.h"
#include "game/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "terrain/index.h"
#include "thread/index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    BMAP_WORKERS = 16u,
    /* The same area and grid as bench_world_map, so one map lies over the other. */
    BMAP_STEP = 8u,
    BMAP_SIDE = 2048u,
    BMAP_PIXELS = BMAP_SIDE * BMAP_SIDE,
    BMAP_ACROSS_BLOCKS = BMAP_SIDE * BMAP_STEP,
    BMAP_PATH_BYTES = 4096u,
    /* Ground, in bins of this many blocks, from BMAP_BIN_FLOOR up, for its share of a biome. */
    BMAP_BIN_BLOCKS = 4u,
    BMAP_BINS = 512u,
    /* Distance to the sea, in bins of a step, up to the far side of the map. */
    BMAP_SEA_BINS = 4096u,
    BMAP_BANDS = 6u,
    BMAP_BAND_TOP = 4u, /* how many of a band's biomes are named */
};
static const int32_t BMAP_BIN_FLOOR = -250;
static const int32_t BMAP_BAND_FROM[BMAP_BANDS + 1u] = {-250, 150, 300, 500, 750, 1000, 1251};
static const float32_t BMAP_NO_SEA = 1e9f32;
static const float32_t BMAP_DIAGONAL = 1.41421356f32;

/* What each biome looks like on the map, by hand: a cold one blue-white, a dry one yellow, a wet one deep green. */
static const uint32_t BMAP_COLOUR[BIOME_COUNT] = {
    [BIOME_ID_BADLANDS] = 0xC8642Au,
    [BIOME_ID_BAMBOO_JUNGLE] = 0x38A818u,
    [BIOME_ID_BEACH] = 0xF0E4A0u,
    [BIOME_ID_BIRCH_FOREST] = 0x60B060u,
    [BIOME_ID_CHERRY_GROVE] = 0xF0A0C8u,
    [BIOME_ID_COLD_OCEAN] = 0x2850A0u,
    [BIOME_ID_DARK_FOREST] = 0x205020u,
    [BIOME_ID_DEEP_COLD_OCEAN] = 0x183468u,
    [BIOME_ID_DEEP_FROZEN_OCEAN] = 0x5078A8u,
    [BIOME_ID_DEEP_LUKEWARM_OCEAN] = 0x105878u,
    [BIOME_ID_DEEP_OCEAN] = 0x102860u,
    [BIOME_ID_DESERT] = 0xE8D478u,
    [BIOME_ID_ERODED_BADLANDS] = 0xE04818u,
    [BIOME_ID_FLOWER_FOREST] = 0x88C040u,
    [BIOME_ID_FOREST] = 0x388038u,
    [BIOME_ID_FROZEN_OCEAN] = 0x90B8E0u,
    [BIOME_ID_FROZEN_PEAKS] = 0xE0F0FFu,
    [BIOME_ID_FROZEN_RIVER] = 0xA0D8F0u,
    [BIOME_ID_GROVE] = 0x90B0A0u,
    [BIOME_ID_ICE_SPIKES] = 0xB0E8F8u,
    [BIOME_ID_JAGGED_PEAKS] = 0xF8F8F8u,
    [BIOME_ID_JUNGLE] = 0x208008u,
    [BIOME_ID_LUKEWARM_OCEAN] = 0x1880A8u,
    [BIOME_ID_MANGROVE_SWAMP] = 0x487038u,
    [BIOME_ID_MEADOW] = 0xA8D880u,
    [BIOME_ID_MUSHROOM_FIELDS] = 0xFF00FFu,
    [BIOME_ID_OCEAN] = 0x1840A0u,
    [BIOME_ID_OLD_GROWTH_BIRCH_FOREST] = 0x489848u,
    [BIOME_ID_OLD_GROWTH_PINE_TAIGA] = 0x386048u,
    [BIOME_ID_OLD_GROWTH_SPRUCE_TAIGA] = 0x305840u,
    [BIOME_ID_PLAINS] = 0x90C860u,
    [BIOME_ID_RIVER] = 0x20C8E8u,
    [BIOME_ID_SAVANNA] = 0xB8B048u,
    [BIOME_ID_SAVANNA_PLATEAU] = 0xA09838u,
    [BIOME_ID_SNOWY_BEACH] = 0xE8E8D8u,
    [BIOME_ID_SNOWY_PLAINS] = 0xD8E8E8u,
    [BIOME_ID_SNOWY_SLOPES] = 0xC8D8E8u,
    [BIOME_ID_SNOWY_TAIGA] = 0xA8C0C0u,
    [BIOME_ID_SPARSE_JUNGLE] = 0x58A030u,
    [BIOME_ID_STONY_PEAKS] = 0x888888u,
    [BIOME_ID_STONY_SHORE] = 0x686868u,
    [BIOME_ID_SUNFLOWER_PLAINS] = 0xC0D850u,
    [BIOME_ID_SWAMP] = 0x506830u,
    [BIOME_ID_TAIGA] = 0x487860u,
    [BIOME_ID_WARM_OCEAN] = 0x20A8B8u,
    [BIOME_ID_WINDSWEPT_FOREST] = 0x608870u,
    [BIOME_ID_WINDSWEPT_GRAVELLY_HILLS] = 0x909890u,
    [BIOME_ID_WINDSWEPT_HILLS] = 0x789880u,
    [BIOME_ID_WINDSWEPT_SAVANNA] = 0x989860u,
    [BIOME_ID_WOODED_BADLANDS] = 0x987038u,
};

/* What is shared with the workers. */
typedef struct bmap_run_t {
    const terrain_t *borrowed_terrain;     /* 0 */
    terrain_biome_column_t *borrowed_rows; /* 8: a row's per worker */
    terrain_biome_column_t *borrowed_map;  /* 16: BMAP_PIXELS */
    _Atomic uint32_t next;                 /* 24 */
    _Atomic uint32_t next_worker;          /* 28 */
    int32_t origin_x;                      /* 32: the map's first column */
    int32_t origin_y;                      /* 36 */
} bmap_run_t;
static_assert(sizeof(bmap_run_t) == 40);

/* A biome's numbers. */
typedef struct bmap_row_t {
    uint32_t columns;            /* 0 */
    uint32_t ground[BMAP_BINS];  /* 4 */
    uint32_t sea[BMAP_SEA_BINS]; /* 2052: how far from the sea, in steps */
} bmap_row_t;
static_assert(sizeof(bmap_row_t) == 4u + 4u * BMAP_BINS + 4u * BMAP_SEA_BINS);

/* Everything this program keeps, one region. */
typedef struct bmap_memory_t {
    const char *directory; /* 0 */
    float32_t *sea;        /* 8: BMAP_PIXELS: how far from the sea, in blocks */
    uint32_t *pixels;      /* 16: BMAP_PIXELS */
    uint8_t *stream;       /* 24 */
    uint64_t stream_bytes; /* 32 */
    bmap_row_t *rows;      /* 40: BIOME_COUNT */
} bmap_memory_t;
static_assert(sizeof(bmap_memory_t) == 48);

static void bmap_row_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    bmap_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    terrain_biome_column_t *const columns = run->borrowed_rows + (uint64_t)me * BMAP_SIDE;
    for (;;) {
        const uint32_t row = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (row >= BMAP_SIDE) {
            break;
        }
        const terrain_fields_area_t area = {
            .origin_x = run->origin_x,
            .origin_y = run->origin_y + (int32_t)(row * BMAP_STEP),
            .step_x = (int32_t)BMAP_STEP,
            .step_y = 1,
            .count_x = BMAP_SIDE,
            .count_y = 1u,
        };
        terrain_biome_columns_area(run->borrowed_terrain->borrowed_biome, nullptr, &area, columns);
        memcpy(run->borrowed_map + (uint64_t)row * BMAP_SIDE, columns, BMAP_SIDE * sizeof(terrain_biome_column_t));
    }
}

static bool bmap_is_sea(uint8_t biome) {
    return biome_defs[biome].where == BIOME_WHERE_OCEAN || biome_defs[biome].where == BIOME_WHERE_DEEP_OCEAN;
}

/* One step of a sweep: distance[at] from a neighbour, if that is nearer. */
static void bmap_nearer(float32_t *distance, uint32_t at, uint32_t from, float32_t cost) {
    const float32_t via = distance[from] + cost;
    distance[at] = via < distance[at] ? via : distance[at];
}

/* How far every column is from the sea, in blocks, by two sweeps, which read up to 8% long. */
static void bmap_sea_fill(const terrain_biome_column_t *map, float32_t *distance) {
    const float32_t straight = (float32_t)BMAP_STEP;
    const float32_t corner = straight * BMAP_DIAGONAL;
    for (uint32_t i = 0u; i < BMAP_PIXELS; i++) {
        distance[i] = bmap_is_sea(map[i].biome) ? 0.0f32 : BMAP_NO_SEA;
    }
    for (uint32_t y = 0u; y < BMAP_SIDE; y++) {
        for (uint32_t x = 0u; x < BMAP_SIDE; x++) {
            const uint32_t at = x + BMAP_SIDE * y;
            if (x > 0u) {
                bmap_nearer(distance, at, at - 1u, straight);
            }
            if (y > 0u) {
                bmap_nearer(distance, at, at - BMAP_SIDE, straight);
                if (x > 0u) {
                    bmap_nearer(distance, at, at - BMAP_SIDE - 1u, corner);
                }
                if (x + 1u < BMAP_SIDE) {
                    bmap_nearer(distance, at, at - BMAP_SIDE + 1u, corner);
                }
            }
        }
    }
    for (uint32_t y = BMAP_SIDE; y-- > 0u;) {
        for (uint32_t x = BMAP_SIDE; x-- > 0u;) {
            const uint32_t at = x + BMAP_SIDE * y;
            if (x + 1u < BMAP_SIDE) {
                bmap_nearer(distance, at, at + 1u, straight);
            }
            if (y + 1u < BMAP_SIDE) {
                bmap_nearer(distance, at, at + BMAP_SIDE, straight);
                if (x + 1u < BMAP_SIDE) {
                    bmap_nearer(distance, at, at + BMAP_SIDE + 1u, corner);
                }
                if (x > 0u) {
                    bmap_nearer(distance, at, at + BMAP_SIDE - 1u, corner);
                }
            }
        }
    }
}

static uint32_t bmap_shade(uint32_t colour, uint32_t percent) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        uint32_t channel = ((colour >> shift) & 0xFFu) * percent / 100u;
        channel = channel < 255u ? channel : 255u;
        out |= channel << shift;
    }
    return out;
}

/* A biome's colour, lit from the upper left, so that the land's shape shows through. */
static void bmap_draw(const terrain_biome_column_t *map, uint32_t *pixels) {
    for (uint32_t y = 0u; y < BMAP_SIDE; y++) {
        for (uint32_t x = 0u; x < BMAP_SIDE; x++) {
            const uint32_t at = x + BMAP_SIDE * y;
            float32_t rise = 0.0f32;
            if (x > 0u && y > 0u) {
                rise = map[at].ground - map[at - BMAP_SIDE - 1u].ground;
            }
            float32_t light = 100.0f32 + rise * 2.0f32;
            light = light < 55.0f32 ? 55.0f32 : light;
            light = light > 130.0f32 ? 130.0f32 : light;
            pixels[at] = bmap_shade(BMAP_COLOUR[map[at].biome], (uint32_t)light);
        }
    }
}

static bool bmap_png_write(const char *path, const bmap_memory_t *memory) {
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)memory->pixels, BMAP_SIDE * 4u, BMAP_SIDE, BMAP_SIDE, memory->stream,
                            memory->stream_bytes, &bytes)) {
        fprintf(stderr, "bench_biome_map: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_biome_map: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(memory->stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_biome_map: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

/* The lowest bin that this share of a histogram's weight lies at or under. */
static uint32_t bmap_percentile(const uint32_t *bins, uint32_t count, uint32_t total, uint32_t percent) {
    const uint64_t wanted = ((uint64_t)total * percent + 99u) / 100u;
    uint64_t seen = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        seen += bins[i];
        if (seen >= wanted) {
            return i;
        }
    }
    return count - 1u;
}

static int32_t bmap_ground_of_bin(uint32_t bin) {
    return BMAP_BIN_FLOOR + (int32_t)(bin * BMAP_BIN_BLOCKS);
}

static void bmap_measure(const terrain_biome_column_t *map, const float32_t *sea, bmap_row_t *rows) {
    memset(rows, 0, (uint64_t)BIOME_COUNT * sizeof(bmap_row_t));
    for (uint32_t i = 0u; i < BMAP_PIXELS; i++) {
        bmap_row_t *const row = &rows[map[i].biome];
        row->columns++;
        int32_t bin = ((int32_t)map[i].ground - BMAP_BIN_FLOOR) / (int32_t)BMAP_BIN_BLOCKS;
        bin = bin < 0 ? 0 : bin;
        bin = bin >= (int32_t)BMAP_BINS ? (int32_t)BMAP_BINS - 1 : bin;
        row->ground[bin]++;
        uint32_t steps = sea[i] >= BMAP_NO_SEA ? BMAP_SEA_BINS - 1u : (uint32_t)(sea[i] / (float32_t)BMAP_STEP);
        steps = steps < BMAP_SEA_BINS ? steps : BMAP_SEA_BINS - 1u;
        row->sea[steps]++;
    }
}

static void bmap_print_biomes(const bmap_row_t *rows) {
    printf("  %-26s %7s %7s  %6s %6s %6s  %8s %8s  %s\n", "biome", "map", "of land", "low", "typ", "high", "sea typ",
           "sea far", "colour");
    uint64_t land = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        if (!bmap_is_sea((uint8_t)b)) {
            land += rows[b].columns;
        }
    }
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        const bmap_row_t *const row = &rows[b];
        if (biome_defs[b].where == BIOME_WHERE_UNDERGROUND) {
            continue;
        }
        if (row->columns == 0u) {
            printf("  %-26s none on this map\n", biome_defs[b].name);
            continue;
        }
        printf("  %-26s %6.2f%% %6.2f%%  %6d %6d %6d  %8u %8u  #%06X\n", biome_defs[b].name,
               (libc_double_t)((float64_t)row->columns * 100.0f64 / (float64_t)BMAP_PIXELS),
               (libc_double_t)(bmap_is_sea((uint8_t)b) ? 0.0f64 : (float64_t)row->columns * 100.0f64 / (float64_t)land),
               bmap_ground_of_bin(bmap_percentile(row->ground, BMAP_BINS, row->columns, 10u)),
               bmap_ground_of_bin(bmap_percentile(row->ground, BMAP_BINS, row->columns, 50u)),
               bmap_ground_of_bin(bmap_percentile(row->ground, BMAP_BINS, row->columns, 90u)),
               bmap_percentile(row->sea, BMAP_SEA_BINS, row->columns, 50u) * BMAP_STEP,
               bmap_percentile(row->sea, BMAP_SEA_BINS, row->columns, 90u) * BMAP_STEP, BMAP_COLOUR[b]);
    }
}

/* Who holds each height of dry land: the question a taller world asks of a table written for a lower one. */
static void bmap_print_bands(const terrain_biome_column_t *map) {
    uint64_t whole = 0u;
    uint64_t wooded = 0u;
    for (uint32_t i = 0u; i < BMAP_PIXELS; i++) {
        if (biome_defs[map[i].biome].where == BIOME_WHERE_LAND) {
            whole++;
        }
    }
    for (uint32_t band = 0u; band < BMAP_BANDS; band++) {
        uint32_t counts[BIOME_COUNT] = {0u};
        uint64_t here = 0u;
        uint64_t trees = 0u;
        for (uint32_t i = 0u; i < BMAP_PIXELS; i++) {
            const biome_def_t *const def = &biome_defs[map[i].biome];
            const int32_t ground = (int32_t)map[i].ground;
            if (def->where != BIOME_WHERE_LAND || ground < BMAP_BAND_FROM[band] ||
                ground >= BMAP_BAND_FROM[band + 1u]) {
                continue;
            }
            counts[map[i].biome]++;
            here++;
            for (uint32_t tree = BIOME_FEATURE_TREE_OAK; tree <= BIOME_FEATURE_TREE_CHERRY; tree++) {
                if (biome_features[map[i].biome][tree] >= 1.0f32) {
                    trees++;
                    break;
                }
            }
        }
        wooded += trees;
        if (here == 0u) {
            continue;
        }
        printf("  z %5d to %5d: %5.1f%% of land, %5.1f%% of it wooded:", BMAP_BAND_FROM[band],
               BMAP_BAND_FROM[band + 1u] - 1, (libc_double_t)((float64_t)here * 100.0f64 / (float64_t)whole),
               (libc_double_t)((float64_t)trees * 100.0f64 / (float64_t)here));
        for (uint32_t rank = 0u; rank < BMAP_BAND_TOP; rank++) {
            uint32_t best = 0u;
            for (uint32_t b = 1u; b < BIOME_COUNT; b++) {
                best = counts[b] > counts[best] ? b : best;
            }
            if (counts[best] == 0u) {
                break;
            }
            printf(" %s %.0f%%", biome_defs[best].name,
                   (libc_double_t)((float64_t)counts[best] * 100.0f64 / (float64_t)here));
            counts[best] = 0u;
        }
        printf("\n");
    }
    printf("  %.1f%% of all land is in a biome with trees\n",
           (libc_double_t)((float64_t)wooded * 100.0f64 / (float64_t)whole));
}

static bool bmap_seed(job_system_t *system, bmap_run_t *run, const bmap_memory_t *memory, uint64_t seed) {
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return false;
    }
    run->borrowed_terrain = &terrain;
    run->origin_x = GAME_WORLD_MIDDLE - (int32_t)(BMAP_ACROSS_BLOCKS / 2u);
    run->origin_y = GAME_WORLD_MIDDLE - (int32_t)(BMAP_ACROSS_BLOCKS / 2u);
    run->origin_x -= run->origin_x % (int32_t)BMAP_STEP;
    run->origin_y -= run->origin_y % (int32_t)BMAP_STEP;
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < BMAP_WORKERS; i++) {
        job_submit(system, &group, bmap_row_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    terrain_deinit(&terrain);

    bmap_sea_fill(run->borrowed_map, memory->sea);
    bmap_measure(run->borrowed_map, memory->sea, memory->rows);
    bmap_draw(run->borrowed_map, memory->pixels);
    char path[BMAP_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_biomes.png", memory->directory, seed);
    const bool ok = bmap_png_write(path, memory);
    printf("seed %" PRIu64 ": %s\n", seed, path);
    bmap_print_biomes(memory->rows);
    bmap_print_bands(run->borrowed_map);
    return ok;
}

int32_t main(int32_t argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: bench_biome_map <directory for the pictures> [seed...]\n");
        return 2;
    }
    const char *const directory = argv[1];
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_biome_map: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    const uint32_t seeds = argc > 2 ? (uint32_t)(argc - 2) : 1u;

    const uint64_t rows_bytes = (uint64_t)BMAP_WORKERS * BMAP_SIDE * sizeof(terrain_biome_column_t);
    const uint64_t map_bytes = (uint64_t)BMAP_PIXELS * sizeof(terrain_biome_column_t);
    const uint64_t table_bytes = (uint64_t)BIOME_COUNT * sizeof(bmap_row_t);
    const uint64_t stream_bytes = display_png_bytes_needed(BMAP_SIDE, BMAP_SIDE);
    const uint64_t total = rows_bytes + map_bytes + table_bytes + (uint64_t)BMAP_PIXELS * 8u + stream_bytes + 4096u;
    heap_region_t memory_region = {0};
    if (!heap_reserve(total, &memory_region)) {
        return 2;
    }
    thread_topology_t topology;
    job_system_t system;
    if (!thread_topology_read(&topology) || !job_init(&system, &topology)) {
        heap_release(&memory_region);
        return 2;
    }

    uint8_t *cursor = memory_region.owned_base;
    bmap_run_t run = {0};
    run.borrowed_rows = (terrain_biome_column_t *)cursor;
    cursor += rows_bytes;
    run.borrowed_map = (terrain_biome_column_t *)cursor;
    cursor += map_bytes;
    bmap_memory_t memory = {.directory = directory, .stream_bytes = stream_bytes};
    memory.sea = (float32_t *)cursor;
    cursor += (uint64_t)BMAP_PIXELS * 4u;
    memory.pixels = (uint32_t *)cursor;
    cursor += (uint64_t)BMAP_PIXELS * 4u;
    memory.rows = (bmap_row_t *)cursor;
    cursor += table_bytes;
    memory.stream = cursor;

    int32_t status = 0;
    for (uint32_t s = 0u; s < seeds; s++) {
        const uint64_t seed = argc > 2 ? strtoull(argv[2 + s], nullptr, 0) : (uint64_t)TERRAIN_SEED_DEFAULT;
        if (!bmap_seed(&system, &run, &memory, seed)) {
            status = 1;
        }
    }
    job_deinit(&system);
    heap_release(&memory_region);
    return status;
}
