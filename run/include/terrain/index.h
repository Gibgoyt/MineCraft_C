/* See run/src/terrain/README.md. */
#ifndef MC_RUN_TERRAIN_INDEX_H
#define MC_RUN_TERRAIN_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "terrain/climate/index.h"
#include "terrain/density/index.h"
#include "terrain/hydrology/index.h"
#include "terrain/noise/index.h"
#include "terrain/rng/index.h"

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
                  sizeof(terrain_density_t) + sizeof(terrain_hydrology_t) <
              TERRAIN_TABLES_BYTES);
static_assert((uint32_t)TERRAIN_CLIMATE_NOISE_FIELDS + (uint32_t)TERRAIN_DENSITY_NOISE_FIELDS +
                  (uint32_t)TERRAIN_HYDROLOGY_NOISE_FIELDS <=
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
    uint8_t _pad0[56];                       /* 136 */
} terrain_t;
static_assert(sizeof(terrain_t) == 192);
static_assert(alignof(terrain_t) == 64);
static_assert(offsetof(terrain_t, owned_tables) == 64);
static_assert(offsetof(terrain_t, noise_bank) == 80);
static_assert(offsetof(terrain_t, seed) == 104);
static_assert(offsetof(terrain_t, borrowed_climate) == 112);
static_assert(offsetof(terrain_t, borrowed_density) == 120);
static_assert(offsetof(terrain_t, borrowed_hydrology) == 128);

/* Reserves the region and starts the parts, for this seed. On failure logs why, leaves nothing behind and returns false. A
 * world with another seed is a terrain_deinit and another terrain_init: nothing computed from the old seed may survive. */
[[nodiscard]] bool terrain_init(terrain_t *terrain, uint64_t seed);

/* Releases what init reserved. Safe on a zeroed terrain_t. No worker may still be reading it. */
void terrain_deinit(terrain_t *terrain);

#endif
