/* bench_cave_walk: whether a body on foot can find a seed's caves and go down them, and a walk into one, with no display and no
 * card. See README.md. */
#include "display/headless/png/index.h"
#include "game/index.h"
#include "game/spawn/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "physics/index.h"
#include "player/camera/index.h"
#include "player/movement/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"
#include "world/index.h"
#include "world/section/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    WALK_WORKERS = 16u,
    /* A measured area is this many chunks a side, and the one a route is looked for in, round where a body starts, this many. */
    WALK_AREA_CHUNKS = 32u,
    WALK_ROUTE_CHUNKS = 64u,
    WALK_SIDE_MAX = WALK_ROUTE_CHUNKS * TERRAIN_FEATURE_CHUNK,
    /* Areas measured a seed: this many a side, this far apart, about the world's middle. */
    WALK_AREAS_SIDE = 3u,
    WALK_AREAS = WALK_AREAS_SIDE * WALK_AREAS_SIDE,
    WALK_AREA_SPACING = 4096u,
    WALK_LAYERS = (uint32_t)(TERRAIN_MADE_TOP - MC_WORLD_Z_MIN + 1),
    WALK_STORAGE_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    /* Places a body stood, which is far fewer than the voxels. */
    WALK_QUEUE_MAX = 96u * 1024u * 1024u,
    /* Ground over the feet, in blocks, for a place to be in a cave, and for a way in to be a real way down. */
    WALK_UNDER = 8u,
    WALK_ENTRANCE_DEEP = 20u,
    /* How far under its ground a walk goes before it is called done. */
    WALK_ROUTE_DEEP = 24u,
    WALK_ROUTE_POINTS_MAX = 16384u,
    /* The most a body may drop and take no harm. */
    WALK_DROP_SAFE = 3u,
    WALK_BANDS = 5u,
    WALK_DISTANCE_BIN = 8u,
    WALK_DISTANCE_BINS = 256u,
    /* Land under this is low land, where a game starts and where ranges do not hide the caves in their sides. */
    WALK_LOW_LAND = 150u,
    WALK_PATH_BYTES = 512u,
    WALK_SEEDS_MAX = 64u,
    /* The walk: ticks before it sets off, and with no way forward before it gives up. */
    WALK_START_TICK = 100u,
    WALK_STUCK_TICKS = 200u,
    WALK_TICKS_MAX = 40000u,
    /* Pictures of the walk. */
    WALK_MOUTH_SLICE_WIDE = 256u,
    WALK_SHOT_EVERY = 200u,
    WALK_SLICE_MARGIN = 24u,
    WALK_SLICE_ROWS_MAX = 1024u,
};
/* How far a seed's second walk goes down: the deep levels, which are what stage 5 asks be there to explore. */
static const int32_t WALK_ROUTE_DEEP_Z = -150;
static_assert(MC_THREADS_WORKERS >= WALK_WORKERS);
static_assert(WALK_UNDER <= WALK_ENTRANCE_DEEP && WALK_ENTRANCE_DEEP <= WALK_ROUTE_DEEP);
static_assert((uint64_t)WALK_SIDE_MAX * WALK_SIDE_MAX * WALK_LAYERS < UINT32_MAX);

/* What each voxel is to a body. */
enum : uint8_t {
    WALK_SOLID = 1u,
    WALK_LIQUID = 2u,
    WALK_CUT = 4u,
    WALK_REACHED = 8u,
    WALK_LAVA = 16u,
};

/* The moves a place was reached by, for the way back: a side, and how far up it went, as dz + WALK_DROP_SAFE. */
enum : uint8_t {
    WALK_PARENT_SIDES = 4u,
    WALK_PARENT_NONE = 255u,
};

static const int32_t WALK_DX[WALK_PARENT_SIDES] = {1, -1, 0, 0};
static const int32_t WALK_DY[WALK_PARENT_SIDES] = {0, 0, 1, -1};

/* Where each row of the table starts, going up, and where the last ends. */
static const int32_t WALK_BAND_FROM[WALK_BANDS + 1u] = {-250, -150, -50, 0, 50, 1251};

/* As the game's own, in run/src/game/index.c: what the script's mouse turns from. A mismatch shows as a walk that goes astray. */
#define WALK_GAME_START_YAW 0.6f32
#define WALK_PI 3.14159265358979f64

/* One area's voxels and what is worked out on them. */
typedef struct walk_area_t {
    uint8_t *borrowed_cells;   /* 0: side * side * WALK_LAYERS */
    uint8_t *borrowed_parents; /* 8: the same, for a route */
    int16_t *borrowed_top;     /* 16: side * side: the highest z of the ground, or INT16_MIN for none */
    uint8_t *borrowed_land;    /* 24: side * side: 1 for dry ground on top */
    int32_t origin_x;          /* 32 */
    int32_t origin_y;          /* 36 */
    uint32_t side;             /* 40 */
    uint8_t _pad0[4];          /* 44 */
    uint64_t row;              /* 48: side */
    uint64_t layer;            /* 56: side * side */
} walk_area_t;
static_assert(sizeof(walk_area_t) == 64);

/* What is shared with the workers. */
typedef struct walk_run_t {
    const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_blocks;         /* 8: a whole chunk's per worker */
    uint8_t *borrowed_opened;          /* 16: the same */
    walk_area_t *borrowed_area;        /* 24 */
    _Atomic uint32_t next;             /* 32 */
    _Atomic uint32_t next_worker;      /* 36 */
    uint32_t chunks_side;              /* 40 */
    uint8_t _pad0[4];                  /* 44 */
} walk_run_t;
static_assert(sizeof(walk_run_t) == 48);

/* All the memory that is not an area's own. */
typedef struct walk_memory_t {
    uint32_t *queue;       /* 0: WALK_QUEUE_MAX */
    uint32_t *origins;     /* 8: the same: each place's way in from the sky, as a column */
    uint16_t *way_deep;    /* 16: side * side at most: how far under its ground each way in leads */
    float32_t *distance;   /* 24: the same */
    uint8_t *mask;         /* 32: the same */
    uint32_t *points;      /* 40: WALK_ROUTE_POINTS_MAX places, as voxels */
    uint32_t *pixels;      /* 48: enough for any slice */
    uint8_t *stream;       /* 56: the PNG */
    uint64_t stream_bytes; /* 64 */
    const char *directory; /* 72 */
    uint16_t *one_states;  /* 80: a whole column, for the spawn search */
    uint8_t *one_opened;   /* 88 */
    world_t *world;        /* 96: for the walk */
    physics_t *physics;    /* 104 */
} walk_memory_t;
static_assert(sizeof(walk_memory_t) == 112);

/* What a seed's areas add up to. */
typedef struct walk_totals_t {
    uint64_t under[WALK_BANDS];                 /* 0: places a body could stand in a cave */
    uint64_t reached[WALK_BANDS];               /* 40: those a body can walk to from the sky, and in its own area */
    uint64_t land_columns;                      /* 80 */
    uint64_t ways_in;                           /* 88: ways in that lead at least WALK_ENTRANCE_DEEP under */
    uint64_t distance_bins[WALK_DISTANCE_BINS]; /* 96: of land, to the nearest of them */
    uint64_t none_near;                         /* 2144: land with none in its area */
    int32_t lowest;                             /* 2152: the lowest z walked to */
    uint32_t deepest;                           /* 2156: the most ground over a place walked to */
    uint64_t low_columns;                       /* 2160: of the land, that under WALK_LOW_LAND, where a game starts */
    uint64_t low_bins[WALK_DISTANCE_BINS];      /* 2168 */
    uint64_t cut_air[WALK_BANDS];               /* 4216: what caves cut, by what it holds in the end */
    uint64_t cut_water[WALK_BANDS];             /* 4256 */
    uint64_t cut_lava[WALK_BANDS];              /* 4296 */
    uint64_t cut_stopped[WALK_BANDS];           /* 4336: stone again: the flood's walls between two bodies */
} walk_totals_t;
static_assert(sizeof(walk_totals_t) == 4376);

