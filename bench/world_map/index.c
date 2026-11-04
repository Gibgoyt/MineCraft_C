/* bench_world_map: what a seed's world looks like and measures, with no display and no card. See README.md. */
#include "display/headless/png/index.h"
#include "game/index.h"
#include "game/spawn/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    MAP_WORKERS = 16u,
    /* The map: a column every this many blocks, this many a side. About 16 km by 16 km. */
    MAP_STEP = 8u,
    MAP_SIDE = 2048u,
    MAP_PIXELS = MAP_SIDE * MAP_SIDE,
    MAP_ACROSS_BLOCKS = MAP_SIDE * MAP_STEP,
    /* A whole column, which is what a worker's memory has room for. */
    MAP_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    /* Below what a column's fields say the ground is, what the first box asks for, and how much more each time that is not enough. */
    MAP_BAND_UNDER = 12u,
    MAP_BAND_OVER = 12u,
    MAP_BAND_WIDEN = 128u,
    /* One column in this many is also made whole, to hold the band's answer to the real one. A prime, so it does not fall into rows. */
    MAP_CHECK_EVERY = 4099u,
    /* Grid lines, in pixels: a line every 2048 blocks. */
    MAP_GRID_PIXELS = 256u,
    /* Cave tiles: this many a side over the map, each of this many chunks a side. */
    MAP_TILES_SIDE = 8u,
    MAP_TILES = MAP_TILES_SIDE * MAP_TILES_SIDE,
    MAP_TILE_CHUNKS_SIDE = 16u,
    MAP_TILE_CHUNKS = MAP_TILE_CHUNKS_SIDE * MAP_TILE_CHUNKS_SIDE,
    MAP_TILE_SIDE = MAP_TILE_CHUNKS_SIDE * TERRAIN_FEATURE_CHUNK,
    MAP_TILE_COLUMNS = MAP_TILE_SIDE * MAP_TILE_SIDE,
    MAP_CAVE_CHUNKS = MAP_TILES * MAP_TILE_CHUNKS,
    /* What a tile's rock takes: from the bottom of the world to the top of what terrain makes. */
    MAP_ROCK_LAYERS = (uint32_t)(TERRAIN_MADE_TOP - MC_WORLD_Z_MIN + 1),
    MAP_ROCK_VOXELS = MAP_TILE_COLUMNS * MAP_ROCK_LAYERS,
    /* A column open to the sky through at least this many cut voxels is part of a mouth. */
    MAP_MOUTH_DEPTH = 4u,
    /* A mouth this deep, somewhere in it, is a hole that goes somewhere, and one this deep is a real way down. */
    MAP_OPENING_DEEP = 8u,
    MAP_OPENING_DEEPER = 20u,
    /* The slice: this many blocks wide, one voxel a pixel, each worker's piece this wide. */
    MAP_SLICE_WIDE = 2048u,
    MAP_SLICE_PIECE = 256u,
    MAP_SLICE_PIECES = MAP_SLICE_WIDE / MAP_SLICE_PIECE,
    /* Sky kept over the highest ground a slice crosses, and the least it shows above the sea. */
    MAP_SLICE_SKY = 32u,
    MAP_SLICE_LEAST_TOP = 100u,
    /* A region under 0.1 km^2 is left out, at 64 m^2 a pixel, and one of 1 km^2 is a big one: a sea, or a range. */
    MAP_REGION_LEAST_PIXELS = 1563u,
    MAP_REGION_BIG_PIXELS = 15625u,
    /* Bins of the distances, a bin this many blocks, to the far corner of the map and past it. */
    MAP_DISTANCE_BIN = 64u,
    MAP_DISTANCE_BINS = 512u,
    MAP_PATH_BYTES = 512u,
    MAP_SEEDS_MAX = 64u,
    MAP_BANDS_REACH = 5u,
    MAP_CAVE_BANDS = 8u,
};
static_assert(MC_THREADS_WORKERS >= MAP_WORKERS);
static_assert(MAP_SLICE_WIDE % MAP_SLICE_PIECE == 0u);
static_assert(MAP_MOUTH_DEPTH <= MAP_OPENING_DEEP && MAP_OPENING_DEEP <= MAP_OPENING_DEEPER);

/* The land's height bands for +1250: a row of the table each. */
static const int32_t MAP_REACH[MAP_BANDS_REACH] = {150, 300, 500, 750, 1000};
/* What counts as a mountain, for the distances. */
static const int32_t MAP_MOUNTAIN_Z = 150;
/* Where each row of the caves' table starts, going up, and where the last ends. */
static const int32_t MAP_CAVE_BAND_FROM[MAP_CAVE_BANDS + 1u] = {-250, -200, -150, -100, -50, 0, 50, 150, 1251};

typedef enum map_class_t : uint8_t {
    MAP_CLASS_LAND = 0,
    MAP_CLASS_SEA = 1,
    MAP_CLASS_INLAND_WATER = 2,
} map_class_t;

/* A column, as its picture and its table want it. */
typedef struct map_column_t {
    int16_t ground;   /* 0: the highest z of ground or fluid, with what grows on it left out */
    uint16_t top;     /* 2: the block there */
    uint16_t depth;   /* 4: how much water or ice is on what is under it */
    uint8_t kind;     /* 6: terrain_hydrology_kind_t */
    uint8_t klass;    /* 7: map_class_t */
    uint8_t biome;    /* 8: biome_id_t */
    uint8_t _pad0[1]; /* 9 */
} map_column_t;
static_assert(sizeof(map_column_t) == 10);

typedef struct map_cave_counts_t {
    uint64_t opened[MAP_CAVE_BANDS];   /* 0: voxels a cave cut */
    uint64_t standing[MAP_CAVE_BANDS]; /* 64: voxels of rock left */
} map_cave_counts_t;
static_assert(sizeof(map_cave_counts_t) == 128);

/* What is shared with the workers. */
typedef struct map_run_t {
    const terrain_t *borrowed_terrain;           /* 0 */
    uint16_t *borrowed_blocks;                   /* 8: a whole chunk's per worker */
    uint8_t *borrowed_opened;                    /* 16: the same */
    terrain_hydrology_column_t *borrowed_fields; /* 24: a row's per worker, and after them the same of biomes */
    map_column_t *borrowed_columns;              /* 32: MAP_PIXELS */
    uint16_t *borrowed_mouths;       /* 40: MAP_TILES * MAP_TILE_COLUMNS: how deep each is open to the sky */
    uint16_t *borrowed_slice;        /* 48: MAP_SLICE_WIDE * MAP_LAYERS */
    uint8_t *borrowed_slice_opened;  /* 56: the same */
    _Atomic uint64_t checked;        /* 64 */
    _Atomic uint64_t check_differed; /* 72 */
    _Atomic uint64_t check_left_out; /* 80: ponds and ice spikes, which the map leaves out */
    _Atomic uint64_t land_columns;   /* 88: of the cave tiles */
    _Atomic uint64_t cave_opened[MAP_CAVE_BANDS];   /* 96 */
    _Atomic uint64_t cave_standing[MAP_CAVE_BANDS]; /* 160 */
    _Atomic uint32_t next;                          /* 224 */
    _Atomic uint32_t next_worker;                   /* 228 */
    int32_t origin_x;                               /* 232: the map's first column */
    int32_t origin_y;                               /* 236 */
    int32_t slice_x;                                /* 240: a slice's first column */
    int32_t slice_y;                                /* 244 */
    uint8_t *borrowed_rock;                         /* 248: MAP_ROCK_VOXELS per worker */
    uint32_t *borrowed_rock_stack;                  /* 256: the same */
    _Atomic uint64_t floating_pieces;               /* 264: of rock that touches nothing that holds it up */
    _Atomic uint64_t floating_voxels;               /* 272 */
    _Atomic uint64_t floating_largest;              /* 280 */
} map_run_t;
static_assert(sizeof(map_run_t) == 288);

