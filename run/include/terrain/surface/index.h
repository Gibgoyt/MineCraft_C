/* See run/src/terrain/surface/README.md. */
#ifndef MC_RUN_TERRAIN_SURFACE_INDEX_H
#define MC_RUN_TERRAIN_SURFACE_INDEX_H

#include "arena/index.h"
#include "terrain/biome/index.h"
#include "terrain/cave/index.h"
#include "terrain/density/index.h"
#include "terrain/hydrology/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The noise fields one world's surface takes from the bank. */
    TERRAIN_SURFACE_NOISE_FIELDS = 3u,
    /* How many layers the badlands' colours take to repeat. A power of 2. */
    TERRAIN_SURFACE_BANDS = 256u,
    /* The columns of one chunk-sized tile: the size of what terrain_surface_tile_t keeps. */
    TERRAIN_SURFACE_TILE_COLUMNS = 256u,
};
static_assert((TERRAIN_SURFACE_BANDS & (TERRAIN_SURFACE_BANDS - 1u)) == 0u);

/* One world's surface. Nothing changes it after terrain_surface_init, so any number of workers may read it. */
typedef struct terrain_surface_t {
    uint16_t bands[TERRAIN_SURFACE_BANDS];         /* 0: the badlands' colours, by height */
    const terrain_biome_t *borrowed_biome;         /* 512 */
    const terrain_hydrology_t *borrowed_hydrology; /* 520 */
    const terrain_cave_t *borrowed_cave;           /* 528 */
    const terrain_noise_field_t *borrowed_depth;   /* 536: how much filler a column has */
    const terrain_noise_field_t *borrowed_patch;   /* 544: what is in place of a biome's top */
    const terrain_noise_field_t *borrowed_wander;  /* 552: how the bands bend */
    uint32_t snow_seed;                            /* 560: the snowline's wander, terrain_surface_snow_wander */
    uint8_t _pad0[4];                              /* 564 */
} terrain_surface_t;
static_assert(sizeof(terrain_surface_t) == 568);

enum : int32_t {
    /* Plan 006 master 2.5: the snowline's ragged edge moves it up to this many blocks up or down. */
    TERRAIN_SURFACE_SNOW_WANDER_BLOCKS = 24,
    /* Plan 006 master 2.5: water freezes over where the coldest month at its top is below this, centi-degrees C. */
    TERRAIN_SURFACE_ICE_BELOW_CC = -200,
};

/* What decides a voxel's block. */
typedef struct terrain_surface_inputs_t {
    float32_t depth_noise; /* 0: from -1 to 1 */
    float32_t patch;       /* 4: from -1 to 1 */
    float32_t steepness;   /* 8: blocks of ground per block across */
    int32_t z;             /* 12 */
    int32_t band_z;        /* 16: z, moved by how the bands bend */
    uint32_t depth;        /* 20: solid voxels between this one and what is open above it */
    uint16_t block;        /* 24: what is there now */
    uint8_t wet;           /* 26: 1 if what is open above it is water */
    uint8_t _pad0[1];      /* 27 */
    int32_t snow_from;     /* 28: terrain_surface_snow_from: below it, the biome's snow and ice give way */
} terrain_surface_inputs_t;
static_assert(sizeof(terrain_surface_inputs_t) == 32);

/* What a chunk-sized tile needs that does not change with the layers. Reading it costs far more than laying one slice, so what
 * asks for one chunk slice by slice reads it once. Filled by terrain_surface_tile_read; the caller lends the memory. */
typedef struct terrain_surface_tile_t {
    terrain_biome_column_t columns[TERRAIN_SURFACE_TILE_COLUMNS]; /* 0 */
    float32_t depth[TERRAIN_SURFACE_TILE_COLUMNS];                /* 6144: good once has_noise is 1 */
    float32_t patch[TERRAIN_SURFACE_TILE_COLUMNS];                /* 7168: the same */
    float32_t wander[TERRAIN_SURFACE_TILE_COLUMNS];               /* 8192: the same */
    float32_t steepness[TERRAIN_SURFACE_TILE_COLUMNS];            /* 9216 */
    float32_t solid_top;                                          /* 10240: at or under this, no voxel changes */
    float32_t open_from;                                          /* 10244: from here up, nothing is ground */
    uint8_t has_noise;                                            /* 10248 */
    uint8_t _pad0[7];                                             /* 10249 */
} terrain_surface_tile_t;
static_assert(sizeof(terrain_surface_tile_t) == 10256);

