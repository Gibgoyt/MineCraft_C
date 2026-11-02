/* See run/src/terrain/README.md. */
#ifndef MC_RUN_TERRAIN_INDEX_H
#define MC_RUN_TERRAIN_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "terrain/biome/index.h"
#include "terrain/cave/index.h"
#include "terrain/climate/index.h"
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
};
static_assert(TERRAIN_NOISE_FIELDS_MAX * sizeof(terrain_noise_field_t) + sizeof(terrain_climate_t) +
                  sizeof(terrain_density_t) + sizeof(terrain_hydrology_t) + sizeof(terrain_cave_t) +
                  sizeof(terrain_ore_t) + sizeof(terrain_biome_t) + sizeof(terrain_surface_t) +
                  sizeof(terrain_feature_t) <
              TERRAIN_TABLES_BYTES);
static_assert((uint32_t)TERRAIN_CLIMATE_NOISE_FIELDS + (uint32_t)TERRAIN_DENSITY_NOISE_FIELDS +
                  (uint32_t)TERRAIN_HYDROLOGY_NOISE_FIELDS + (uint32_t)TERRAIN_CAVE_NOISE_FIELDS +
                  (uint32_t)TERRAIN_SURFACE_NOISE_FIELDS <=
              (uint32_t)TERRAIN_NOISE_FIELDS_MAX);

/* All of one world's terrain, for its one seed. There is one, under app_t. After terrain_init nothing changes it, so any
 * number of workers may read it, and that is why they all get the same bytes. */
typedef struct terrain_t {
    arena_t arena_tables;                    /* 0: over owned_tables, lent to the parts; World lifetime */
    heap_region_t owned_tables;              /* 64 */
    terrain_noise_bank_t noise_bank;         /* 80 */
    uint64_t seed;                           /* 104 */
    terrain_climate_t *borrowed_climate;     /* 112: in arena_tables */
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
static_assert(offsetof(terrain_t, borrowed_climate) == 112);
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
 * then y, then z order, and opened that many bytes the caller lends for working. Allocates nothing, and a voxel is a pure function
 * of the terrain and the voxel: splitting a box into parts, in any order, on any thread, gives the same bytes. */
void terrain_generate_box(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                          uint16_t *blocks);

#endif