/* Grown on the ground, not the ground: what a map of the land's shape looks through. */
static bool map_is_growth(uint16_t block) {
    switch (block) {
    case BLOCK_ID_AIR:
    case BLOCK_ID_OAK_LOG:
    case BLOCK_ID_BIRCH_LOG:
    case BLOCK_ID_SPRUCE_LOG:
    case BLOCK_ID_JUNGLE_LOG:
    case BLOCK_ID_ACACIA_LOG:
    case BLOCK_ID_DARK_OAK_LOG:
    case BLOCK_ID_MANGROVE_LOG:
    case BLOCK_ID_CHERRY_LOG:
    case BLOCK_ID_CACTUS:
    case BLOCK_ID_MELON:
    case BLOCK_ID_PUMPKIN:
        return true;
    case BLOCK_ID_WATER:
    case BLOCK_ID_LAVA:
        return false;
    default:
        break;
    }
    return (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u || (block_defs[block].flags & BLOCK_FLAG_DECAYS) != 0u;
}

static bool map_is_ice(uint16_t block) {
    return block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE || block == BLOCK_ID_BLUE_ICE;
}

/* The ground in a run of a column's blocks, from the top down. False if there is none, or the very top of the run is already ground and it may go on above. */
static bool map_column_read(const uint16_t *blocks, int32_t low, uint32_t layers, map_column_t *out) {
    int32_t at = (int32_t)layers - 1;
    while (at >= 0 && map_is_growth(blocks[at])) {
        at--;
    }
    if (at < 0 || at == (int32_t)layers - 1) {
        return false;
    }
    out->ground = (int16_t)(low + at);
    out->top = blocks[at];
    /* Ice on water is frozen water. Ice on rock is a cold peak's ground, or an ice spike's, and is land. */
    int32_t below = at;
    uint32_t depth = 0u;
    bool wet = false;
    while (below >= 0 && (blocks[below] == BLOCK_ID_WATER || map_is_ice(blocks[below]))) {
        wet = wet || blocks[below] == BLOCK_ID_WATER;
        depth++;
        below--;
    }
    /* A layer of water all frozen has no water left to show it was, but lies at the sea's level. */
    wet = wet || (blocks[at] == BLOCK_ID_ICE && low + at == MC_WORLD_Z_SEA);
    out->depth = wet ? (uint16_t)depth : 0u;
    return true;
}

/* Every stage that decides a column's ground, as terrain_generate_box runs them. Trees and ponds are left out here, and ore: terrain_feature_fill_box works out a chunk and the chunks round it for
 * each box, which over 4 million boxes of one column each is far too slow. So the feature stage's one effect on the whole of a sea or a snowfield is put in by hand from the same table: see
 * map_column_make. The check holds all of this to terrain_generate_box itself. */
static void map_stages_run(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                           uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    terrain_cave_cut_with_mask(box, opened, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    terrain_surface_line_caves_box(terrain->borrowed_surface, box, blocks);
}

/* A column's blocks, in a box only as tall as it needs. A voxel is the same whatever box it came in. */
static void map_column_make(const terrain_t *terrain, const terrain_hydrology_column_t *field, uint8_t biome, int32_t x,
                            int32_t y, uint16_t *blocks, uint8_t *opened, map_column_t *out) {
    const float32_t lowest_ground = field->ground < field->height ? field->ground : field->height;
    const float32_t highest_ground = field->height > 0.0f32 ? field->height : 0.0f32;
    int32_t low = (int32_t)floorf(lowest_ground - field->softness) - (int32_t)MAP_BAND_UNDER;
    int32_t high = (int32_t)ceilf(highest_ground + field->softness) + (int32_t)MAP_BAND_OVER;
    low = low < COLUMN_Z_STORAGE_MIN ? COLUMN_Z_STORAGE_MIN : low;
    high = high > COLUMN_Z_STORAGE_END - 1 ? COLUMN_Z_STORAGE_END - 1 : high;
    for (;;) {
        const terrain_density_box_t box = {
            .origin_x = x,
            .origin_y = y,
            .origin_z = low,
            .count_x = 1u,
            .count_y = 1u,
            .count_z = (uint32_t)(high - low + 1),
        };
        map_stages_run(terrain, &box, opened, blocks);
        if (map_column_read(blocks, low, box.count_z, out)) {
            break;
        }
        if (low == COLUMN_Z_STORAGE_MIN && high == COLUMN_Z_STORAGE_END - 1) {
            /* Nothing at all in a whole column can't be, as bedrock is always there. */
            out->ground = (int16_t)COLUMN_Z_STORAGE_MIN;
            out->top = BLOCK_ID_AIR;
            out->depth = 0u;
            return;
        }
        low -= (int32_t)MAP_BAND_WIDEN;
        low = low < COLUMN_Z_STORAGE_MIN ? COLUMN_Z_STORAGE_MIN : low;
        high = COLUMN_Z_STORAGE_END - 1;
    }
    /* What feature_cold_paint does: ice on a cold biome's water, and snow on its land. Only the ground's height and the sea's look matter here. */
    if (out->top == BLOCK_ID_WATER && biome_features[biome][BIOME_FEATURE_ICE] > 0.0f32) {
        out->top = BLOCK_ID_ICE;
    }
}

static map_class_t map_class_of(const map_column_t *column) {
    if (column->depth == 0u) {
        return MAP_CLASS_LAND;
    }
    const bool inland = column->kind == (uint8_t)TERRAIN_HYDROLOGY_KIND_RIVER ||
                        column->kind == (uint8_t)TERRAIN_HYDROLOGY_KIND_LAKE || column->ground != MC_WORLD_Z_SEA;
    return inland ? MAP_CLASS_INLAND_WATER : MAP_CLASS_SEA;
}

static void map_row_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    map_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    uint16_t *const blocks = run->borrowed_blocks + (uint64_t)me * TERRAIN_FEATURE_CHUNK_COLUMNS * MAP_LAYERS;
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * TERRAIN_FEATURE_CHUNK_COLUMNS * MAP_LAYERS;
    terrain_hydrology_column_t *const fields = run->borrowed_fields + (uint64_t)me * MAP_SIDE;
    terrain_biome_column_t *const biomes =
        (terrain_biome_column_t *)(run->borrowed_fields + (uint64_t)MAP_WORKERS * MAP_SIDE) + (uint64_t)me * MAP_SIDE;
    uint64_t checked = 0u, differed = 0u, left_out = 0u;
    for (;;) {
        const uint32_t row = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (row >= MAP_SIDE) {
            break;
        }
        const terrain_climate_area_t area = {
            .origin_x = run->origin_x,
            .origin_y = run->origin_y + (int32_t)(row * MAP_STEP),
            .step_x = (int32_t)MAP_STEP,
            .step_y = 1,
            .count_x = MAP_SIDE,
            .count_y = 1u,
        };
        terrain_hydrology_columns_area(run->borrowed_terrain->borrowed_hydrology, &area, fields);
        terrain_biome_columns_area(run->borrowed_terrain->borrowed_biome, &area, biomes);
        for (uint32_t i = 0u; i < MAP_SIDE; i++) {
            const int32_t x = area.origin_x + (int32_t)(i * MAP_STEP);
            const uint64_t at = (uint64_t)i + (uint64_t)MAP_SIDE * row;
            map_column_t *const column = &run->borrowed_columns[at];
            map_column_make(run->borrowed_terrain, &fields[i], biomes[i].biome, x, area.origin_y, blocks, opened,
                            column);
            column->kind = (uint8_t)fields[i].kind;
            column->klass = (uint8_t)map_class_of(column);
            column->biome = biomes[i].biome;
            if (at % MAP_CHECK_EVERY == 0u) {
                const terrain_density_box_t whole = {
                    .origin_x = x,
                    .origin_y = area.origin_y,
                    .origin_z = COLUMN_Z_STORAGE_MIN,
                    .count_x = 1u,
                    .count_y = 1u,
                    .count_z = MAP_LAYERS,
                };
                terrain_generate_box(run->borrowed_terrain, &whole, opened, blocks);
                map_column_t real = {.kind = column->kind};
                const bool found = map_column_read(blocks, COLUMN_Z_STORAGE_MIN, MAP_LAYERS, &real);
                real.klass = (uint8_t)map_class_of(&real);
                checked++;
                const int32_t gap = (int32_t)real.ground - (int32_t)column->ground;
                const bool same_ice = map_is_ice(real.top) == map_is_ice(column->top);
                if (found && real.klass == column->klass && same_ice && gap >= -1 && gap <= 1) {
                    /* Snow is the 1. What grows in water, a little under the top of it, changes only the depth. */
                } else if (found && (map_is_ice(real.top) ||
                                     (real.klass == (uint8_t)MAP_CLASS_INLAND_WATER && real.ground > MC_WORLD_Z_SEA))) {
                    left_out++; /* an ice spike, an iceberg, a frozen pond or a pond: the feature stage's, on purpose */
                } else {
                    differed++;
                    printf("  check: at %d, %d the map says %d %s %u deep, whole says %d %s %u deep\n", x,
                           area.origin_y, column->ground, block_defs[column->top].name, column->depth, real.ground,
                           block_defs[real.top].name, real.depth);
                }
            }
        }
    }
    atomic_fetch_add_explicit(&run->checked, checked, memory_order_relaxed);
    atomic_fetch_add_explicit(&run->check_differed, differed, memory_order_relaxed);
    atomic_fetch_add_explicit(&run->check_left_out, left_out, memory_order_relaxed);
}

/* A tile's first column, in tile units of the map: evenly over it, each in the middle of its share. */
static int32_t map_tile_origin(int32_t map_origin, uint32_t tile_along) {
    const uint32_t share = MAP_ACROSS_BLOCKS / MAP_TILES_SIDE;
    return map_origin + (int32_t)(tile_along * share + (share - MAP_TILE_SIDE) / 2u);
}

/* What holds itself up: not what grows, or a fluid, or ice and snow, which lie on whatever is under them. */
static bool map_is_rock(uint16_t block) {
    return block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA && block != BLOCK_ID_SNOW_BLOCK && !map_is_ice(block) &&
           !map_is_growth(block);
}

/* From what is on stack to the end of what joins it, by faces. A voxel is 2 once it is reached. Returns how many were on it, or joined it. */
static uint64_t map_rock_flood(uint8_t *rock, uint32_t *stack, uint32_t on_stack) {
    uint64_t reached = on_stack;
    const uint32_t layer = MAP_TILE_COLUMNS;
    while (on_stack > 0u) {
        const uint32_t at = stack[--on_stack];
        const uint32_t column = at % layer;
        const uint32_t x = column % MAP_TILE_SIDE, y = column / MAP_TILE_SIDE, z = at / layer;
        const uint32_t next_to[6] = {
            x > 0u ? at - 1u : at,
            x + 1u < MAP_TILE_SIDE ? at + 1u : at,
            y > 0u ? at - MAP_TILE_SIDE : at,
            y + 1u < MAP_TILE_SIDE ? at + MAP_TILE_SIDE : at,
            z > 0u ? at - layer : at,
            z + 1u < MAP_ROCK_LAYERS ? at + layer : at,
        };
        for (uint32_t i = 0u; i < 6u; i++) {
            if (rock[next_to[i]] == 1u) {
                rock[next_to[i]] = 2u;
                stack[on_stack++] = next_to[i];
                reached++;
            }
        }
    }
    return reached;
}

/* Rock is held up if it joins the bedrock, or a side of the tile, which it may go on past. What is left floats. */
static void map_rock_measure(map_run_t *run, uint8_t *rock, uint32_t *stack) {
    const uint32_t layer = MAP_TILE_COLUMNS;
    uint32_t on_stack = 0u;
    for (uint32_t at = 0u; at < MAP_ROCK_VOXELS; at++) {
        if (rock[at] != 1u) {
            continue;
        }
        const uint32_t column = at % layer;
        const uint32_t x = column % MAP_TILE_SIDE, y = column / MAP_TILE_SIDE;
        if (at < layer || x == 0u || y == 0u || x + 1u == MAP_TILE_SIDE || y + 1u == MAP_TILE_SIDE) {
            rock[at] = 2u;
            stack[on_stack++] = at;
        }
    }
    (void)map_rock_flood(rock, stack, on_stack);
    uint64_t pieces = 0u, voxels = 0u, largest = 0u;
    for (uint32_t at = 0u; at < MAP_ROCK_VOXELS; at++) {
        if (rock[at] != 1u) {
            continue;
        }
        rock[at] = 2u;
        stack[0] = at;
        const uint64_t size = map_rock_flood(rock, stack, 1u);
        pieces++;
        voxels += size;
        largest = size > largest ? size : largest;
    }
    atomic_fetch_add_explicit(&run->floating_pieces, pieces, memory_order_relaxed);
    atomic_fetch_add_explicit(&run->floating_voxels, voxels, memory_order_relaxed);
    uint64_t seen = atomic_load_explicit(&run->floating_largest, memory_order_relaxed);
    while (largest > seen && !atomic_compare_exchange_weak_explicit(&run->floating_largest, &seen, largest,
                                                                    memory_order_relaxed, memory_order_relaxed)) {
    }
}

/* Whole tiles of whole chunks, every stage: what is open to the sky, what caves have cut, and what floats. */
static void map_cave_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    map_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    const uint64_t chunk_voxels = (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * MAP_LAYERS;
    uint16_t *const blocks = run->borrowed_blocks + (uint64_t)me * chunk_voxels;
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * chunk_voxels;
    uint8_t *const rock = run->borrowed_rock + (uint64_t)me * MAP_ROCK_VOXELS;
    uint32_t *const rock_stack = run->borrowed_rock_stack + (uint64_t)me * MAP_ROCK_VOXELS;
    uint64_t land = 0u;
    map_cave_counts_t counts = {0};
    for (;;) {
        const uint32_t tile = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (tile >= MAP_TILES) {
            break;
        }
        for (uint32_t chunk = 0u; chunk < MAP_TILE_CHUNKS; chunk++) {
            const uint32_t tile_x = (chunk % MAP_TILE_CHUNKS_SIDE) * TERRAIN_FEATURE_CHUNK;
            const uint32_t tile_y = (chunk / MAP_TILE_CHUNKS_SIDE) * TERRAIN_FEATURE_CHUNK;
            const terrain_density_box_t box = {
                .origin_x = map_tile_origin(run->origin_x, tile % MAP_TILES_SIDE) + (int32_t)tile_x,
                .origin_y = map_tile_origin(run->origin_y, tile / MAP_TILES_SIDE) + (int32_t)tile_y,
                .origin_z = COLUMN_Z_STORAGE_MIN,
                .count_x = TERRAIN_FEATURE_CHUNK,
                .count_y = TERRAIN_FEATURE_CHUNK,
                .count_z = MAP_LAYERS,
            };
            terrain_generate_box(run->borrowed_terrain, &box, opened, blocks);
            for (uint32_t column = 0u; column < TERRAIN_FEATURE_CHUNK_COLUMNS; column++) {
                /* Down from the sky, through what is not ground, counting what a cave cut. */
                uint32_t layer = MAP_LAYERS;
                uint32_t open_to_sky = 0u;
                while (layer > 0u) {
                    const uint64_t voxel = (uint64_t)column + (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * (layer - 1u);
                    if (!map_is_growth(blocks[voxel])) {
                        break;
                    }
                    open_to_sky += opened[voxel] != 0u ? 1u : 0u;
                    layer--;
                }
                const uint16_t top =
                    layer > 0u ? blocks[(uint64_t)column + (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * (layer - 1u)]
                               : (uint16_t)BLOCK_ID_AIR;
                land += top != BLOCK_ID_WATER && !map_is_ice(top) ? 1u : 0u;
                const uint32_t ox = column % TERRAIN_FEATURE_CHUNK;
                const uint32_t oy = column / TERRAIN_FEATURE_CHUNK;
                const uint32_t in_tile = tile_x + ox + MAP_TILE_SIDE * (tile_y + oy);
                run->borrowed_mouths[(uint64_t)tile * MAP_TILE_COLUMNS + in_tile] =
                    open_to_sky >= MAP_MOUTH_DEPTH ? (uint16_t)open_to_sky : 0u;
                /* How much of the rock each band of height has lost, and all of it for what floats. */
                uint32_t band = 0u;
                for (uint32_t l = 0u; l < MAP_LAYERS; l++) {
                    const int32_t z = COLUMN_Z_STORAGE_MIN + (int32_t)l;
                    if (z < MC_WORLD_Z_MIN) {
                        continue;
                    }
                    if (z > TERRAIN_MADE_TOP) {
                        break;
                    }
                    while (z >= MAP_CAVE_BAND_FROM[band + 1u]) {
                        band++;
                    }
                    const uint64_t voxel = (uint64_t)column + (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * l;
                    const bool is_rock = map_is_rock(blocks[voxel]);
                    if (opened[voxel] != 0u) {
                        counts.opened[band]++;
                    } else if (is_rock) {
                        counts.standing[band]++;
                    }
                    rock[in_tile + MAP_TILE_COLUMNS * (uint32_t)(z - MC_WORLD_Z_MIN)] = is_rock ? 1u : 0u;
                }
            }
        }
        map_rock_measure(run, rock, rock_stack);
    }
    atomic_fetch_add_explicit(&run->land_columns, land, memory_order_relaxed);
    for (uint32_t band = 0u; band < MAP_CAVE_BANDS; band++) {
        atomic_fetch_add_explicit(&run->cave_opened[band], counts.opened[band], memory_order_relaxed);
        atomic_fetch_add_explicit(&run->cave_standing[band], counts.standing[band], memory_order_relaxed);
    }
}

static void map_slice_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    map_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    const uint64_t chunk_voxels = (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * MAP_LAYERS;
    uint16_t *const blocks = run->borrowed_blocks + (uint64_t)me * chunk_voxels;
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * chunk_voxels;
    for (;;) {
        const uint32_t piece = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (piece >= MAP_SLICE_PIECES) {
            return;
        }
        const terrain_density_box_t box = {
            .origin_x = run->slice_x + (int32_t)(piece * MAP_SLICE_PIECE),
            .origin_y = run->slice_y,
            .origin_z = COLUMN_Z_STORAGE_MIN,
            .count_x = MAP_SLICE_PIECE,
            .count_y = 1u,
            .count_z = MAP_LAYERS,
        };
        terrain_generate_box(run->borrowed_terrain, &box, opened, blocks);
        for (uint32_t layer = 0u; layer < MAP_LAYERS; layer++) {
            const uint64_t to = (uint64_t)piece * MAP_SLICE_PIECE + (uint64_t)MAP_SLICE_WIDE * layer;
            const uint64_t from = (uint64_t)MAP_SLICE_PIECE * layer;
            memcpy(&run->borrowed_slice[to], &blocks[from], MAP_SLICE_PIECE * sizeof(uint16_t));
            memcpy(&run->borrowed_slice_opened[to], &opened[from], MAP_SLICE_PIECE);
        }
    }
}

static void map_run_jobs(job_system_t *system, map_run_t *run, job_function_t function) {
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < MAP_WORKERS; i++) {
        job_submit(system, &group, function, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
}

/* ---- Regions and distances ----------------------------------------------------------------------------------------- */

/* One region of mask from start, 8-connected, side by side pixels. Its pixels go into queue, and each is 2 in mask from then on. Returns how many. */
static uint32_t map_flood(uint8_t *mask, uint32_t side, uint32_t start, uint32_t *queue) {
    uint32_t head = 0u, tail = 0u;
    queue[tail++] = start;
    mask[start] = 2u;
    while (head < tail) {
        const uint32_t at = queue[head++];
        const uint32_t x = at % side;
        const uint32_t y = at / side;
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                const int32_t nx = (int32_t)x + dx, ny = (int32_t)y + dy;
                if (nx < 0 || ny < 0 || nx >= (int32_t)side || ny >= (int32_t)side) {
                    continue;
                }
                const uint32_t near = (uint32_t)nx + side * (uint32_t)ny;
                if (mask[near] == 1u) {
                    mask[near] = 2u;
                    queue[tail++] = near;
                }
            }
        }
    }
    return tail;
}

typedef struct map_regions_t {
    uint32_t count;          /* 0: of at least MAP_REGION_LEAST_PIXELS */
    uint32_t big;            /* 4: of at least MAP_REGION_BIG_PIXELS */
    uint32_t largest_pixels; /* 8 */
    uint32_t largest_extent; /* 12: the longer side of its box, in pixels */
    uint32_t largest_edges;  /* 16: how many of the map's four sides it reaches */
    uint8_t _pad0[4];        /* 20 */
    uint64_t kept_pixels;    /* 24: in all of those */
} map_regions_t;
static_assert(sizeof(map_regions_t) == 32);

/* Where mask is 1 are the pixels of interest. After this, mask is 2 at those of a region big enough to count, and 0 everywhere else. */
static void map_regions_find(uint8_t *mask, uint32_t *queue, map_regions_t *out) {
    memset(out, 0, sizeof(*out));
    for (uint32_t start = 0u; start < MAP_PIXELS; start++) {
        if (mask[start] != 1u) {
            continue;
        }
        const uint32_t pixels = map_flood(mask, MAP_SIDE, start, queue);
        if (pixels < MAP_REGION_LEAST_PIXELS) {
            for (uint32_t i = 0u; i < pixels; i++) {
                mask[queue[i]] = 3u;
            }
            continue;
        }
        out->count++;
        out->big += pixels >= MAP_REGION_BIG_PIXELS ? 1u : 0u;
        out->kept_pixels += pixels;
        if (pixels > out->largest_pixels) {
            uint32_t left = MAP_SIDE, right = 0u, up = MAP_SIDE, down = 0u;
            for (uint32_t i = 0u; i < pixels; i++) {
                const uint32_t x = queue[i] % MAP_SIDE, y = queue[i] / MAP_SIDE;
                left = x < left ? x : left;
                right = x > right ? x : right;
                up = y < up ? y : up;
                down = y > down ? y : down;
            }
            out->largest_pixels = pixels;
            out->largest_extent = (right - left > down - up ? right - left : down - up) + 1u;
            out->largest_edges = (left == 0u ? 1u : 0u) + (up == 0u ? 1u : 0u) + (right == MAP_SIDE - 1u ? 1u : 0u) +
                                 (down == MAP_SIDE - 1u ? 1u : 0u);
        }
    }
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        mask[i] = mask[i] == 3u ? 0u : mask[i];
    }
}

/* Each pixel's distance in blocks to the nearest pixel that mask has at 2, by two sweeps of steps of 1 and of the square root of 2: up to 8% over the straight line, which is all
 * a "typical distance" needs. With none, all are at least MAP_DISTANCE_NONE. */
#define MAP_DISTANCE_NONE 1.0e9f32
static void map_distance_fill(const uint8_t *mask, float32_t *distance) {
    const float32_t straight = (float32_t)MAP_STEP;
    const float32_t diagonal = (float32_t)MAP_STEP * 1.41421356f32;
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        distance[i] = mask[i] == 2u ? 0.0f32 : MAP_DISTANCE_NONE;
    }
    for (uint32_t y = 0u; y < MAP_SIDE; y++) {
        for (uint32_t x = 0u; x < MAP_SIDE; x++) {
            const uint32_t at = x + MAP_SIDE * y;
            float32_t best = distance[at];
            if (x > 0u) {
                const float32_t via = distance[at - 1u] + straight;
                best = via < best ? via : best;
            }
            if (y > 0u) {
                const float32_t via = distance[at - MAP_SIDE] + straight;
                best = via < best ? via : best;
                if (x > 0u) {
                    const float32_t corner = distance[at - MAP_SIDE - 1u] + diagonal;
                    best = corner < best ? corner : best;
                }
                if (x + 1u < MAP_SIDE) {
                    const float32_t corner = distance[at - MAP_SIDE + 1u] + diagonal;
                    best = corner < best ? corner : best;
                }
            }
            distance[at] = best;
        }
    }
    for (uint32_t y = MAP_SIDE; y-- > 0u;) {
        for (uint32_t x = MAP_SIDE; x-- > 0u;) {
            const uint32_t at = x + MAP_SIDE * y;
            float32_t best = distance[at];
            if (x + 1u < MAP_SIDE) {
                const float32_t via = distance[at + 1u] + straight;
                best = via < best ? via : best;
            }
            if (y + 1u < MAP_SIDE) {
                const float32_t via = distance[at + MAP_SIDE] + straight;
                best = via < best ? via : best;
                if (x + 1u < MAP_SIDE) {
                    const float32_t corner = distance[at + MAP_SIDE + 1u] + diagonal;
                    best = corner < best ? corner : best;
                }
                if (x > 0u) {
                    const float32_t corner = distance[at + MAP_SIDE - 1u] + diagonal;
                    best = corner < best ? corner : best;
                }
            }
            distance[at] = best;
        }
    }
}

/* ---- Pictures ------------------------------------------------------------------------------------------------------ */

typedef struct map_ramp_stop_t {
    int32_t z;       /* 0 */
    uint32_t colour; /* 4 */
} map_ramp_stop_t;
static_assert(sizeof(map_ramp_stop_t) == 8);

/* Land by height, with the bands the table has at the points where it changes: green, to yellow at 150, to orange at 300, to brown at 500, to grey at 750, to white at the top. */
static const map_ramp_stop_t MAP_RAMP[] = {
    {-250, 0x305020u}, {0, 0x4C9A3Cu},   {60, 0x8CC468u},   {150, 0xE8DC70u},  {300, 0xD89038u},
    {500, 0x8C5A32u},  {750, 0x8A8A8Au}, {1000, 0xD8D8D8u}, {1250, 0xFFFFFFu},
};
enum : uint32_t { MAP_RAMP_STOPS = sizeof(MAP_RAMP) / sizeof(MAP_RAMP[0]) };

static uint32_t map_mix(uint32_t a, uint32_t b, uint32_t over, uint32_t of) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        const uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca * (of - over) + cb * over) / of) << shift;
    }
    return out;
}

