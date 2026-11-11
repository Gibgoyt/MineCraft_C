/* See run/src/terrain/ore/README.md. */
#ifndef MC_RUN_TERRAIN_ORE_INDEX_H
#define MC_RUN_TERRAIN_ORE_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "tables/ores/index.h"
#include "terrain/density/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Rows in the table, rock first and then ore: config/ores/, generated into tables/ores/. */
    TERRAIN_ORE_ROWS = ORE_ROW_COUNT,
    /* The balls of one vein. */
    TERRAIN_ORE_BALLS = 3u,
};

enum : int32_t {
    /* Stone becomes deepslate through this band: all of it at or below BOTTOM, none of it at or above TOP. */
    TERRAIN_ORE_DEEPSLATE_BOTTOM = -128,
    TERRAIN_ORE_DEEPSLATE_TOP = -112,
    /* A row's min_column_height when it can go under any column. */
    TERRAIN_ORE_ANY_COLUMN = ORE_ANY_COLUMN,
};
static_assert(TERRAIN_ORE_DEEPSLATE_BOTTOM < TERRAIN_ORE_DEEPSLATE_TOP);
static_assert(TERRAIN_ORE_DEEPSLATE_BOTTOM > MC_WORLD_Z_MIN && TERRAIN_ORE_DEEPSLATE_TOP < MC_WORLD_Z_SEA);

/* How a row picks a vein's z: the generated table's words. */
typedef ore_spread_t terrain_ore_spread_t;
enum : uint8_t {
    TERRAIN_ORE_UNIFORM = ORE_SPREAD_UNIFORM,   /* the same at every z from z_low to z_high */
    TERRAIN_ORE_TRIANGLE = ORE_SPREAD_TRIANGLE, /* most at the middle, none past the ends */
};

/* A row of the table: one kind of vein at one range of depths, as config/ores/ says. A z_low below the world is deliberate: it
 * puts the most veins at the bottom of the world, and a vein that lands outside the world is lost. The fields are in the
 * generated tables/ores/index.h. */
typedef ore_row_def_t terrain_ore_row_t;
static_assert(sizeof(terrain_ore_row_t) == 24);

/* What one world has worked out from a row. */
typedef struct terrain_ore_world_row_t {
    uint64_t seed;    /* 0: from the world's seed and the row's name */
    float32_t radius; /* 8: of each ball */
    int32_t reach;    /* 12: no voxel of a vein is further than this from where it started, on any axis */
} terrain_ore_world_row_t;
static_assert(sizeof(terrain_ore_world_row_t) == 16);

/* One world's ores. Nothing changes it after terrain_ore_init, so any number of workers may read it. */
typedef struct terrain_ore_t {
    terrain_ore_world_row_t rows[TERRAIN_ORE_ROWS]; /* 0: in the table's order */
    const terrain_density_t *borrowed_density;      /* 16 * TERRAIN_ORE_ROWS: where a column's ground is */
    uint64_t strata_seed;                           /* 16 * TERRAIN_ORE_ROWS + 8 */
} terrain_ore_t;
static_assert(sizeof(terrain_ore_t) == 16u * TERRAIN_ORE_ROWS + 16u);

/* Takes the ores from arena, for this world's seed. For use while terrain_init runs. density must outlive it. */
void terrain_ore_init(terrain_ore_t **out, arena_t *arena, uint64_t seed, const terrain_density_t *density);

/* Empties the handle. The memory goes back with the arena. */
void terrain_ore_deinit(terrain_ore_t *ore);

/* The table: TERRAIN_ORE_ROWS rows, in the order they are laid down. The same for every world. */
const terrain_ore_row_t *terrain_ore_table(void);

/* How many veins row row_index of the table starts in the chunk (chunk_x, chunk_y) and does not lose outside the world, by the
 * same draws terrain_ore_fill_box makes: none in a chunk too low for a row with a min_column_height. For measuring a row's rate,
 * as bench_world_map and the tests do. Allocates nothing; any thread. */
uint32_t terrain_ore_chunk_veins(const terrain_ore_t *ore, uint32_t row_index, int32_t chunk_x, int32_t chunk_y);

/* Lays the rock and the ores into blocks, which holds the box's voxels in x, then y, then z order, and which has the ground in
 * it already: stone turns to deepslate by depth, then each row in turn replaces rock. Air, water, lava, bedrock and anything
 * else not rock are left as they are, so caves cut first show their ore. Allocates nothing, and given the same blocks at a
 * voxel a value is a pure function of the ores and the voxel: splitting a box into parts, in any order, on any thread, gives
 * the same bytes. */
void terrain_ore_fill_box(const terrain_ore_t *ore, const terrain_density_box_t *box, uint16_t *blocks);

#endif
