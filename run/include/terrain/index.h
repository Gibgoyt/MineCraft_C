/* See run/src/terrain/README.md. */
#ifndef MC_RUN_TERRAIN_INDEX_H
#define MC_RUN_TERRAIN_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "terrain/biome/index.h"
#include "terrain/cave/index.h"
#include "terrain/fields/index.h"
#include "terrain/density/index.h"
#include "terrain/feature/index.h"
#include "terrain/hydrology/index.h"
#include "terrain/noise/index.h"
#include "terrain/ore/index.h"
#include "terrain/rng/index.h"
#include "terrain/surface/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint64_t {
    /* The world's seed until a menu or a save gives one (Stages 14.4, 16.1). */
    TERRAIN_SEED_DEFAULT = 0x4D494E4543524146ull,
    /* What everything computed once per world takes. Pages cost nothing until touched. */
    TERRAIN_TABLES_BYTES = HEAP_REGION_ALIGNMENT,
};
enum : uint32_t {
    /* Noise fields one world can have. */
    TERRAIN_NOISE_FIELDS_MAX = 64u,
    /* Which land a seed makes. Raised by any change that gives any seed other blocks: a save holds only what changed, so a world saved on other land is refused, and
     * never stitched to a different landscape. tests/run/terrain/feature/ holds this number to the land. */
    TERRAIN_GENERATOR_VERSION = 1u,
};
static_assert(TERRAIN_NOISE_FIELDS_MAX * sizeof(terrain_noise_field_t) + sizeof(terrain_fields_t) +
                  sizeof(terrain_density_t) + sizeof(terrain_hydrology_t) + sizeof(terrain_cave_t) +
                  sizeof(terrain_ore_t) + sizeof(terrain_biome_t) + sizeof(terrain_surface_t) +
                  sizeof(terrain_feature_t) <
              TERRAIN_TABLES_BYTES);
static_assert((uint32_t)TERRAIN_FIELDS_NOISE_FIELDS + (uint32_t)TERRAIN_DENSITY_NOISE_FIELDS +
                  (uint32_t)TERRAIN_HYDROLOGY_NOISE_FIELDS + (uint32_t)TERRAIN_CAVE_NOISE_FIELDS +
                  (uint32_t)TERRAIN_SURFACE_NOISE_FIELDS <=
              (uint32_t)TERRAIN_NOISE_FIELDS_MAX);

/* Nothing a stage makes is above this: what grows, at its tallest, on the highest ground there can be. It follows the density's own limit. */
enum : int32_t {
    TERRAIN_MADE_TOP = TERRAIN_DENSITY_NEVER_SOLID_FROM + TERRAIN_FEATURE_TALLEST,
};
static_assert(TERRAIN_MADE_TOP <= MC_WORLD_Z_MAX);

/* All of one world's terrain, for its one seed. There is one, under app_t. After terrain_init nothing changes it, so any
 * number of workers may read it, and that is why they all get the same bytes. */
typedef struct terrain_t {
    arena_t arena_tables;                    /* 0: over owned_tables, lent to the parts; World lifetime */
    heap_region_t owned_tables;              /* 64 */
    terrain_noise_bank_t noise_bank;         /* 80 */
    uint64_t seed;                           /* 104 */
    terrain_fields_t *borrowed_fields;       /* 112: in arena_tables */
    terrain_density_t *borrowed_density;     /* 120: in arena_tables */
    terrain_hydrology_t *borrowed_hydrology; /* 128: in arena_tables */
    terrain_cave_t *borrowed_cave;           /* 136: in arena_tables */
    terrain_ore_t *borrowed_ore;             /* 144: in arena_tables */
    terrain_biome_t *borrowed_biome;         /* 152: in arena_tables */
    terrain_surface_t *borrowed_surface;     /* 160: in arena_tables */
    terrain_feature_t *borrowed_feature;     /* 168: in arena_tables */
    uint8_t _pad0[16];                       /* 176 */
} terrain_t;
static_assert(sizeof(terrain_t) == 192);
static_assert(alignof(terrain_t) == 64);
static_assert(offsetof(terrain_t, owned_tables) == 64);
static_assert(offsetof(terrain_t, noise_bank) == 80);
static_assert(offsetof(terrain_t, seed) == 104);
static_assert(offsetof(terrain_t, borrowed_fields) == 112);
static_assert(offsetof(terrain_t, borrowed_density) == 120);
static_assert(offsetof(terrain_t, borrowed_hydrology) == 128);
static_assert(offsetof(terrain_t, borrowed_cave) == 136);
static_assert(offsetof(terrain_t, borrowed_ore) == 144);
static_assert(offsetof(terrain_t, borrowed_biome) == 152);
static_assert(offsetof(terrain_t, borrowed_surface) == 160);
static_assert(offsetof(terrain_t, borrowed_feature) == 168);

/* Reserves the region and starts the parts, for this seed. On failure logs why, leaves nothing behind and returns false. A
 * world with another seed is a terrain_deinit and another terrain_init: nothing computed from the old seed may survive. */
[[nodiscard]] bool terrain_init(terrain_t *terrain, uint64_t seed);

/* Releases what init reserved. Safe on a zeroed terrain_t. No worker may still be reading it. */
void terrain_deinit(terrain_t *terrain);

/* Every stage in its order, for one box: the blocks of the world there. blocks holds terrain_density_box_voxels(box) values in x,
 * then y, then z order, and opened that many bytes the caller lends for working, which it leaves as TERRAIN_CAVE_WANTED_UNCUT where a cave wanted a voxel that held nothing to cut, and
 * some other bit set where a cave cut one: test it with ~TERRAIN_CAVE_WANTED_UNCUT, not against 0. Allocates nothing, and a voxel is a pure function
 * of the terrain and the voxel: splitting a box into parts, in any order, on any thread, gives the same bytes. */
void terrain_generate_box(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                          uint16_t *blocks);

/* The ground the terrain's 2D splines give at a column, with no 3D density and no chunk: the profile at the four lattice
 * columns round it (TERRAIN_DENSITY_CELL_XY apart), blended along x, then y, as terrain_density's boxes blend them. A pure
 * function of the terrain and the column. The climate reads the ground through this and nothing else: plan 004, 6.1. */
terrain_density_profile_t terrain_height_estimate_profile(const terrain_t *terrain, int32_t x, int32_t y);
float32_t terrain_height_estimate(const terrain_t *terrain, int32_t x, int32_t y); /* .height of the above */

/* The same at count_x by count_y columns step blocks apart, x changing fastest. origin_x, origin_y and step must be whole
 * multiples of TERRAIN_DENSITY_CELL_XY (asserted): every column is then a lattice column, the blend's fractions are 0, and
 * each value is bit for bit the single-column answer. Allocates nothing: works through the stack in pieces of 128. */
void terrain_height_estimate_area(const terrain_t *terrain, int32_t origin_x, int32_t origin_y, int32_t step,
                                  uint32_t count_x, uint32_t count_y, float32_t *heights);

#endif