static uint32_t map_shade(uint32_t colour, uint32_t percent) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        uint32_t channel = ((colour >> shift) & 0xFFu) * percent / 100u;
        channel = channel < 255u ? channel : 255u;
        out |= channel << shift;
    }
    return out;
}

static uint32_t map_land_colour(int32_t z) {
    if (z <= MAP_RAMP[0].z) {
        return MAP_RAMP[0].colour;
    }
    for (uint32_t i = 1u; i < MAP_RAMP_STOPS; i++) {
        if (z <= MAP_RAMP[i].z) {
            return map_mix(MAP_RAMP[i - 1u].colour, MAP_RAMP[i].colour, (uint32_t)(z - MAP_RAMP[i - 1u].z),
                           (uint32_t)(MAP_RAMP[i].z - MAP_RAMP[i - 1u].z));
        }
    }
    return MAP_RAMP[MAP_RAMP_STOPS - 1u].colour;
}

static const uint32_t MAP_COLOUR_DEEP = 0x0C2460u;
static const uint32_t MAP_COLOUR_SHALLOW = 0x2F78D8u;
static const uint32_t MAP_COLOUR_INLAND = 0x20C8E8u;
static const uint32_t MAP_COLOUR_ICE = 0xB8ECF4u;
static const uint32_t MAP_COLOUR_ICE_HATCH = 0x70B8E0u;
static const uint32_t MAP_COLOUR_LAVA = 0xFF4000u;
static const uint32_t MAP_COLOUR_SPAWN = 0xFF00FFu;
static const uint32_t MAP_COLOUR_MOUNTAIN = 0xFF0000u;