/* What one seed's line of the table says. */
typedef struct walk_table_t {
    uint64_t seed;               /* 0 */
    float64_t ways_per_km2;      /* 8 */
    float64_t typical;           /* 16 */
    float64_t far;               /* 24 */
    float64_t within_128;        /* 32 */
    float64_t share[WALK_BANDS]; /* 40 */
    int32_t lowest;              /* 80 */
    uint32_t deepest;            /* 84 */
    uint32_t route_blocks;       /* 88: steps to WALK_ROUTE_DEEP from where a body starts, and 0 for none */
    uint8_t _pad0[4];            /* 92 */
    uint32_t walked_ticks;       /* 96: 0 if the body did not get there */
    uint32_t deep_blocks;        /* 100: the same, to WALK_ROUTE_DEEP_Z */
    float64_t low_typical;       /* 104: typical, of low land */
    float64_t low_within_128;    /* 112 */
    uint32_t deep_ticks;         /* 120 */
    uint8_t _pad1[4];            /* 124 */
} walk_table_t;
static_assert(sizeof(walk_table_t) == 128);

/* ---- Voxels -------------------------------------------------------------------------------------------------------- */

static bool walk_is_growth(uint16_t block) {
    switch (block) {
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
    default:
        break;
    }
    return (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u || (block_defs[block].flags & BLOCK_FLAG_DECAYS) != 0u;
}

static bool walk_is_ice(uint16_t block) {
    return block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE || block == BLOCK_ID_BLUE_ICE;
}

static void walk_chunks_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    walk_run_t *const run = borrowed_argument;
    const walk_area_t *const area = run->borrowed_area;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    const uint64_t chunk_voxels = (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * WALK_STORAGE_LAYERS;
    uint16_t *const blocks = run->borrowed_blocks + (uint64_t)me * chunk_voxels;
    uint8_t *const opened = run->borrowed_opened + (uint64_t)me * chunk_voxels;
    const uint32_t chunks = run->chunks_side * run->chunks_side;
    for (;;) {
        const uint32_t chunk = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (chunk >= chunks) {
            return;
        }
        const uint32_t tile_x = (chunk % run->chunks_side) * TERRAIN_FEATURE_CHUNK;
        const uint32_t tile_y = (chunk / run->chunks_side) * TERRAIN_FEATURE_CHUNK;
        const terrain_density_box_t box = {
            .origin_x = area->origin_x + (int32_t)tile_x,
            .origin_y = area->origin_y + (int32_t)tile_y,
            .origin_z = COLUMN_Z_STORAGE_MIN,
            .count_x = TERRAIN_FEATURE_CHUNK,
            .count_y = TERRAIN_FEATURE_CHUNK,
            .count_z = WALK_STORAGE_LAYERS,
        };
        terrain_generate_box(run->borrowed_terrain, &box, opened, blocks);
        for (uint32_t column = 0u; column < TERRAIN_FEATURE_CHUNK_COLUMNS; column++) {
            const uint64_t in_area = (uint64_t)(tile_x + column % TERRAIN_FEATURE_CHUNK) +
                                     area->row * (tile_y + column / TERRAIN_FEATURE_CHUNK);
            int16_t top = INT16_MIN;
            uint8_t land = 0u;
            for (uint32_t l = WALK_LAYERS; l-- > 0u;) {
                const uint32_t storage = l + (uint32_t)(MC_WORLD_Z_MIN - COLUMN_Z_STORAGE_MIN);
                const uint64_t voxel = (uint64_t)column + (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * storage;
                const uint16_t block = blocks[voxel];
                uint8_t cell = 0u;
                cell |= (block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u ? WALK_SOLID : 0u;
                cell |= block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA ? WALK_LIQUID : 0u;
                cell |= block == BLOCK_ID_LAVA ? WALK_LAVA : 0u;
                cell |= opened[voxel] != 0u ? WALK_CUT : 0u;
                area->borrowed_cells[in_area + area->layer * l] = cell;
                if (top == INT16_MIN && (block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA ||
                                         ((cell & WALK_SOLID) != 0u && !walk_is_growth(block)))) {
                    top = (int16_t)(MC_WORLD_Z_MIN + (int32_t)l);
                    land = block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA && !walk_is_ice(block) ? 1u : 0u;
                }
            }
            area->borrowed_top[in_area] = top;
            area->borrowed_land[in_area] = land;
        }
    }
}

static void walk_area_make(job_system_t *system, walk_run_t *run, walk_area_t *area, uint32_t chunks_side) {
    area->side = chunks_side * TERRAIN_FEATURE_CHUNK;
    area->row = area->side;
    area->layer = (uint64_t)area->side * area->side;
    run->borrowed_area = area;
    run->chunks_side = chunks_side;
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < WALK_WORKERS; i++) {
        job_submit(system, &group, walk_chunks_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
}

/* A place is the voxel of a body's feet. It can stand there: on something solid, with room for all of it and no fluid. */
static bool walk_can_stand(const walk_area_t *area, uint64_t at) {
    const uint8_t *const c = area->borrowed_cells;
    const uint8_t blocked = WALK_SOLID | WALK_LIQUID;
    return (c[at] & blocked) == 0u && (c[at + area->layer] & blocked) == 0u && (c[at - area->layer] & WALK_SOLID) != 0u;
}

/* Ground over a place, from its column's top, which leaves out what grows. */
static int32_t walk_cover(const walk_area_t *area, uint64_t at) {
    const uint64_t column = at % area->layer;
    const int32_t z = MC_WORLD_Z_MIN + (int32_t)(at / area->layer);
    return (int32_t)area->borrowed_top[column] - z + 1;
}

/* A cave's own air, with ground over it: not the foot of a cliff the 3D noise has undercut. */
static bool walk_in_cave(const walk_area_t *area, uint64_t at, int32_t under) {
    return (area->borrowed_cells[at] & WALK_CUT) != 0u && walk_cover(area, at) >= under;
}

/* Where a body goes from a place, taking one step to a side, if it can. A step takes it up one block, by a jump,
 * if there is room over its head, or along, or down as far as is safe. Out of the area or into fluid is no step. */
static bool walk_step(const walk_area_t *area, uint64_t from, uint32_t side_index, uint64_t *out, int32_t *out_dz) {
    const uint64_t column = from % area->layer;
    const int64_t x = (int64_t)(column % area->row) + WALK_DX[side_index];
    const int64_t y = (int64_t)(column / area->row) + WALK_DY[side_index];
    if (x < 0 || y < 0 || x >= (int64_t)area->side || y >= (int64_t)area->side) {
        return false;
    }
    const uint64_t layer = from / area->layer;
    if (layer < 1u + WALK_DROP_SAFE || layer + 3u >= WALK_LAYERS) {
        return false;
    }
    const uint64_t next_column = (uint64_t)x + area->row * (uint64_t)y;
    const uint64_t next = next_column + layer * area->layer;
    const uint8_t *const c = area->borrowed_cells;
    const uint8_t blocked = WALK_SOLID | WALK_LIQUID;
    if ((c[next] & WALK_SOLID) != 0u) {
        /* A jump: room over its head here, and for all of it there. */
        if ((c[from + 2u * area->layer] & blocked) != 0u || (c[next + area->layer] & blocked) != 0u ||
            (c[next + 2u * area->layer] & blocked) != 0u) {
            return false;
        }
        *out = next + area->layer;
        *out_dz = 1;
        return true;
    }
    if ((c[next] & WALK_LIQUID) != 0u || (c[next + area->layer] & blocked) != 0u) {
        return false;
    }
    for (uint32_t drop = 0u; drop <= WALK_DROP_SAFE; drop++) {
        const uint64_t place = next - (uint64_t)drop * area->layer;
        if ((c[place] & WALK_LIQUID) != 0u) {
            return false;
        }
        if ((c[place - area->layer] & WALK_SOLID) != 0u) {
            *out = place;
            *out_dz = -(int32_t)drop;
            return true;
        }
    }
    return false;
}

/* ---- What a body can reach from the sky ------------------------------------------------------------------------------ */

static uint32_t walk_band_of(int32_t z) {
    uint32_t band = 0u;
    while (band + 1u < WALK_BANDS && z >= WALK_BAND_FROM[band + 1u]) {
        band++;
    }
    return band;
}

/* Sweeps the distance from each column to the nearest marked one, reading up to 8% long. */
static void walk_distance_sweep(const walk_memory_t *memory, uint32_t side) {
    const float32_t straight = 1.0f32;
    const float32_t diagonal = 1.41421356f32;
    float32_t *const d = memory->distance;
    for (uint32_t y = 0u; y < side; y++) {
        for (uint32_t x = 0u; x < side; x++) {
            const uint32_t at = x + side * y;
            float32_t best = d[at];
            if (x > 0u && d[at - 1u] + straight < best) {
                best = d[at - 1u] + straight;
            }
            if (y > 0u && d[at - side] + straight < best) {
                best = d[at - side] + straight;
            }
            if (x > 0u && y > 0u && d[at - side - 1u] + diagonal < best) {
                best = d[at - side - 1u] + diagonal;
            }
            if (x + 1u < side && y > 0u && d[at - side + 1u] + diagonal < best) {
                best = d[at - side + 1u] + diagonal;
            }
            d[at] = best;
        }
    }
    for (uint32_t y = side; y-- > 0u;) {
        for (uint32_t x = side; x-- > 0u;) {
            const uint32_t at = x + side * y;
            float32_t best = d[at];
            if (x + 1u < side && d[at + 1u] + straight < best) {
                best = d[at + 1u] + straight;
            }
            if (y + 1u < side && d[at + side] + straight < best) {
                best = d[at + side] + straight;
            }
            if (x + 1u < side && y + 1u < side && d[at + side + 1u] + diagonal < best) {
                best = d[at + side + 1u] + diagonal;
            }
            if (x > 0u && y + 1u < side && d[at + side - 1u] + diagonal < best) {
                best = d[at + side - 1u] + diagonal;
            }
            d[at] = best;
        }
    }
}

/* Marked columns that touch, counted as one: mask is 1 at a marked column. */
static uint64_t walk_count_pieces(const walk_memory_t *memory, uint32_t side) {
    uint8_t *const mask = memory->mask;
    uint32_t *const stack = memory->queue;
    const uint32_t columns = side * side;
    uint64_t pieces = 0u;
    for (uint32_t start = 0u; start < columns; start++) {
        if (mask[start] != 1u) {
            continue;
        }
        pieces++;
        uint32_t held = 0u;
        stack[held] = start;
        held++;
        mask[start] = 2u;
        while (held > 0u) {
            held--;
            const uint32_t at = stack[held];
            const uint32_t x = at % side;
            const uint32_t y = at / side;
            for (uint32_t s = 0u; s < WALK_PARENT_SIDES; s++) {
                const int64_t nx = (int64_t)x + WALK_DX[s];
                const int64_t ny = (int64_t)y + WALK_DY[s];
                if (nx < 0 || ny < 0 || nx >= (int64_t)side || ny >= (int64_t)side) {
                    continue;
                }
                const uint32_t near = (uint32_t)nx + side * (uint32_t)ny;
                if (mask[near] == 1u) {
                    mask[near] = 2u;
                    stack[held] = near;
                    held++;
                }
            }
        }
    }
    return pieces;
}

/* Everything a body can walk to in this area from the sky, and how much that is of all there is. Returns false if the queue ran out. */
static bool walk_area_measure(const walk_area_t *area, const walk_memory_t *memory, walk_totals_t *totals) {
    uint8_t *const c = area->borrowed_cells;
    uint32_t *const queue = memory->queue;
    uint32_t *const origins = memory->origins;
    const uint32_t columns = area->side * area->side;
    uint64_t tail = 0u;
    for (uint32_t column = 0u; column < columns; column++) {
        memory->way_deep[column] = 0u;
        const int16_t top = area->borrowed_top[column];
        if (top == INT16_MIN || top + 2 >= MC_WORLD_Z_MIN + (int32_t)WALK_LAYERS) {
            continue;
        }
        const uint64_t place = (uint64_t)column + area->layer * (uint64_t)((int32_t)top + 1 - MC_WORLD_Z_MIN);
        /* Water or lava on top is no place to stand, and a tree's foot is not walked to from above. */
        if (walk_can_stand(area, place)) {
            c[place] |= WALK_REACHED;
            queue[tail] = (uint32_t)place;
            origins[tail] = column;
            tail++;
        }
    }
    for (uint64_t head = 0u; head < tail; head++) {
        const uint64_t from = queue[head];
        const int32_t cover = walk_cover(area, from);
        const int32_t z = MC_WORLD_Z_MIN + (int32_t)(from / area->layer);
        if (walk_in_cave(area, from, (int32_t)WALK_UNDER)) {
            memory->way_deep[origins[head]] =
                (uint32_t)cover > memory->way_deep[origins[head]] ? (uint16_t)cover : memory->way_deep[origins[head]];
            totals->reached[walk_band_of(z)]++;
            totals->lowest = z < totals->lowest ? z : totals->lowest;
            totals->deepest = (uint32_t)cover > totals->deepest ? (uint32_t)cover : totals->deepest;
        }
        for (uint32_t s = 0u; s < WALK_PARENT_SIDES; s++) {
            uint64_t to = 0u;
            int32_t dz = 0;
            if (!walk_step(area, from, s, &to, &dz) || (c[to] & WALK_REACHED) != 0u) {
                continue;
            }
            if (tail >= WALK_QUEUE_MAX) {
                return false;
            }
            c[to] |= WALK_REACHED;
            queue[tail] = (uint32_t)to;
            origins[tail] = origins[head];
            tail++;
        }
    }
    /* All the places a body could stand in a cave, reached or not. */
    for (uint32_t column = 0u; column < columns; column++) {
        const int16_t top = area->borrowed_top[column];
        if (top == INT16_MIN) {
            continue;
        }
        const uint32_t highest = (uint32_t)((int32_t)top - MC_WORLD_Z_MIN + 1);
        for (uint32_t l = 1u; l + (uint32_t)WALK_UNDER <= highest; l++) {
            const uint64_t place = (uint64_t)column + area->layer * l;
            if ((c[place] & WALK_CUT) == 0u) {
                continue;
            }
            const uint32_t band = walk_band_of(MC_WORLD_Z_MIN + (int32_t)l);
            totals->under[band] += walk_can_stand(area, place) ? 1u : 0u;
            if ((c[place] & WALK_LAVA) != 0u) {
                totals->cut_lava[band]++;
            } else if ((c[place] & WALK_LIQUID) != 0u) {
                totals->cut_water[band]++;
            } else if ((c[place] & WALK_SOLID) != 0u) {
                totals->cut_stopped[band]++;
            } else {
                totals->cut_air[band]++;
            }
        }
    }
    /* The ways in that lead somewhere: how many, and how far the land is from them. */
    const float32_t far = 1.0e9f32;
    for (uint32_t column = 0u; column < columns; column++) {
        const bool way = memory->way_deep[column] >= WALK_ENTRANCE_DEEP;
        memory->mask[column] = way ? 1u : 0u;
        memory->distance[column] = way ? 0.0f32 : far;
    }
    totals->ways_in += walk_count_pieces(memory, area->side);
    walk_distance_sweep(memory, area->side);
    for (uint32_t column = 0u; column < columns; column++) {
        if (area->borrowed_land[column] == 0u) {
            continue;
        }
        totals->land_columns++;
        if (memory->distance[column] >= far / 2.0f32) {
            totals->none_near++;
            continue;
        }
        uint32_t bin = (uint32_t)memory->distance[column] / WALK_DISTANCE_BIN;
        bin = bin >= WALK_DISTANCE_BINS ? WALK_DISTANCE_BINS - 1u : bin;
        totals->distance_bins[bin]++;
        if (area->borrowed_top[column] < (int32_t)WALK_LOW_LAND) {
            totals->low_columns++;
            totals->low_bins[bin]++;
        }
    }
    return true;
}

/* The distance that this much of the land is within, in blocks: the top of its bin. An area with no way in counts as past every bin. */
static float64_t walk_distance_share(const uint64_t *bins, uint64_t columns, float64_t share) {
    const uint64_t wanted = (uint64_t)((float64_t)columns * share);
    uint64_t sum = 0u;
    for (uint32_t bin = 0u; bin < WALK_DISTANCE_BINS; bin++) {
        sum += bins[bin];
        if (sum >= wanted) {
            return (float64_t)((bin + 1u) * WALK_DISTANCE_BIN);
        }
    }
    return -1.0f64;
}

/* ---- A way in from where a body starts ------------------------------------------------------------------------------- */

/* Looks from a place, by fewest steps, for the first one that far under its ground and at or under that z. Its route goes in
 * memory->points, first to last. Returns how many places, or 0 for none. */
static uint32_t walk_route_find(const walk_area_t *area, const walk_memory_t *memory, uint64_t start,
                                int32_t z_at_most) {
    uint8_t *const c = area->borrowed_cells;
    uint8_t *const parents = area->borrowed_parents;
    uint32_t *const queue = memory->queue;
    uint64_t tail = 0u;
    queue[tail] = (uint32_t)start;
    tail++;
    c[start] |= WALK_REACHED;
    parents[start] = WALK_PARENT_NONE;
    int32_t lowest = INT32_MAX;
    for (uint64_t head = 0u; head < tail; head++) {
        const uint64_t from = queue[head];
        const int32_t z_here = MC_WORLD_Z_MIN + (int32_t)(from / area->layer);
        lowest = z_here < lowest ? z_here : lowest;
        if (MC_WORLD_Z_MIN + (int32_t)(from / area->layer) <= z_at_most &&
            walk_in_cave(area, from, (int32_t)WALK_ROUTE_DEEP)) {
            /* Back up the moves, then turn them round. */
            uint32_t count = 0u;
            uint64_t at = from;
            for (;;) {
                if (count >= WALK_ROUTE_POINTS_MAX) {
                    return 0u;
                }
                memory->points[count] = (uint32_t)at;
                count++;
                if (parents[at] == WALK_PARENT_NONE) {
                    break;
                }
                const uint32_t side_index = parents[at] & 3u;
                const int32_t dz = (int32_t)(parents[at] >> 2u) - (int32_t)WALK_DROP_SAFE;
                const uint64_t column = at % area->layer;
                const uint64_t x = (uint64_t)((int64_t)(column % area->row) - WALK_DX[side_index]);
                const uint64_t y = (uint64_t)((int64_t)(column / area->row) - WALK_DY[side_index]);
                const int64_t layer = (int64_t)(at / area->layer) - dz;
                at = x + area->row * y + area->layer * (uint64_t)layer;
            }
            for (uint32_t i = 0u; i < count / 2u; i++) {
                const uint32_t kept = memory->points[i];
                memory->points[i] = memory->points[count - 1u - i];
                memory->points[count - 1u - i] = kept;
            }
            return count;
        }
        for (uint32_t s = 0u; s < WALK_PARENT_SIDES; s++) {
            uint64_t to = 0u;
            int32_t dz = 0;
            if (!walk_step(area, from, s, &to, &dz) || (c[to] & WALK_REACHED) != 0u) {
                continue;
            }
            if (tail >= WALK_QUEUE_MAX) {
                return 0u;
            }
            c[to] |= WALK_REACHED;
            parents[to] = (uint8_t)(s | (uint32_t)((dz + (int32_t)WALK_DROP_SAFE) << 2u));
            queue[tail] = (uint32_t)to;
            tail++;
        }
    }
    printf("  walked to %" PRIu64 " places, the lowest at z = %d\n", tail, lowest);
    return 0u;
}

/* ---- Pictures -------------------------------------------------------------------------------------------------------- */

static const uint32_t WALK_COLOUR_SKY = 0xA8D0F0u;
static const uint32_t WALK_COLOUR_ROCK = 0x8C8C8Cu;
static const uint32_t WALK_COLOUR_CAVE = 0x141414u;
static const uint32_t WALK_COLOUR_WATER = 0x2050C0u;
static const uint32_t WALK_COLOUR_LAVA = 0xFF5000u;
static const uint32_t WALK_COLOUR_ROUTE = 0xFF00FFu;
static const uint32_t WALK_COLOUR_GROWTH = 0x3C8C3Cu;

static uint32_t walk_voxel_colour(const walk_area_t *area, uint64_t at) {
    const uint8_t cell = area->borrowed_cells[at];
    if ((cell & WALK_LAVA) != 0u) {
        return WALK_COLOUR_LAVA;
    }
    if ((cell & WALK_LIQUID) != 0u) {
        return WALK_COLOUR_WATER;
    }
    const int32_t z = MC_WORLD_Z_MIN + (int32_t)(at / area->layer);
    const int32_t top = area->borrowed_top[at % area->layer];
    if ((cell & WALK_SOLID) != 0u) {
        return z > top ? WALK_COLOUR_GROWTH : WALK_COLOUR_ROCK;
    }
    return z > top ? WALK_COLOUR_SKY : WALK_COLOUR_CAVE;
}

static bool walk_png_write(const walk_memory_t *memory, const char *path, uint32_t wide, uint32_t high) {
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)memory->pixels, wide * 4u, wide, high, memory->stream,
                            memory->stream_bytes, &bytes)) {
        fprintf(stderr, "bench_cave_walk: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_cave_walk: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(memory->stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_cave_walk: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

/* The route unrolled: each column along it, side by side, as if the rock were cut along the walk and laid flat. */
static bool walk_slice_along(const walk_area_t *area, const walk_memory_t *memory, uint32_t count, uint64_t seed,
                             const char *name) {
    int32_t low = INT32_MAX, high = INT32_MIN;
    for (uint32_t i = 0u; i < count; i++) {
        const uint64_t at = memory->points[i];
        const int32_t z = MC_WORLD_Z_MIN + (int32_t)(at / area->layer);
        const int32_t top = area->borrowed_top[at % area->layer];
        low = z < low ? z : low;
        high = (top > z ? top : z) > high ? (top > z ? top : z) : high;
    }
    low = low - (int32_t)WALK_SLICE_MARGIN < MC_WORLD_Z_MIN ? MC_WORLD_Z_MIN : low - (int32_t)WALK_SLICE_MARGIN;
    high += (int32_t)WALK_SLICE_MARGIN;
    const uint32_t rows = (uint32_t)(high - low + 1);
    const uint32_t wide = count;
    if (rows > WALK_SLICE_ROWS_MAX || wide > WALK_ROUTE_POINTS_MAX) {
        printf("  the route's slice would be %u by %u, so it is left out\n", wide, rows);
        return true;
    }
    for (uint32_t i = 0u; i < count; i++) {
        const uint64_t at = memory->points[i];
        const uint64_t column = at % area->layer;
        const uint32_t feet = (uint32_t)(at / area->layer);
        for (uint32_t r = 0u; r < rows; r++) {
            const uint32_t l = (uint32_t)(high - (int32_t)r - MC_WORLD_Z_MIN);
            const bool route = l == feet || l == feet + 1u;
            memory->pixels[i + wide * r] =
                route ? WALK_COLOUR_ROUTE : walk_voxel_colour(area, column + area->layer * (uint64_t)l);
        }
    }
    char path[WALK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_%s_unrolled.png", memory->directory, seed, name);
    printf("  route unrolled: %u columns, z = %d at the top row to %d at the bottom: %s\n", wide, high, low, path);
    return walk_png_write(memory, path, wide, rows);
}

/* A straight slice along x, through a place. */
static bool walk_slice_straight(const walk_area_t *area, const walk_memory_t *memory, uint64_t through, int32_t low,
                                int32_t high, uint32_t count, uint64_t seed, const char *name) {
    const uint64_t column = through % area->layer;
    const uint32_t y = (uint32_t)(column / area->row);
    const uint32_t middle = (uint32_t)(column % area->row);
    uint32_t first = middle > WALK_MOUTH_SLICE_WIDE / 2u ? middle - WALK_MOUTH_SLICE_WIDE / 2u : 0u;
    first = first + WALK_MOUTH_SLICE_WIDE > area->side ? area->side - WALK_MOUTH_SLICE_WIDE : first;
    const uint32_t rows = (uint32_t)(high - low + 1);
    if (rows > WALK_SLICE_ROWS_MAX) {
        return true;
    }
    for (uint32_t i = 0u; i < WALK_MOUTH_SLICE_WIDE; i++) {
        for (uint32_t r = 0u; r < rows; r++) {
            const uint32_t l = (uint32_t)(high - (int32_t)r - MC_WORLD_Z_MIN);
            const uint64_t at = (uint64_t)(first + i) + area->row * y + area->layer * (uint64_t)l;
            memory->pixels[i + WALK_MOUTH_SLICE_WIDE * r] = walk_voxel_colour(area, at);
        }
    }
    /* The walk where it crosses this slice. */
    for (uint32_t p = 0u; p < count; p++) {
        const uint64_t at = memory->points[p];
        const uint64_t place = at % area->layer;
        const int32_t z = MC_WORLD_Z_MIN + (int32_t)(at / area->layer);
        if ((uint32_t)(place / area->row) == y && (uint32_t)(place % area->row) >= first &&
            (uint32_t)(place % area->row) < first + WALK_MOUTH_SLICE_WIDE && z >= low && z <= high) {
            memory->pixels[(uint32_t)(place % area->row) - first + WALK_MOUTH_SLICE_WIDE * (uint32_t)(high - z)] =
                WALK_COLOUR_ROUTE;
        }
    }
    char path[WALK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_%s_mouth.png", memory->directory, seed, name);
    printf("  slice through the way in: y = %d, x = %d to %d, z = %d at the top row to %d at the bottom: %s\n",
           area->origin_y + (int32_t)y, area->origin_x + (int32_t)first,
           area->origin_x + (int32_t)first + (int32_t)WALK_MOUTH_SLICE_WIDE - 1, high, low, path);
    return walk_png_write(memory, path, WALK_MOUTH_SLICE_WIDE, rows);
}

/* ---- The walk -------------------------------------------------------------------------------------------------------- */

typedef struct walk_column_job_t {
    const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_states;         /* 8 */
    uint8_t *borrowed_opened;          /* 16 */
    world_map_request_t request;       /* 24 */
} walk_column_job_t;
static_assert(sizeof(walk_column_job_t) == 24u + sizeof(world_map_request_t));

static void walk_column_job(job_worker_t *worker, void *borrowed_job) {
    (void)worker;
    walk_column_job_t *const job = borrowed_job;
    const terrain_density_box_t box = {
        .origin_x = job->request.chunk_x * (int32_t)COLUMN_SIZE,
        .origin_y = job->request.chunk_y * (int32_t)COLUMN_SIZE,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = COLUMN_SIZE,
        .count_y = COLUMN_SIZE,
        .count_z = WALK_STORAGE_LAYERS,
    };
    terrain_generate_box(job->borrowed_terrain, &box, job->borrowed_opened, job->borrowed_states);
}

/* The columns round the route, made with every stage, into a real world for the body to move through. */
static bool walk_world_fill(job_system_t *system, walk_run_t *run, const walk_memory_t *memory, int32_t chunk_x,
                            int32_t chunk_y, uint32_t radius) {
    world_t *const world = memory->world;
    if (!world_open(world, radius, chunk_x, chunk_y)) {
        return false;
    }
    const uint64_t chunk_voxels = (uint64_t)TERRAIN_FEATURE_CHUNK_COLUMNS * WALK_STORAGE_LAYERS;
    walk_column_job_t jobs[WALK_WORKERS];
    for (;;) {
        uint32_t taken = 0u;
        world_map_request_t request;
        while (taken < WALK_WORKERS && world_map_stream_take(&world->map, &request, 1u) == 1u) {
            jobs[taken] = (walk_column_job_t){
                .borrowed_terrain = run->borrowed_terrain,
                .borrowed_states = run->borrowed_blocks + (uint64_t)taken * chunk_voxels,
                .borrowed_opened = run->borrowed_opened + (uint64_t)taken * chunk_voxels,
                .request = request,
            };
            taken++;
        }
        if (taken == 0u) {
            return true;
        }
        job_group_t group;
        job_group_init(&group);
        for (uint32_t i = 0u; i < taken; i++) {
            job_submit(system, &group, walk_column_job, &jobs[i]);
        }
        job_group_wait(system, &group);
        job_group_deinit(&group);
        for (uint32_t i = 0u; i < taken; i++) {
            column_t *const column = world_map_stream_commit(&world->map, &jobs[i].request);
            if (column == nullptr) {
                return false;
            }
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                if (!section_blocks_import(&column->sections[section], &world->section_pool,
                                           jobs[i].borrowed_states + (uint64_t)section * SECTION_VOXELS)) {
                    fprintf(stderr, "bench_cave_walk: the world's section memory is full\n");
                    return false;
                }
            }
        }
    }
}

/* What the walk gives back. */
typedef struct walk_outcome_t {
    float64_t position[3]; /* 0: the feet at the end */
    uint32_t ticks;        /* 24: the walk's, and 0 if it did not get there */
    uint32_t stuck_at;     /* 28: which place of the route it gave up on */
    uint8_t was_waiting;   /* 32: its column was not loaded when it gave up */
    uint8_t _pad0[7];      /* 33 */
} walk_outcome_t;
static_assert(sizeof(walk_outcome_t) == 40);

/* The real body's own moves along the route, on the real blocks: what the game's tick does, from what a script's keys and mouse say, and a
 * script that says them. */
static walk_outcome_t walk_along(const walk_area_t *area, const walk_memory_t *memory, uint32_t count, FILE *script,
                                 const char *name) {
    walk_outcome_t outcome = {0};
    player_movement_t body;
    const uint64_t first = memory->points[0];
    const float64_t start_x = (float64_t)area->origin_x + (float64_t)(first % area->layer % area->row) + 0.5f64;
    const float64_t start_y = (float64_t)area->origin_y + (float64_t)(first % area->layer / area->row) + 0.5f64;
    const float64_t start_z = (float64_t)(MC_WORLD_Z_MIN + (int32_t)(first / area->layer));
    player_movement_start(&body, start_x, start_y, start_z);
    render_camera_t camera;
    render_camera_start(&camera, start_x, start_y, start_z, WALK_GAME_START_YAW, 0.0f32);

    fprintf(script, "# Made by bench_cave_walk. Its first line of keys is on tick %u.\n", WALK_START_TICK);
    fprintf(script, "0 screenshot %s_start.png\n", name);
    fprintf(script, "%u key w down\n", WALK_START_TICK);

    uint32_t next_point = 1u;
    uint32_t best_point = 1u;
    uint32_t since_better = 0u;
    bool jump_held = false;
    bool shot_mouth = false;
    for (uint32_t tick = 0u; tick < WALK_TICKS_MAX; tick++) {
        /* On to the next place once this one is under it. */
        for (;;) {
            if (next_point >= count) {
                break;
            }
            const uint64_t at = memory->points[next_point];
            const float64_t to_x = (float64_t)area->origin_x + (float64_t)(at % area->layer % area->row) + 0.5f64;
            const float64_t to_y = (float64_t)area->origin_y + (float64_t)(at % area->layer / area->row) + 0.5f64;
            const float64_t to_z = (float64_t)(MC_WORLD_Z_MIN + (int32_t)(at / area->layer));
            const float64_t dx = to_x - body.position[0], dy = to_y - body.position[1];
            if (dx * dx + dy * dy < 0.0625f64 && fabs(to_z - body.position[2]) < 0.75f64) {
                next_point++;
                continue;
            }
            break;
        }
        if (next_point >= count) {
            outcome.ticks = tick + 1u;
            break;
        }
        if (!shot_mouth && walk_in_cave(area, memory->points[next_point], (int32_t)WALK_UNDER)) {
            fprintf(script, "%u screenshot %s_mouth.png\n", WALK_START_TICK + tick, name);
            shot_mouth = true;
        }
        if (next_point > best_point) {
            best_point = next_point;
            since_better = 0u;
        } else {
            since_better++;
            if (since_better > WALK_STUCK_TICKS) {
                outcome.stuck_at = next_point;
                break;
            }
        }
        const uint64_t at = memory->points[next_point];
        const float64_t to_x = (float64_t)area->origin_x + (float64_t)(at % area->layer % area->row) + 0.5f64;
        const float64_t to_y = (float64_t)area->origin_y + (float64_t)(at % area->layer / area->row) + 0.5f64;
        const float64_t to_z = (float64_t)(MC_WORLD_Z_MIN + (int32_t)(at / area->layer));
        const float64_t dx = to_x - body.position[0], dy = to_y - body.position[1];
        /* Whole counts of the mouse, as a script can only say. Turning left is a smaller x. */
        const float64_t wanted = atan2(dy, dx);
        float64_t off = (float64_t)camera.yaw - wanted;
        off -= 2.0f64 * WALK_PI * round(off / (2.0f64 * WALK_PI));
        const int64_t counts = (int64_t)round(off / (float64_t)RENDER_CAMERA_RADIANS_PER_COUNT);
        if (counts != 0) {
            player_camera_look(&camera, counts, 0);
            fprintf(script, "%u move %" PRId64 " 0\n", WALK_START_TICK + tick, counts);
        }
        const bool jump = to_z > body.position[2] + 0.1f64 && dx * dx + dy * dy < 2.25f64;
        if (jump != jump_held) {
            fprintf(script, "%u key space %s\n", WALK_START_TICK + tick, jump ? "down" : "up");
            jump_held = jump;
        }
        const player_intent_t intent = {.yaw = camera.yaw,
                                        .forward = 1,
                                        .right = 0,
                                        .jump = jump ? 1u : 0u,
                                        .sneak = 0u,
                                        .sprint = 0u,
                                        ._pad0 = {0}};
        player_movement_tick(&body, memory->physics, &memory->world->map, &intent);
        if (tick % WALK_SHOT_EVERY == 0u && tick > 0u) {
            fprintf(script, "%u screenshot %s_%05u.png\n", WALK_START_TICK + tick, name, tick);
        }
    }
    memcpy(outcome.position, body.position, sizeof(outcome.position));
    outcome.was_waiting = body.waiting_for_column;
    if (outcome.ticks != 0u) {
        fprintf(script, "%u key w up\n", WALK_START_TICK + outcome.ticks);
        if (jump_held) {
            fprintf(script, "%u key space up\n", WALK_START_TICK + outcome.ticks);
        }
        fprintf(script, "%u screenshot %s_end.png\n", WALK_START_TICK + outcome.ticks + 10u, name);
        fprintf(script, "%u end\n", WALK_START_TICK + outcome.ticks + 11u);
    }
    return outcome;
}

/* One walk from where a body starts, to a place that far under the ground and at or under that z: found, drawn, walked, and scripted for
 * the real game to walk too. */
static bool walk_route_one(job_system_t *system, walk_run_t *run, walk_area_t *area, const walk_memory_t *memory,
                           uint64_t seed, uint64_t start, int32_t z_at_most, const char *name, uint32_t *out_blocks,
                           uint32_t *out_ticks) {
    /* What an earlier walk went through is open again. */
    const uint64_t voxels = area->layer * WALK_LAYERS;
    for (uint64_t i = 0u; i < voxels; i++) {
        area->borrowed_cells[i] &= (uint8_t)~WALK_REACHED;
    }
    const uint32_t count = walk_route_find(area, memory, start, z_at_most);
    if (count == 0u) {
        printf("  %s: no way on foot from where a body starts to %u under the ground and z <= %d, within %u blocks\n",
               name, WALK_ROUTE_DEEP, z_at_most, WALK_ROUTE_CHUNKS * TERRAIN_FEATURE_CHUNK / 2u);
        return false;
    }
    uint32_t mouth_at = count - 1u;
    for (uint32_t i = 0u; i < count && mouth_at == count - 1u; i++) {
        if (walk_in_cave(area, memory->points[i], (int32_t)WALK_UNDER)) {
            mouth_at = i;
        }
    }
    *out_blocks = count;
    const uint64_t last = memory->points[count - 1u];
    const uint64_t mouth = memory->points[mouth_at];
    printf("  %s: %u steps to a place %d under its ground, at %d, %d, %d, going in at %d, %d, %d\n", name, count,
           walk_cover(area, last), area->origin_x + (int32_t)(last % area->layer % area->row),
           area->origin_y + (int32_t)(last % area->layer / area->row), MC_WORLD_Z_MIN + (int32_t)(last / area->layer),
           area->origin_x + (int32_t)(mouth % area->layer % area->row),
           area->origin_y + (int32_t)(mouth % area->layer / area->row),
           MC_WORLD_Z_MIN + (int32_t)(mouth / area->layer));
    bool ok = walk_slice_along(area, memory, count, seed, name);
    const int32_t mouth_z = MC_WORLD_Z_MIN + (int32_t)(mouth / area->layer);
    ok = walk_slice_straight(area, memory, mouth, mouth_z - 64, mouth_z + 64, count, seed, name) && ok;

    /* A real body through real columns. */
    int64_t low_x = INT64_MAX, low_y = INT64_MAX, high_x = INT64_MIN, high_y = INT64_MIN;
    for (uint32_t i = 0u; i < count; i++) {
        const uint64_t at = memory->points[i] % area->layer;
        const int64_t x = (int64_t)area->origin_x + (int64_t)(at % area->row);
        const int64_t y = (int64_t)area->origin_y + (int64_t)(at / area->row);
        low_x = x < low_x ? x : low_x;
        low_y = y < low_y ? y : low_y;
        high_x = x > high_x ? x : high_x;
        high_y = y > high_y ? y : high_y;
    }
    const int32_t chunk_x = column_chunk_of((int32_t)((low_x + high_x) / 2));
    const int32_t chunk_y = column_chunk_of((int32_t)((low_y + high_y) / 2));
    const int64_t across_x = high_x - low_x, across_y = high_y - low_y;
    /* The table is a disc, so its corners want a bit more. */
    const uint32_t radius =
        (uint32_t)((across_x > across_y ? across_x : across_y) * 3 / (int64_t)(4u * COLUMN_SIZE)) + 3u;
    if (!walk_world_fill(system, run, memory, chunk_x, chunk_y, radius)) {
        printf("  %s: the world for the walk could not be made\n", name);
        world_close(memory->world);
        return false;
    }
    char path[WALK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_%s.script", memory->directory, seed, name);
    FILE *const script = fopen(path, "w");
    if (script == nullptr) {
        fprintf(stderr, "bench_cave_walk: cannot write %s: %s\n", path, strerror(errno));
        world_close(memory->world);
        return false;
    }
    const walk_outcome_t outcome = walk_along(area, memory, count, script, name);
    const bool closed = fclose(script) == 0;
    world_close(memory->world);
    *out_ticks = outcome.ticks;
    if (outcome.ticks == 0u || !closed) {
        printf("  %s: the body gave up on the way, at step %u of %u, at %.2f, %.2f, %.2f, %s\n", name, outcome.stuck_at,
               count, (libc_double_t)outcome.position[0], (libc_double_t)outcome.position[1],
               (libc_double_t)outcome.position[2], outcome.was_waiting != 0u ? "with its column not there" : "stuck");
        return false;
    }
    printf("  %s: the body walked it in %u ticks, %.0f s of play, and ended at %.3f, %.3f, %.3f: %s\n", name,
           outcome.ticks, (libc_double_t)((float64_t)outcome.ticks / (float64_t)MC_GAME_TICK_HZ),
           (libc_double_t)outcome.position[0], (libc_double_t)outcome.position[1], (libc_double_t)outcome.position[2],
           path);
    return ok;
}

/* Two walks from where a body starts: into the nearest cave, and down to the deep levels. */
static bool walk_route(job_system_t *system, walk_run_t *run, walk_area_t *area, const walk_memory_t *memory,
                       uint64_t seed, walk_table_t *table) {
    const game_column_job_t one_column = {.borrowed_states = memory->one_states, .borrowed_opened = memory->one_opened};
    int32_t spawn_x = 0, spawn_y = 0;
    if (!game_spawn_find(nullptr, run->borrowed_terrain, &one_column, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &spawn_x,
                         &spawn_y)) {
        printf("  no dry land for a body to start on\n");
        return false;
    }
    const int32_t ground = game_ground_z(run->borrowed_terrain, &one_column, spawn_x, spawn_y);
    const int32_t half = (int32_t)(WALK_ROUTE_CHUNKS * TERRAIN_FEATURE_CHUNK / 2u);
    area->origin_x = column_chunk_of(spawn_x) * (int32_t)COLUMN_SIZE - half;
    area->origin_y = column_chunk_of(spawn_y) * (int32_t)COLUMN_SIZE - half;
    walk_area_make(system, run, area, WALK_ROUTE_CHUNKS);
    const uint64_t column = (uint64_t)(spawn_x - area->origin_x) + area->row * (uint64_t)(spawn_y - area->origin_y);
    uint64_t start = column + area->layer * (uint64_t)(ground + 1 - MC_WORLD_Z_MIN);
    while (!walk_can_stand(area, start) && start >= area->layer * 2u) {
        start -= area->layer;
    }
    printf("  a body starts at %d, %d, %d\n", spawn_x, spawn_y, MC_WORLD_Z_MIN + (int32_t)(start / area->layer));
    const bool cave = walk_route_one(system, run, area, memory, seed, start, INT32_MAX, "cave", &table->route_blocks,
                                     &table->walked_ticks);
    const bool deep = walk_route_one(system, run, area, memory, seed, start, WALK_ROUTE_DEEP_Z, "deep",
                                     &table->deep_blocks, &table->deep_ticks);
    return cave && deep;
}

/* ---- One seed -------------------------------------------------------------------------------------------------------- */

static bool walk_seed(job_system_t *system, walk_run_t *run, walk_area_t *area, const walk_memory_t *memory,
                      uint64_t seed, bool route_only, walk_table_t *table) {
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return false;
    }
    bool ok = true;
    memset(table, 0, sizeof(*table));
    table->seed = seed;
    run->borrowed_terrain = &terrain;
    printf("seed %" PRIu64 " (0x%016" PRIX64 ")\n", seed, seed);
    if (!route_only) {
        walk_totals_t *const totals = (walk_totals_t *)memory->stream;
        memset(totals, 0, sizeof(*totals));
        totals->lowest = INT32_MAX;
        int64_t started = time_now_ns();
        for (uint32_t i = 0u; i < WALK_AREAS; i++) {
            const int32_t across = (int32_t)(WALK_AREA_CHUNKS * TERRAIN_FEATURE_CHUNK);
            const int32_t step = (int32_t)WALK_AREA_SPACING;
            area->origin_x = ((int32_t)(i % WALK_AREAS_SIDE) - 1) * step - across / 2;
            area->origin_y = ((int32_t)(i / WALK_AREAS_SIDE) - 1) * step - across / 2;
            walk_area_make(system, run, area, WALK_AREA_CHUNKS);
            if (!walk_area_measure(area, memory, totals)) {
                printf("  too many places to hold, in area %u\n", i);
                ok = false;
            }
        }
        const float64_t land_km2 = (float64_t)totals->land_columns / 1.0e6f64;
        table->ways_per_km2 = land_km2 > 0.0f64 ? (float64_t)totals->ways_in / land_km2 : 0.0f64;
        table->typical = walk_distance_share(totals->distance_bins, totals->land_columns, 0.5f64);
        table->far = walk_distance_share(totals->distance_bins, totals->land_columns, 0.9f64);
        table->low_typical = walk_distance_share(totals->low_bins, totals->low_columns, 0.5f64);
        uint64_t within = 0u;
        uint64_t low_within = 0u;
        for (uint32_t bin = 0u; bin < 128u / WALK_DISTANCE_BIN; bin++) {
            within += totals->distance_bins[bin];
            low_within += totals->low_bins[bin];
        }
        table->low_within_128 =
            totals->low_columns > 0u ? 100.0f64 * (float64_t)low_within / (float64_t)totals->low_columns : 0.0f64;
        table->within_128 =
            totals->land_columns > 0u ? 100.0f64 * (float64_t)within / (float64_t)totals->land_columns : 0.0f64;
        printf("  %u areas of %u blocks a side, %.2f km^2 of land, in %.1f s\n", WALK_AREAS,
               WALK_AREA_CHUNKS * TERRAIN_FEATURE_CHUNK, (libc_double_t)land_km2,
               (libc_double_t)((float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND));
        for (uint32_t band = 0u; band < WALK_BANDS; band++) {
            table->share[band] = totals->under[band] > 0u
                                     ? 100.0f64 * (float64_t)totals->reached[band] / (float64_t)totals->under[band]
                                     : 0.0f64;
            const float64_t cut = (float64_t)(totals->cut_air[band] + totals->cut_water[band] + totals->cut_lava[band] +
                                              totals->cut_stopped[band]);
            const float64_t per_cut = cut > 0.0f64 ? 100.0f64 / cut : 0.0f64;
            printf("    z %5d to %5d: %10" PRIu64 " places to stand in caves, %5.1f%% of them walked to from the sky; "
                   "what caves cut is %4.1f%% air, %4.1f%% water, %4.1f%% lava, %4.1f%% stone again\n",
                   WALK_BAND_FROM[band], WALK_BAND_FROM[band + 1u] - 1, totals->under[band],
                   (libc_double_t)table->share[band], (libc_double_t)((float64_t)totals->cut_air[band] * per_cut),
                   (libc_double_t)((float64_t)totals->cut_water[band] * per_cut),
                   (libc_double_t)((float64_t)totals->cut_lava[band] * per_cut),
                   (libc_double_t)((float64_t)totals->cut_stopped[band] * per_cut));
        }
        table->lowest = totals->lowest == INT32_MAX ? 0 : totals->lowest;
        table->deepest = totals->deepest;
        printf("  %" PRIu64
               " ways in that lead at least %u under, %.1f per km^2 of land; land to the nearest: half within "
               "%.0f blocks, 90%% within %.0f, %.0f%% within 128, and %.0f%% has none in its area\n",
               totals->ways_in, WALK_ENTRANCE_DEEP, (libc_double_t)table->ways_per_km2, (libc_double_t)table->typical,
               (libc_double_t)table->far, (libc_double_t)table->within_128,
               (libc_double_t)(totals->land_columns > 0u
                                   ? 100.0f64 * (float64_t)totals->none_near / (float64_t)totals->land_columns
                                   : 0.0f64));
        printf("  the lowest walked to is z = %d, and the most ground over a place walked to is %u\n", table->lowest,
               table->deepest);
    }
    ok = walk_route(system, run, area, memory, seed, table) && ok;
    fflush(stdout);
    terrain_deinit(&terrain);
    return ok;
}

int32_t main(int32_t argc, char **argv) {
    /* The walk alone, for a test: no areas are measured. */
    const bool route_only = argc > 1 && strcmp(argv[1], "--route-only") == 0;
    if (route_only) {
        argc--;
        argv++;
    }
    if (argc < 2 || argc - 2 > (int32_t)WALK_SEEDS_MAX) {
        fprintf(stderr, "usage: bench_cave_walk [--route-only] <directory for the pictures and scripts> [seed...]\n");
        return 2;
    }
    const char *const directory = argv[1];
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_cave_walk: cannot make %s: %s\n", directory, strerror(errno));
        return 2;
    }
    const uint32_t seeds = argc > 2 ? (uint32_t)(argc - 2) : 1u;

    const uint64_t columns = (uint64_t)WALK_SIDE_MAX * WALK_SIDE_MAX;
    const uint64_t voxels = columns * WALK_LAYERS;
    const uint64_t worker_voxels = (uint64_t)WALK_WORKERS * TERRAIN_FEATURE_CHUNK_COLUMNS * WALK_STORAGE_LAYERS;
    const uint64_t pixels = (uint64_t)WALK_ROUTE_POINTS_MAX * WALK_SLICE_ROWS_MAX;
    uint64_t stream_bytes = display_png_bytes_needed(WALK_ROUTE_POINTS_MAX, WALK_SLICE_ROWS_MAX);
    stream_bytes = stream_bytes > sizeof(walk_totals_t) ? stream_bytes : sizeof(walk_totals_t);
    const uint64_t total = voxels * 2u + columns * (2u + 1u + 2u + 4u + 1u) + (uint64_t)WALK_QUEUE_MAX * 8u +
                           (uint64_t)WALK_ROUTE_POINTS_MAX * 4u + pixels * 4u + stream_bytes + worker_voxels * 3u +
                           (uint64_t)WALK_STORAGE_LAYERS * 3u + sizeof(world_t) + sizeof(physics_t) + 16u * 4096u +
                           (uint64_t)WALK_SEEDS_MAX * sizeof(walk_table_t);
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
    walk_table_t *const tables = (walk_table_t *)cursor;
    cursor += (uint64_t)WALK_SEEDS_MAX * sizeof(walk_table_t);
    cursor = (uint8_t *)(((uintptr_t)cursor + 4095u) & ~(uintptr_t)4095u);
    walk_memory_t memory = {.directory = directory, .stream_bytes = stream_bytes};
    memory.world = (world_t *)cursor;
    cursor += sizeof(world_t);
    cursor = (uint8_t *)(((uintptr_t)cursor + 4095u) & ~(uintptr_t)4095u);
    memory.physics = (physics_t *)cursor;
    cursor += sizeof(physics_t);
    cursor = (uint8_t *)(((uintptr_t)cursor + 4095u) & ~(uintptr_t)4095u);
    walk_run_t run = {0};
    run.borrowed_blocks = (uint16_t *)cursor;
    cursor += worker_voxels * 2u;
    memory.one_states = (uint16_t *)cursor;
    cursor += (uint64_t)WALK_STORAGE_LAYERS * 2u;
    memory.queue = (uint32_t *)cursor;
    cursor += (uint64_t)WALK_QUEUE_MAX * 4u;
    memory.origins = (uint32_t *)cursor;
    cursor += (uint64_t)WALK_QUEUE_MAX * 4u;
    memory.points = (uint32_t *)cursor;
    cursor += (uint64_t)WALK_ROUTE_POINTS_MAX * 4u;
    memory.pixels = (uint32_t *)cursor;
    cursor += pixels * 4u;
    memory.distance = (float32_t *)cursor;
    cursor += columns * 4u;
    int16_t *const top = (int16_t *)cursor;
    cursor += columns * 2u;
    memory.way_deep = (uint16_t *)cursor;
    cursor += columns * 2u;
    memory.mask = cursor;
    cursor += columns;
    uint8_t *const land = cursor;
    cursor += columns;
    uint8_t *const cells = cursor;
    cursor += voxels;
    uint8_t *const parents = cursor;
    cursor += voxels;
    run.borrowed_opened = cursor;
    cursor += worker_voxels;
    memory.one_opened = cursor;
    cursor += WALK_STORAGE_LAYERS;
    memory.stream = cursor;

    walk_area_t area = {
        .borrowed_cells = cells, .borrowed_parents = parents, .borrowed_top = top, .borrowed_land = land};

    int32_t status = 0;
    if (!world_init(memory.world, (world_limits_t){8ull << 30, 16384u}) || !physics_init(memory.physics)) {
        status = 2;
    }
    for (uint32_t s = 0u; s < seeds && status != 2; s++) {
        const uint64_t seed = argc > 2 ? strtoull(argv[2 + s], nullptr, 0) : (uint64_t)TERRAIN_SEED_DEFAULT;
        if (!walk_seed(&system, &run, &area, &memory, seed, route_only, &tables[s])) {
            status = 1;
        }
    }
    if (status != 2 && !route_only) {
        printf("\nseed                  ways/km2  typical   far  within128  low-typ  low128");
        for (uint32_t band = 0u; band < WALK_BANDS; band++) {
            printf("  %5d+", WALK_BAND_FROM[band]);
        }
        printf("  lowest  deepest  route  ticks   deep  ticks\n");
        for (uint32_t s = 0u; s < seeds; s++) {
            const walk_table_t *const t = &tables[s];
            printf("%-21" PRIu64 " %8.1f  %7.0f  %4.0f  %8.0f%%  %7.0f  %5.0f%%", t->seed,
                   (libc_double_t)t->ways_per_km2, (libc_double_t)t->typical, (libc_double_t)t->far,
                   (libc_double_t)t->within_128, (libc_double_t)t->low_typical, (libc_double_t)t->low_within_128);
            for (uint32_t band = 0u; band < WALK_BANDS; band++) {
                printf("  %5.1f%%", (libc_double_t)t->share[band]);
            }
            printf("  %6d  %7u  %5u  %5u  %5u  %5u\n", t->lowest, t->deepest, t->route_blocks, t->walked_ticks,
                   t->deep_blocks, t->deep_ticks);
        }
    }
    physics_deinit(memory.physics);
    world_deinit(memory.world);
    job_deinit(&system);
    heap_release(&memory_region);
    return status;
}
