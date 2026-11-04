/* See run/src/terrain/ore/README.md. */
#ifndef MC_RUN_TERRAIN_ORE_INDEX_H
#define MC_RUN_TERRAIN_ORE_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "terrain/density/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Rows in the table, rock first and then ore. */
    TERRAIN_ORE_ROWS = 26u,
    /* The balls of one vein. */
    TERRAIN_ORE_BALLS = 3u,
};

enum : int32_t {
    /* Stone becomes deepslate through this band: all of it at or below BOTTOM, none of it at or above TOP. */
    TERRAIN_ORE_DEEPSLATE_BOTTOM = -128,
    TERRAIN_ORE_DEEPSLATE_TOP = -112,
    /* A row's min_column_height when it can go under any column. */
    TERRAIN_ORE_ANY_COLUMN = INT16_MIN,
};
static_assert(TERRAIN_ORE_DEEPSLATE_BOTTOM < TERRAIN_ORE_DEEPSLATE_TOP);
static_assert(TERRAIN_ORE_DEEPSLATE_BOTTOM > MC_WORLD_Z_MIN && TERRAIN_ORE_DEEPSLATE_TOP < MC_WORLD_Z_SEA);

/* How a row picks a vein's z. */
typedef enum terrain_ore_spread_t : uint8_t {
    TERRAIN_ORE_UNIFORM = 0,  /* the same at every z from z_low to z_high */
    TERRAIN_ORE_TRIANGLE = 1, /* most at the middle, none past the ends */
} terrain_ore_spread_t;

/* A row of the table: one kind of vein at one range of depths. A z_low below the world is deliberate: it puts the most veins
 * at the bottom of the world, and a vein that lands outside the world is lost. */
typedef struct terrain_ore_row_t {
    const char *borrowed_name; /* 0: a string literal; the row's stream comes from it */
    uint32_t per_chunk_256;    /* 8: veins tried in a chunk, in 256ths */
    int16_t z_low;             /* 12: both ends included */
    int16_t z_high;            /* 14 */
    int16_t min_column_height; /* 16: only in a chunk whose middle column's height is at least this */
    uint16_t block;            /* 18: what it makes in stone and the like */
    uint16_t deep_block;       /* 20: what it makes in deepslate and tuff; the same as block for a rock */
    uint8_t size;              /* 22: voxels a vein aims for, before what is not rock takes its share */
    uint8_t spread;            /* 23: terrain_ore_spread_t */
} terrain_ore_row_t;
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
    const terrain_density_t *borrowed_density;      /* 352: where a column's ground is */
    uint64_t strata_seed;                           /* 360 */
} terrain_ore_t;
static_assert(sizeof(terrain_ore_t) == 432);

/* Takes the ores from arena, for this world's seed. For use while terrain_init runs. density must outlive it. */
void terrain_ore_init(terrain_ore_t **out, arena_t *arena, uint64_t seed, const terrain_density_t *density);

/* Empties the handle. The memory goes back with the arena. */
void terrain_ore_deinit(terrain_ore_t *ore);

/* The table: TERRAIN_ORE_ROWS rows, in the order they are laid down. The same for every world. */
const terrain_ore_row_t *terrain_ore_table(void);

/* Lays the rock and the ores into blocks, which holds the box's voxels in x, then y, then z order, and which has the ground in
 * it already: stone turns to deepslate by depth, then each row in turn replaces rock. Air, water, lava, bedrock and anything
 * else not rock are left as they are, so caves cut first show their ore. Allocates nothing, and given the same blocks at a
 * voxel a value is a pure function of the ores and the voxel: splitting a box into parts, in any order, on any thread, gives
 * the same bytes. */
void terrain_ore_fill_box(const terrain_ore_t *ore, const terrain_density_box_t *box, uint16_t *blocks);

#endif