static void map_mark(uint32_t *pixels, int32_t px, int32_t py, uint32_t colour) {
    for (int32_t d = -14; d <= 14; d++) {
        for (int32_t w = -1; w <= 1; w++) {
            const int32_t xs[2] = {px + d, px + w};
            const int32_t ys[2] = {py + w, py + d};
            for (uint32_t k = 0u; k < 2u; k++) {
                if (xs[k] >= 0 && ys[k] >= 0 && xs[k] < (int32_t)MAP_SIDE && ys[k] < (int32_t)MAP_SIDE) {
                    pixels[(uint32_t)xs[k] + MAP_SIDE * (uint32_t)ys[k]] = colour;
                }
            }
        }
    }
}

static void map_draw(const map_column_t *columns, uint32_t *pixels) {
    for (uint32_t y = 0u; y < MAP_SIDE; y++) {
        for (uint32_t x = 0u; x < MAP_SIDE; x++) {
            const uint32_t at = x + MAP_SIDE * y;
            const map_column_t *const column = &columns[at];
            uint32_t colour;
            if (map_is_ice(column->top) && column->depth > 0u) {
                colour = (x + y) % 16u < 2u ? MAP_COLOUR_ICE_HATCH : MAP_COLOUR_ICE;
            } else if (column->top == BLOCK_ID_LAVA) {
                colour = MAP_COLOUR_LAVA;
            } else if (column->klass == (uint8_t)MAP_CLASS_INLAND_WATER) {
                colour = MAP_COLOUR_INLAND;
            } else if (column->klass == (uint8_t)MAP_CLASS_SEA) {
                const uint32_t depth = column->depth < 100u ? column->depth : 100u;
                colour = map_mix(MAP_COLOUR_SHALLOW, MAP_COLOUR_DEEP, depth, 100u);
            } else {
                const int32_t ground = column->ground;
                /* Lit from the low x and low y, so a slope facing there is bright. */
                const int32_t west = x > 0u ? columns[at - 1u].ground : ground;
                const int32_t north = y > 0u ? columns[at - MAP_SIDE].ground : ground;
                int32_t percent = 100 + 2 * (west + north - 2 * ground);
                percent = percent < 55 ? 55 : (percent > 140 ? 140 : percent);
                colour = map_shade(map_land_colour(ground), (uint32_t)percent);
                /* A thin dark line where a height band ends. */
                const int32_t east = x + 1u < MAP_SIDE ? columns[at + 1u].ground : ground;
                const int32_t south = y + 1u < MAP_SIDE ? columns[at + MAP_SIDE].ground : ground;
                for (uint32_t c = 0u; c < MAP_BANDS_REACH; c++) {
                    const bool here = ground >= MAP_REACH[c];
                    if (here != (east >= MAP_REACH[c]) || here != (south >= MAP_REACH[c])) {
                        colour = map_shade(colour, 45u);
                        break;
                    }
                }
            }
            const bool axis = x == MAP_SIDE / 2u || y == MAP_SIDE / 2u;
            if (axis) {
                colour = map_shade(colour, 60u);
            } else if (x % MAP_GRID_PIXELS == 0u || y % MAP_GRID_PIXELS == 0u) {
                colour = map_shade(colour, 80u);
            }
            pixels[at] = colour;
        }
    }
}