/* Takes the surface from arena and adds its fields to bank, for bank's seed, with the bands drawn from seed. For use while
 * terrain_init runs. What it is given must outlive it. If the bank refuses a field, which has logged why, sets *out to nullptr
 * and returns false. Fields added before that stay in the bank, so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_surface_init(terrain_surface_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                                        uint64_t seed, const terrain_biome_t *biome,
                                        const terrain_hydrology_t *hydrology, const terrain_cave_t *cave);

/* Empties the handle. The memory goes back with the arena. */
void terrain_surface_deinit(terrain_surface_t *surface);

/* Plan 006 master 2.5: the snowline's ragged edge at (x, y), in blocks from -TERRAIN_SURFACE_SNOW_WANDER_BLOCKS to
 * TERRAIN_SURFACE_SNOW_WANDER_BLOCKS: two octaves of hashed value noise, 64 and 16 blocks, all in integers. Pure. */
int32_t terrain_surface_snow_wander(uint32_t snow_seed, int64_t x, int64_t y);

/* The lowest z whose dry top lies under permanent snow at (x, y): anywhere (INT32_MIN) in the Ice family, nowhere
 * (INT32_MAX) where the snowline is TERRAIN_CLIMATE_NO_SNOW_LINE, and otherwise the snowline moved by its wander. Pure. */
int32_t terrain_surface_snow_from(const terrain_surface_t *surface, uint8_t family, int32_t snow_line_z, int64_t x,
                                  int64_t y);

/* Whether water whose top is at z freezes over for good: the coldest month there, from t_cold_cc at sea level by the lapse
 * (and the basin heat below sea level), is below TERRAIN_SURFACE_ICE_BELOW_CC. Pure. */
bool terrain_surface_freezes(int32_t t_cold_cc, int32_t z);

/* Whether a block is one of the frozen ones a biome's snow and ice stand for: snow_block, ice, packed_ice, blue_ice. */
bool terrain_surface_is_frozen(uint16_t block);

/* The README's rules: what one voxel of a biome becomes, as plain arithmetic. bands holds TERRAIN_SURFACE_BANDS colours.
 * Plan 006 phase 04: a dry voxel the rules make frozen, under inputs' snow_from, takes the biome's below_top (the top) or
 * below_filler (deeper) instead, and a frozen high block starts no lower than snow_from. */
uint16_t terrain_surface_block_from(const biome_def_t *def, const uint16_t *bands,
                                    const terrain_surface_inputs_t *inputs);

/* The README's rule under the ground: what a rock voxel of an underground biome becomes, given that a cave lies directly above
 * it (floor) or below it (ceiling), or neither. */
uint16_t terrain_surface_cave_block_from(const biome_def_t *def, uint16_t block, float32_t patch, bool cave_above,
                                         bool cave_below);

/* Covers the ground. blocks holds the box's voxels as terrain_hydrology_fill_box left them, in x, then y, then z order, and
 * they become what each column's biome lays on them. Allocates nothing, and a voxel is a pure function of the surface and the
 * voxel: splitting a box into parts, in any order, on any thread, gives the same bytes. Call it before anything else changes
 * blocks: see the README's order. */
/* Plan 006 phase 03: climate is the sample round the box (terrain_biome_columns_area), or nullptr for the fallback. */
void terrain_surface_fill_box(const terrain_surface_t *surface, const terrain_climate_sample_t *climate,
                              const terrain_density_box_t *box, uint16_t *blocks);

/* The same as terrain_surface_fill_box, byte for byte, for a box exactly one chunk across at the place tile was read for: any
 * layers, any number of calls, with the tile read once. x and y are the chunk's lowest corner. */
void terrain_surface_tile_read(const terrain_surface_t *surface, const terrain_climate_sample_t *climate, int32_t x,
                               int32_t y, terrain_surface_tile_t *tile);
void terrain_surface_fill_tile_box(const terrain_surface_t *surface, terrain_surface_tile_t *tile,
                                   const terrain_density_box_t *box, uint16_t *blocks);

/* Lines the caves of the underground biomes. Any stone voxel of blocks directly above or below what a cave wants takes the
 * biome's floor or ceiling. Nothing else changes. Call it after the caves are cut and flooded, and before terrain_ore_fill_box.
 * As pure and as free of allocation as the fill. */
void terrain_surface_line_caves_box(const terrain_surface_t *surface, const terrain_climate_sample_t *climate,
                                    const terrain_density_box_t *box, uint16_t *blocks);

/* The same blocks, from a mask of this very box where mask != 0 is what terrain_cave_mask_box said, so that the caves are asked again only for the layers just past
 * its two ends. On a tall column of a range, asking again was the third of its time. */
void terrain_surface_line_caves_with_mask(const terrain_surface_t *surface, const terrain_climate_sample_t *climate,
                                          const terrain_density_box_t *box, const uint8_t *mask, uint16_t *blocks);

#endif