static uint32_t map_slice_colour(uint16_t block, uint8_t opened) {
    switch (block) {
    case BLOCK_ID_AIR:
        return opened != 0u ? 0x101010u : 0xA8D0F0u;
    case BLOCK_ID_WATER:
        return opened != 0u ? 0x103890u : 0x2F68D8u;
    case BLOCK_ID_LAVA:
        return 0xFF5000u;
    case BLOCK_ID_BEDROCK:
        return 0x202020u;
    case BLOCK_ID_DEEPSLATE:
    case BLOCK_ID_COBBLED_DEEPSLATE:
    case BLOCK_ID_TUFF:
        return 0x585860u;
    case BLOCK_ID_STONE:
    case BLOCK_ID_GRAVEL:
    case BLOCK_ID_CALCITE:
        return 0x8E8E8Eu;
    case BLOCK_ID_DIRT:
    case BLOCK_ID_COARSE_DIRT:
    case BLOCK_ID_ROOTED_DIRT:
    case BLOCK_ID_MUD:
    case BLOCK_ID_PODZOL:
    case BLOCK_ID_MYCELIUM:
    case BLOCK_ID_CLAY:
        return 0x7A5A3Cu;
    case BLOCK_ID_GRASS_BLOCK:
    case BLOCK_ID_MOSS_BLOCK:
        return 0x4CA030u;
    case BLOCK_ID_SAND:
    case BLOCK_ID_SANDSTONE:
    case BLOCK_ID_RED_SAND:
    case BLOCK_ID_RED_SANDSTONE:
        return 0xDCCF90u;
    case BLOCK_ID_SNOW_BLOCK:
        return 0xFFFFFFu;
    case BLOCK_ID_ICE:
    case BLOCK_ID_PACKED_ICE:
    case BLOCK_ID_BLUE_ICE:
        return 0x90E0F8u;
    default:
        break;
    }
    if (map_is_growth(block)) {
        return 0x2C7820u;
    }
    /* Ore and terracotta: rock with something in it. */
    return 0xA88070u;
}

/* From the slice's blocks to a picture, upward on the page being upward in the world, up to some way over its highest ground. Ticks on the left: a short one every 50 blocks, a long
 * one every 250, and the sea's whole line, in dots. Returns how many rows it drew. */
static uint32_t map_slice_draw(const map_run_t *run, uint32_t *pixels, int32_t *out_top_z) {
    int32_t top_z = (int32_t)MAP_SLICE_LEAST_TOP;
    for (uint32_t layer = MAP_LAYERS; layer-- > 0u;) {
        bool any = false;
        for (uint32_t x = 0u; x < MAP_SLICE_WIDE && !any; x++) {
            const uint16_t block = run->borrowed_slice[(uint64_t)x + (uint64_t)MAP_SLICE_WIDE * layer];
            any = block != BLOCK_ID_AIR;
        }
        if (any) {
            const int32_t z = COLUMN_Z_STORAGE_MIN + (int32_t)layer + (int32_t)MAP_SLICE_SKY;
            top_z = z > top_z ? z : top_z;
            break;
        }
    }
    top_z = top_z < COLUMN_Z_STORAGE_END - 1 ? top_z : COLUMN_Z_STORAGE_END - 1;
    const uint32_t rows = (uint32_t)(top_z - MC_WORLD_Z_MIN + 1);
    for (uint32_t row = 0u; row < rows; row++) {
        const int32_t z = top_z - (int32_t)row;
        const uint32_t layer = (uint32_t)(z - COLUMN_Z_STORAGE_MIN);
        for (uint32_t x = 0u; x < MAP_SLICE_WIDE; x++) {
            const uint64_t at = (uint64_t)x + (uint64_t)MAP_SLICE_WIDE * layer;
            uint32_t colour = map_slice_colour(run->borrowed_slice[at], run->borrowed_slice_opened[at]);
            if (z == MC_WORLD_Z_SEA && x % 8u < 3u) {
                colour = 0xFFFFFFu;
            }
            if ((z % 50 == 0 && x < 8u) || (z % 250 == 0 && x < 32u)) {
                colour = 0xFF00FFu;
            }
            pixels[(uint64_t)x + (uint64_t)MAP_SLICE_WIDE * row] = colour;
        }
    }
    *out_top_z = top_z;
    return rows;
}

static bool map_png_write(const char *path, const uint32_t *pixels, uint32_t wide, uint32_t high, uint8_t *stream,
                          uint64_t stream_bytes) {
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)pixels, wide * 4u, wide, high, stream, stream_bytes, &bytes)) {
        fprintf(stderr, "bench_world_map: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_world_map: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_world_map: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

/* ---- One seed ------------------------------------------------------------------------------------------------------ */

/* All of one seed's table. */
typedef struct map_table_t {
    uint64_t seed;                    /* 0 */
    float64_t sea;                    /* 8: shares of the map, in percent */
    float64_t ice;                    /* 16 */
    float64_t inland_water;           /* 24 */
    float64_t land_low;               /* 32: 0 to 60 */
    float64_t reach[MAP_BANDS_REACH]; /* 40 */
    float64_t mountain_typical;       /* 80: blocks, median over the land */
    float64_t mountain_far;           /* 88: blocks, the 90th percentile */
    float64_t spawn_to_land;          /* 96: blocks, or -1 */
    float64_t spawn_to_mountain;      /* 104: blocks, or -1 */
    float64_t openings_per_km2;       /* 112: of land */
    float64_t deep_openings_per_km2;  /* 120 */
    float64_t floating_per_km2;       /* 128 */
    float64_t steep;                  /* 136: share of the land, in percent, 3 blocks up for 1 across or more */
    map_regions_t seas;               /* 144 */
    map_regions_t ranges;             /* 176 */
    float64_t land_joined;            /* 208: the largest piece's share of all that is not sea, in percent */
    float64_t dry_joined;             /* 216: the same of dry land, which a river or a lake breaks */
    int32_t highest_ground;           /* 224 */
    int32_t spawn_x;                  /* 228 */
    int32_t spawn_y;                  /* 232 */
    int32_t spawn_z;                  /* 236 */
    uint16_t spawn_top;               /* 240 */
    uint8_t _pad0[6];                 /* 242 */
} map_table_t;
static_assert(sizeof(map_table_t) == 248);

/* Buffers for a seed, all in the one region. */
typedef struct map_memory_t {
    uint8_t *mask;         /* 0: MAP_PIXELS */
    uint32_t *queue;       /* 8: MAP_PIXELS */
    float32_t *distance;   /* 16: MAP_PIXELS */
    uint32_t *pixels;      /* 24: MAP_PIXELS, also big enough for a slice */
    uint8_t *stream;       /* 32 */
    uint64_t stream_bytes; /* 40 */
    const char *directory; /* 48 */
} map_memory_t;
static_assert(sizeof(map_memory_t) == 56);

static void map_path(char *path, const char *directory, uint64_t seed, const char *what) {
    snprintf(path, MAP_PATH_BYTES, "%s/%" PRIu64 "_%s.png", directory, seed, what);
}

static float64_t map_percent(uint64_t part, uint64_t whole) {
    return 100.0f64 * (float64_t)part / (float64_t)whole;
}

static float64_t map_km2_of_pixels(uint64_t pixels) {
    return (float64_t)pixels * (float64_t)MAP_STEP * (float64_t)MAP_STEP / 1.0e6f64;
}

/* How far to the nearest pixel whose class is land, in blocks. -1 if none. */
static float64_t map_nearest_land(const map_column_t *columns, int32_t px, int32_t py) {
    uint64_t best = UINT64_MAX;
    for (uint32_t y = 0u; y < MAP_SIDE; y++) {
        for (uint32_t x = 0u; x < MAP_SIDE; x++) {
            if (columns[x + MAP_SIDE * y].klass != (uint8_t)MAP_CLASS_LAND) {
                continue;
            }
            const int64_t dx = (int64_t)x - (int64_t)px, dy = (int64_t)y - (int64_t)py;
            const uint64_t d = (uint64_t)(dx * dx + dy * dy);
            best = d < best ? d : best;
        }
    }
    return best != UINT64_MAX ? sqrt((float64_t)best) * (float64_t)MAP_STEP : -1.0f64;
}

/* The nearest pixel that mask has at 2, and how far, in blocks. False if none. */
static bool map_nearest_mask(const uint8_t *mask, int32_t px, int32_t py, uint32_t *out_at, float64_t *out_blocks) {
    bool found = false;
    uint64_t best = UINT64_MAX;
    for (uint32_t y = 0u; y < MAP_SIDE; y++) {
        for (uint32_t x = 0u; x < MAP_SIDE; x++) {
            if (mask[x + MAP_SIDE * y] != 2u) {
                continue;
            }
            const int64_t dx = (int64_t)x - (int64_t)px, dy = (int64_t)y - (int64_t)py;
            const uint64_t d = (uint64_t)(dx * dx + dy * dy);
            if (d < best) {
                best = d;
                *out_at = x + MAP_SIDE * y;
                found = true;
            }
        }
    }
    *out_blocks = found ? sqrt((float64_t)best) * (float64_t)MAP_STEP : -1.0f64;
    return found;
}

/* Everything measured from the map's columns. Leaves in mask the mountains that count. */
static void map_measure(const map_run_t *run, const map_memory_t *memory, map_table_t *table) {
    const map_column_t *const columns = run->borrowed_columns;
    uint64_t sea = 0u, ice = 0u, inland = 0u, land = 0u, low = 0u;
    uint64_t reach[MAP_BANDS_REACH] = {0u};
    uint64_t sea_but_dry = 0u;
    uint64_t kind_is_sea[TERRAIN_HYDROLOGY_KIND_COUNT] = {0u};
    table->highest_ground = COLUMN_Z_STORAGE_MIN;
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        const map_column_t *const column = &columns[i];
        const int32_t ground = column->ground;
        const bool ocean_kind = column->kind == (uint8_t)TERRAIN_HYDROLOGY_KIND_OCEAN ||
                                column->kind == (uint8_t)TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN;
        ice += map_is_ice(column->top) && column->depth > 0u ? 1u : 0u;
        if (column->klass == (uint8_t)MAP_CLASS_SEA) {
            sea++;
            kind_is_sea[column->kind]++;
            continue;
        }
        if (column->klass == (uint8_t)MAP_CLASS_INLAND_WATER) {
            inland++;
            continue;
        }
        land++;
        sea_but_dry += ocean_kind ? 1u : 0u;
        table->highest_ground = ground > table->highest_ground ? ground : table->highest_ground;
        low += ground >= 0 && ground <= 60 ? 1u : 0u;
        for (uint32_t r = 0u; r < MAP_BANDS_REACH; r++) {
            reach[r] += ground >= MAP_REACH[r] ? 1u : 0u;
        }
    }
    uint64_t steep = 0u;
    for (uint32_t y = 0u; y + 1u < MAP_SIDE; y++) {
        for (uint32_t x = 0u; x + 1u < MAP_SIDE; x++) {
            const uint32_t at = x + MAP_SIDE * y;
            if (columns[at].klass != (uint8_t)MAP_CLASS_LAND) {
                continue;
            }
            const int32_t ground = columns[at].ground;
            const int32_t across = abs(columns[at + 1u].ground - ground);
            const int32_t along = abs(columns[at + MAP_SIDE].ground - ground);
            steep += (across > along ? across : along) >= 3 * (int32_t)MAP_STEP ? 1u : 0u;
        }
    }
    table->steep = land > 0u ? map_percent(steep, land) : 0.0f64;
    table->sea = map_percent(sea, MAP_PIXELS);
    table->ice = map_percent(ice, MAP_PIXELS);
    table->inland_water = map_percent(inland, MAP_PIXELS);
    table->land_low = map_percent(low, MAP_PIXELS);
    for (uint32_t r = 0u; r < MAP_BANDS_REACH; r++) {
        table->reach[r] = map_percent(reach[r], MAP_PIXELS);
    }

    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        memory->mask[i] = columns[i].klass == (uint8_t)MAP_CLASS_SEA ? 1u : 0u;
    }
    map_regions_find(memory->mask, memory->queue, &table->seas);

    /* Is it one world or many islands? Once with rivers and lakes as part of the land, and once as what cuts it up. */
    map_regions_t pieces;
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        memory->mask[i] = columns[i].klass != (uint8_t)MAP_CLASS_SEA ? 1u : 0u;
    }
    map_regions_find(memory->mask, memory->queue, &pieces);
    table->land_joined = map_percent(pieces.largest_pixels, land + inland);
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        memory->mask[i] = columns[i].klass == (uint8_t)MAP_CLASS_LAND ? 1u : 0u;
    }
    map_regions_find(memory->mask, memory->queue, &pieces);
    table->dry_joined = map_percent(pieces.largest_pixels, land);

    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        memory->mask[i] = columns[i].klass == (uint8_t)MAP_CLASS_LAND && columns[i].ground >= MAP_MOUNTAIN_Z ? 1u : 0u;
    }
    map_regions_find(memory->mask, memory->queue, &table->ranges);

    table->mountain_typical = -1.0f64;
    table->mountain_far = -1.0f64;
    if (table->ranges.count > 0u) {
        map_distance_fill(memory->mask, memory->distance);
        static_assert(MAP_DISTANCE_BINS * MAP_DISTANCE_BIN > MAP_ACROSS_BLOCKS * 3u / 2u);
        uint32_t bins[MAP_DISTANCE_BINS] = {0u};
        uint64_t counted = 0u;
        for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
            if (columns[i].klass != (uint8_t)MAP_CLASS_LAND) {
                continue;
            }
            const uint32_t bin = (uint32_t)(memory->distance[i] / (float32_t)MAP_DISTANCE_BIN);
            bins[bin < MAP_DISTANCE_BINS ? bin : MAP_DISTANCE_BINS - 1u]++;
            counted++;
        }
        uint64_t running = 0u;
        for (uint32_t b = 0u; b < MAP_DISTANCE_BINS; b++) {
            running += bins[b];
            if (table->mountain_typical < 0.0f64 && running * 2u >= counted) {
                table->mountain_typical = (float64_t)((b + 1u) * MAP_DISTANCE_BIN);
            }
            if (table->mountain_far < 0.0f64 && running * 10u >= counted * 9u) {
                table->mountain_far = (float64_t)((b + 1u) * MAP_DISTANCE_BIN);
            }
        }
    }
    printf("  of the map: sea %.2f%%, ice %.2f%%, rivers and lakes %.2f%%, land %.2f%%\n", (libc_double_t)table->sea,
           (libc_double_t)table->ice, (libc_double_t)table->inland_water, (libc_double_t)map_percent(land, MAP_PIXELS));
    printf("  hydrology's kind against the blocks: %.3f%% is called ocean and is dry; called something else and is sea "
           "at the sea's level:",
           (libc_double_t)map_percent(sea_but_dry, MAP_PIXELS));
    for (uint32_t kind = 0u; kind < (uint32_t)TERRAIN_HYDROLOGY_KIND_COUNT; kind++) {
        if (kind_is_sea[kind] > 0u && kind != (uint32_t)TERRAIN_HYDROLOGY_KIND_OCEAN &&
            kind != (uint32_t)TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN) {
            printf(" kind %u %.3f%%", kind, (libc_double_t)map_percent(kind_is_sea[kind], MAP_PIXELS));
        }
    }
    printf("\n");
    /* The commonest biomes on top, and how many there are of those a surface can have. */
    uint32_t biome_counts[BIOME_COUNT] = {0u};
    for (uint32_t i = 0u; i < MAP_PIXELS; i++) {
        biome_counts[columns[i].biome]++;
    }
    uint32_t seen = 0u;
    for (uint32_t b = 0u; b < BIOME_COUNT; b++) {
        seen += biome_counts[b] > 0u ? 1u : 0u;
    }
    printf("  %u biomes on top; the commonest:", seen);
    for (uint32_t rank = 0u; rank < 8u; rank++) {
        uint32_t best = 0u;
        for (uint32_t b = 1u; b < BIOME_COUNT; b++) {
            best = biome_counts[b] > biome_counts[best] ? b : best;
        }
        printf(" %s %.1f%%", biome_defs[best].name, (libc_double_t)map_percent(biome_counts[best], MAP_PIXELS));
        biome_counts[best] = 0u;
    }
    printf("\n");
}

/* One tile's mouths into openings: a run of columns, any of them this deep. */
static void map_openings_count(uint16_t *mouths, uint8_t *mask, uint32_t *queue, uint32_t *out_openings,
                               uint32_t *out_deep, uint32_t *out_deeper) {
    uint32_t openings = 0u, deep = 0u, deeper = 0u;
    for (uint32_t i = 0u; i < MAP_TILE_COLUMNS; i++) {
        mask[i] = mouths[i] != 0u ? 1u : 0u;
    }
    for (uint32_t start = 0u; start < MAP_TILE_COLUMNS; start++) {
        if (mask[start] != 1u) {
            continue;
        }
        const uint32_t pixels = map_flood(mask, MAP_TILE_SIDE, start, queue);
        uint32_t deepest = 0u;
        for (uint32_t i = 0u; i < pixels; i++) {
            deepest = mouths[queue[i]] > deepest ? mouths[queue[i]] : deepest;
        }
        openings += 1u;
        deep += deepest >= MAP_OPENING_DEEP ? 1u : 0u;
        deeper += deepest >= MAP_OPENING_DEEPER ? 1u : 0u;
    }
    *out_openings = openings;
    *out_deep = deep;
    *out_deeper = deeper;
}

static bool map_slice_make(job_system_t *system, map_run_t *run, const map_memory_t *memory, int32_t middle_x,
                           int32_t y, uint64_t seed, const char *what) {
    run->slice_x = middle_x - (int32_t)(MAP_SLICE_WIDE / 2u);
    run->slice_y = y;
    map_run_jobs(system, run, map_slice_job);
    int32_t top_z = 0;
    const uint32_t rows = map_slice_draw(run, memory->pixels, &top_z);
    char path[MAP_PATH_BYTES];
    map_path(path, memory->directory, seed, what);
    if (!map_png_write(path, memory->pixels, MAP_SLICE_WIDE, rows, memory->stream, memory->stream_bytes)) {
        return false;
    }
    printf("  %s: y = %d, x = %d to %d, z = %d at the top row to %d at the bottom: %s\n", what, y, run->slice_x,
           run->slice_x + (int32_t)MAP_SLICE_WIDE - 1, top_z, MC_WORLD_Z_MIN, path);
    return true;
}

/* The whole of one seed. Returns false if anything could not be done or did not check out. */
static bool map_seed(job_system_t *system, map_run_t *run, const map_memory_t *memory, uint64_t seed,
                     const game_column_job_t *one_column, bool map_only, map_table_t *table) {
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return false;
    }
    bool ok = true;
    memset(table, 0, sizeof(*table));
    table->seed = seed;
    run->borrowed_terrain = &terrain;
    atomic_store_explicit(&run->checked, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->check_differed, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->check_left_out, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->land_columns, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->floating_pieces, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->floating_voxels, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->floating_largest, 0u, memory_order_relaxed);
    for (uint32_t band = 0u; band < MAP_CAVE_BANDS; band++) {
        atomic_store_explicit(&run->cave_opened[band], 0u, memory_order_relaxed);
        atomic_store_explicit(&run->cave_standing[band], 0u, memory_order_relaxed);
    }
    run->origin_x = GAME_WORLD_MIDDLE - (int32_t)(MAP_ACROSS_BLOCKS / 2u);
    run->origin_y = GAME_WORLD_MIDDLE - (int32_t)(MAP_ACROSS_BLOCKS / 2u);
    /* A column of the map is on the grid of its step, so the same place is the same pixel however it is found. */
    run->origin_x -= run->origin_x % (int32_t)MAP_STEP;
    run->origin_y -= run->origin_y % (int32_t)MAP_STEP;

    printf("seed %" PRIu64 " (0x%016" PRIX64 ")\n", seed, seed);
    int64_t started = time_now_ns();
    map_run_jobs(system, run, map_row_job);
    const float64_t map_seconds = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
    const uint64_t checked = atomic_load_explicit(&run->checked, memory_order_relaxed);
    const uint64_t differed = atomic_load_explicit(&run->check_differed, memory_order_relaxed);
    printf("  %u columns in %.1f s; of %" PRIu64 " also made with every stage, %" PRIu64
           " had a pond or ice spike or the like, which the map leaves out, and %" PRIu64 " differed\n",
           MAP_PIXELS, (libc_double_t)map_seconds, checked,
           atomic_load_explicit(&run->check_left_out, memory_order_relaxed), differed);
    ok = ok && differed == 0u;

    map_measure(run, memory, table);

    /* Where the game starts a body, by its own search. */
    if (!game_spawn_find(nullptr, &terrain, one_column, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &table->spawn_x,
                         &table->spawn_y)) {
        printf("  no dry land for a body to start on within %d blocks\n", GAME_SPAWN_REACH);
    }
    table->spawn_z = game_ground_z(&terrain, one_column, table->spawn_x, table->spawn_y);
    table->spawn_top = one_column->borrowed_states[table->spawn_z - COLUMN_Z_STORAGE_MIN];
    /* The search goes further than the map, so this may be off it. What reads it takes a place outside. */
    const int32_t spawn_px = (table->spawn_x - run->origin_x) / (int32_t)MAP_STEP;
    const int32_t spawn_py = (table->spawn_y - run->origin_y) / (int32_t)MAP_STEP;
    uint32_t mountain_at = 0u;
    table->spawn_to_land = map_nearest_land(run->borrowed_columns, spawn_px, spawn_py);
    const bool mountain_found =
        map_nearest_mask(memory->mask, spawn_px, spawn_py, &mountain_at, &table->spawn_to_mountain);

    map_draw(run->borrowed_columns, memory->pixels);
    map_mark(memory->pixels, spawn_px, spawn_py, MAP_COLOUR_SPAWN);
    if (mountain_found) {
        map_mark(memory->pixels, (int32_t)(mountain_at % MAP_SIDE), (int32_t)(mountain_at / MAP_SIDE),
                 MAP_COLOUR_MOUNTAIN);
    }
    char path[MAP_PATH_BYTES];
    map_path(path, memory->directory, seed, "map");
    ok = map_png_write(path, memory->pixels, MAP_SIDE, MAP_SIDE, memory->stream, memory->stream_bytes) && ok;
    printf("  map: x = %d to %d, y = %d to %d, %u blocks a pixel: %s\n", run->origin_x,
           run->origin_x + (int32_t)(MAP_ACROSS_BLOCKS - MAP_STEP), run->origin_y,
           run->origin_y + (int32_t)(MAP_ACROSS_BLOCKS - MAP_STEP), MAP_STEP, path);

    printf("  seas of at least 0.1 km^2: %u, %u of them 1 km^2; the largest %.1f km^2, %.1f km across, reaching %u of "
           "the map's 4 sides, %.0f%% of all sea\n",
           table->seas.count, table->seas.big, (libc_double_t)map_km2_of_pixels(table->seas.largest_pixels),
           (libc_double_t)((float64_t)(table->seas.largest_extent * MAP_STEP) / 1000.0f64), table->seas.largest_edges,
           (libc_double_t)(table->seas.kept_pixels > 0u
                               ? map_percent(table->seas.largest_pixels, table->seas.kept_pixels)
                               : 0.0f64));
    printf(
        "  the largest piece of land holds %.1f%% of all that is not sea, and %.1f%% of dry land if rivers and lakes "
        "cut it\n",
        (libc_double_t)table->land_joined, (libc_double_t)table->dry_joined);
    if (map_only) {
        fflush(stdout);
        terrain_deinit(&terrain);
        return ok;
    }

    ok = map_slice_make(system, run, memory, table->spawn_x, table->spawn_y, seed, "slice_spawn") && ok;
    if (mountain_found) {
        const int32_t x = run->origin_x + (int32_t)((mountain_at % MAP_SIDE) * MAP_STEP);
        const int32_t y = run->origin_y + (int32_t)((mountain_at / MAP_SIDE) * MAP_STEP);
        ok = map_slice_make(system, run, memory, x, y, seed, "slice_mountain") && ok;
    }

    /* Caves: every tile's mouths, and each band's rock. */
    started = time_now_ns();
    map_run_jobs(system, run, map_cave_job);
    const float64_t cave_seconds = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
    uint32_t openings = 0u, deep = 0u, deeper = 0u;
    for (uint32_t tile = 0u; tile < MAP_TILES; tile++) {
        uint32_t one = 0u, one_deep = 0u, one_deeper = 0u;
        map_openings_count(&run->borrowed_mouths[(uint64_t)tile * MAP_TILE_COLUMNS], memory->mask, memory->queue, &one,
                           &one_deep, &one_deeper);
        openings += one;
        deep += one_deep;
        deeper += one_deeper;
    }
    const uint64_t cave_land = atomic_load_explicit(&run->land_columns, memory_order_relaxed);
    const float64_t land_km2 = (float64_t)cave_land / 1.0e6f64;
    table->openings_per_km2 = land_km2 > 0.0f64 ? (float64_t)deep / land_km2 : 0.0f64;
    table->deep_openings_per_km2 = land_km2 > 0.0f64 ? (float64_t)deeper / land_km2 : 0.0f64;
    printf("  caves: %u chunks with what floats in %.1f s over %.2f km^2 of land: %u mouths, %u at least %u deep, %u "
           "at least %u deep\n",
           MAP_CAVE_CHUNKS, (libc_double_t)cave_seconds, (libc_double_t)land_km2, openings, deep, MAP_OPENING_DEEP,
           deeper, MAP_OPENING_DEEPER);
    table->floating_per_km2 =
        land_km2 > 0.0f64 ? (float64_t)atomic_load_explicit(&run->floating_pieces, memory_order_relaxed) / land_km2
                          : 0.0f64;
    printf("  rock that floats: %" PRIu64 " pieces, %" PRIu64 " voxels, the largest %" PRIu64 " voxels\n",
           atomic_load_explicit(&run->floating_pieces, memory_order_relaxed),
           atomic_load_explicit(&run->floating_voxels, memory_order_relaxed),
           atomic_load_explicit(&run->floating_largest, memory_order_relaxed));
    printf("  caves' share of the rock:");
    for (uint32_t band = 0u; band < MAP_CAVE_BANDS; band++) {
        const uint64_t cut = atomic_load_explicit(&run->cave_opened[band], memory_order_relaxed);
        const uint64_t standing = atomic_load_explicit(&run->cave_standing[band], memory_order_relaxed);
        printf(" %d..%d %.1f%%", MAP_CAVE_BAND_FROM[band], MAP_CAVE_BAND_FROM[band + 1u] - 1,
               (libc_double_t)(cut + standing > 0u ? map_percent(cut, cut + standing) : 0.0f64));
    }
    printf("\n");

    printf("  ground at %d or more, in pieces of at least 0.1 km^2: %u, %u of them 1 km^2; the largest %.1f km^2, %.1f "
           "km across; highest ground %d\n",
           MAP_MOUNTAIN_Z, table->ranges.count, table->ranges.big,
           (libc_double_t)map_km2_of_pixels(table->ranges.largest_pixels),
           (libc_double_t)((float64_t)(table->ranges.largest_extent * MAP_STEP) / 1000.0f64), table->highest_ground);
    printf("  from land to such a piece: typically %.0f, and at the far 10%% %.0f blocks\n",
           (libc_double_t)table->mountain_typical, (libc_double_t)table->mountain_far);
    printf("  spawn at %d, %d, %d on %s; to dry land %.0f blocks, to a piece of high ground %.0f blocks\n",
           table->spawn_x, table->spawn_y, table->spawn_z, block_defs[table->spawn_top].name,
           (libc_double_t)table->spawn_to_land, (libc_double_t)table->spawn_to_mountain);
    fflush(stdout);
    terrain_deinit(&terrain);
    return ok;
}

int32_t main(int32_t argc, char **argv) {
    /* For tuning: the map and what it measures alone, a third of the time, with no slices and no cave tiles. */
    const bool map_only = argc > 1 && strcmp(argv[1], "--map-only") == 0;
    if (map_only) {
        argc--;
        argv++;
    }
    if (argc < 2 || argc - 2 > (int32_t)MAP_SEEDS_MAX) {
        fprintf(stderr, "usage: bench_world_map [--map-only] <directory for the pictures> [seed...]\n");
        return 2;
    }
    const char *const directory = argv[1];
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_world_map: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    const uint32_t seeds = argc > 2 ? (uint32_t)(argc - 2) : 1u;

    const uint64_t worker_voxels = (uint64_t)MAP_WORKERS * TERRAIN_FEATURE_CHUNK_COLUMNS * MAP_LAYERS;
    const uint64_t fields_bytes =
        (uint64_t)MAP_WORKERS * MAP_SIDE * (sizeof(terrain_hydrology_column_t) + sizeof(terrain_biome_column_t));
    const uint64_t columns_bytes = (uint64_t)MAP_PIXELS * sizeof(map_column_t);
    const uint64_t mouths_bytes = (uint64_t)MAP_TILES * MAP_TILE_COLUMNS * sizeof(uint16_t);
    const uint64_t slice_voxels = (uint64_t)MAP_SLICE_WIDE * MAP_LAYERS;
    const uint64_t pixels_count = MAP_PIXELS > slice_voxels ? MAP_PIXELS : slice_voxels;
    const uint64_t stream_map = display_png_bytes_needed(MAP_SIDE, MAP_SIDE);
    const uint64_t stream_slice = display_png_bytes_needed(MAP_SLICE_WIDE, MAP_LAYERS);
    const uint64_t stream_bytes = stream_map > stream_slice ? stream_map : stream_slice;
    const uint64_t one_column_bytes = (uint64_t)MAP_LAYERS * sizeof(uint16_t) + MAP_LAYERS;
    const uint64_t rock_voxels = (uint64_t)MAP_WORKERS * MAP_ROCK_VOXELS;
    const uint64_t total = rock_voxels * (1u + 4u) + worker_voxels * 3u + fields_bytes + columns_bytes + mouths_bytes +
                           slice_voxels * 3u + (uint64_t)MAP_PIXELS * (1u + 4u + 4u) + pixels_count * 4u +
                           stream_bytes + one_column_bytes + (uint64_t)MAP_SEEDS_MAX * sizeof(map_table_t) + 4096u;
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
    map_table_t *const tables = (map_table_t *)cursor;
    cursor += (uint64_t)MAP_SEEDS_MAX * sizeof(map_table_t);
    map_run_t run = {0};
    run.borrowed_blocks = (uint16_t *)cursor;
    cursor += worker_voxels * 2u;
    run.borrowed_fields = (terrain_hydrology_column_t *)cursor;
    cursor += fields_bytes;
    run.borrowed_columns = (map_column_t *)cursor;
    cursor += columns_bytes;
    run.borrowed_mouths = (uint16_t *)cursor;
    cursor += mouths_bytes;
    run.borrowed_slice = (uint16_t *)cursor;
    cursor += slice_voxels * 2u;
    run.borrowed_rock_stack = (uint32_t *)cursor;
    cursor += rock_voxels * 4u;
    map_memory_t memory = {.directory = directory, .stream_bytes = stream_bytes};
    memory.distance = (float32_t *)cursor;
    cursor += (uint64_t)MAP_PIXELS * 4u;
    memory.queue = (uint32_t *)cursor;
    cursor += (uint64_t)MAP_PIXELS * 4u;
    memory.pixels = (uint32_t *)cursor;
    cursor += pixels_count * 4u;
    uint16_t *const one_states = (uint16_t *)cursor;
    cursor += (uint64_t)MAP_LAYERS * sizeof(uint16_t);
    run.borrowed_opened = cursor;
    cursor += worker_voxels;
    run.borrowed_slice_opened = cursor;
    cursor += slice_voxels;
    run.borrowed_rock = cursor;
    cursor += rock_voxels;
    memory.mask = cursor;
    cursor += MAP_PIXELS;
    uint8_t *const one_opened = cursor;
    cursor += MAP_LAYERS;
    memory.stream = cursor;

    game_column_job_t one_column;
    memset(&one_column, 0, sizeof(one_column));
    one_column.borrowed_states = one_states;
    one_column.borrowed_opened = one_opened;

    int32_t status = 0;
    for (uint32_t s = 0u; s < seeds; s++) {
        const uint64_t seed = argc > 2 ? strtoull(argv[2 + s], nullptr, 0) : (uint64_t)TERRAIN_SEED_DEFAULT;
        if (!map_seed(&system, &run, &memory, seed, &one_column, map_only, &tables[s])) {
            status = 1;
        }
    }

    /* All the seeds, one line each, the table the plan's targets are written against. */
    printf("\nseed                   sea    ice  inland joined    dry  0..60");
    for (uint32_t r = 0u; r < MAP_BANDS_REACH; r++) {
        printf("  >=%-4d", MAP_REACH[r]);
    }
    printf("   top  typical  far10%%  seas  largest  high  big  largest  across  steep  floating/km2  openings/km2  "
           "deep/km2  to land  to high  spawn on\n");
    for (uint32_t s = 0u; s < seeds; s++) {
        const map_table_t *const t = &tables[s];
        printf("%-20" PRIu64 " %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%%", t->seed, (libc_double_t)t->sea,
               (libc_double_t)t->ice, (libc_double_t)t->inland_water, (libc_double_t)t->land_joined,
               (libc_double_t)t->dry_joined, (libc_double_t)t->land_low);
        for (uint32_t r = 0u; r < MAP_BANDS_REACH; r++) {
            printf(" %5.2f%%", (libc_double_t)t->reach[r]);
        }
        printf(" %5d %8.0f %7.0f %5u %8.1f %5u %4u %8.1f %7.1f %5.1f%% %13.1f %13.2f %9.2f %8.0f %8.0f  %s\n",
               t->highest_ground, (libc_double_t)t->mountain_typical, (libc_double_t)t->mountain_far, t->seas.big,
               (libc_double_t)map_km2_of_pixels(t->seas.largest_pixels), t->ranges.count, t->ranges.big,
               (libc_double_t)map_km2_of_pixels(t->ranges.largest_pixels),
               (libc_double_t)((float64_t)(t->ranges.largest_extent * MAP_STEP) / 1000.0f64), (libc_double_t)t->steep,
               (libc_double_t)t->floating_per_km2, (libc_double_t)t->openings_per_km2,
               (libc_double_t)t->deep_openings_per_km2, (libc_double_t)t->spawn_to_land,
               (libc_double_t)t->spawn_to_mountain, block_defs[t->spawn_top].name);
    }
    job_deinit(&system);
    heap_release(&memory_region);
    return status;
}
