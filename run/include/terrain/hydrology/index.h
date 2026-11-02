/* See run/src/terrain/hydrology/README.md. */
#ifndef MC_RUN_TERRAIN_HYDROLOGY_INDEX_H
#define MC_RUN_TERRAIN_HYDROLOGY_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "terrain/climate/index.h"
#include "terrain/density/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The noise fields one world's hydrology takes from the bank. */
    TERRAIN_HYDROLOGY_NOISE_FIELDS = 5u,
    /* An aquifer cell is a cube this many blocks a side, lined up on the world. */
    TERRAIN_HYDROLOGY_AQUIFER_CELL = 32u,
};

enum : int32_t {
    /* The top block of the lava that lies under the whole world, for any cave that gets down to it. */
    TERRAIN_HYDROLOGY_LAVA_TOP = MC_WORLD_Z_MIN + 20,
    /* An opened voxel less than this far below the ground takes its water from the sea, and not from an aquifer. */
    TERRAIN_HYDROLOGY_SURFACE_BAND = 12,
    /* No aquifer holds lava above this, and no opened voxel near the surface is as low. So the sea never meets lava. */
    TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP = -160,
    /* A river's bed reaches from this far below the sea to the next. */
    TERRAIN_HYDROLOGY_BED_SHALLOWEST = -3,
    TERRAIN_HYDROLOGY_BED_DEEPEST = -7,
    /* Sand stops at or below this, whatever the shore. */
    TERRAIN_HYDROLOGY_SAND_TOP_MAX = 5,
    /* The thickest layer of sand and sandstone, in blocks. */
    TERRAIN_HYDROLOGY_LAYERS_MAX = 6,
};
static_assert(TERRAIN_HYDROLOGY_LAVA_TOP == -230);
static_assert(TERRAIN_HYDROLOGY_LAVA_TOP < TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP);
static_assert(TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP + 1 <
              TERRAIN_DENSITY_HEIGHT_MIN - TERRAIN_DENSITY_SOFTNESS_MAX - TERRAIN_HYDROLOGY_SURFACE_BAND);
static_assert((int32_t)TERRAIN_HYDROLOGY_LAVA_TOP <= (int32_t)TERRAIN_DENSITY_ALWAYS_SOLID_TOP);

/* What a column is, for whoever chooses biomes. */
typedef enum terrain_hydrology_kind_t : uint8_t {
    TERRAIN_HYDROLOGY_KIND_LAND = 0,
    TERRAIN_HYDROLOGY_KIND_OCEAN = 1,
    TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN = 2,
    TERRAIN_HYDROLOGY_KIND_RIVER = 3,
    TERRAIN_HYDROLOGY_KIND_LAKE = 4,
    TERRAIN_HYDROLOGY_KIND_BEACH = 5,
    TERRAIN_HYDROLOGY_KIND_STONY_SHORE = 6,
    TERRAIN_HYDROLOGY_KIND_COUNT = 7,
} terrain_hydrology_kind_t;

/* One world's hydrology. Nothing changes it after terrain_hydrology_init, so any number of workers may read it. */
typedef struct terrain_hydrology_t {
    const terrain_density_t *borrowed_density;   /* 0 */
    const terrain_climate_t *borrowed_climate;   /* 8 */
    const terrain_noise_field_t *borrowed_width; /* 16: how wide and deep a river is */
    const terrain_noise_field_t *borrowed_shore; /* 24: where sand, clay and gravel lie */
    const terrain_noise_field_t *borrowed_flood; /* 32: which aquifer cells hold anything */
    const terrain_noise_field_t *borrowed_level; /* 40: how high it comes */
    const terrain_noise_field_t *borrowed_lava;  /* 48: which of the deep ones hold lava */
    uint64_t aquifer_seed;                       /* 56: where the cells' sites are */
} terrain_hydrology_t;
static_assert(sizeof(terrain_hydrology_t) == 64);

/* What decides a column, each already spread out to -1 to 1 where it is noise. */
typedef struct terrain_hydrology_inputs_t {
    float32_t height;          /* 0: terrain_density_profile_t's */
    float32_t softness;        /* 4: the same */
    float32_t continentalness; /* 8 */
    float32_t weirdness;       /* 12: a river runs along where this is 0 */
    float32_t width;           /* 16 */
    float32_t shore;           /* 20 */
    float32_t steepness;       /* 24: in blocks of ground per block across */
} terrain_hydrology_inputs_t;
static_assert(sizeof(terrain_hydrology_inputs_t) == 28);

/* What a column came to. */
typedef struct terrain_hydrology_column_t {
    float32_t height;              /* 0: terrain_density_profile_t's */
    float32_t softness;            /* 4: the same */
    float32_t ground;              /* 8: height, after the river has cut it */
    float32_t carve_top;           /* 12: no ground above this. Above height + softness, it cuts nothing */
    float32_t river;               /* 16: from 0, none, to 1, the middle of the channel */
    float32_t shore;               /* 20: the input, kept for the blocks */
    terrain_hydrology_kind_t kind; /* 24 */
    uint8_t _pad0[3];              /* 25 */
} terrain_hydrology_column_t;
static_assert(sizeof(terrain_hydrology_column_t) == 28);

/* Takes the hydrology from arena and adds its fields to bank, for bank's seed. For use while terrain_init runs. density and
 * climate must outlive it. If the bank refuses a field, which has logged why, sets *out to nullptr and returns false. Fields
 * added before that stay in the bank, so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_hydrology_init(terrain_hydrology_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                                          const terrain_density_t *density, const terrain_climate_t *climate);

/* Empties the handle. The memory goes back with the arena. */
void terrain_hydrology_deinit(terrain_hydrology_t *hydrology);

/* The README's formulas: from what decides a column to what it comes to, as plain arithmetic. */
terrain_hydrology_column_t terrain_hydrology_column_from(const terrain_hydrology_inputs_t *inputs);

/* Each column of an area, as the climate lays one out, but the steps must be above 0. columns holds
 * terrain_climate_area_columns(area) values. Allocates nothing, and each value is a pure function of the hydrology and the
 * column. */
void terrain_hydrology_columns_area(const terrain_hydrology_t *hydrology, const terrain_climate_area_t *area,
                                    terrain_hydrology_column_t *columns);

/* The same at one column. Columns in bulk cost far less each through an area. */
terrain_hydrology_column_t terrain_hydrology_column_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y);

/* Fills out with each voxel's block, as terrain_density_fill_box lays them out, with the rivers cut, the sea and the rivers
 * filled, and sand, sandstone, gravel and clay on what lies under water and along the shore. What is left is for later stages.
 * Allocates nothing, and a voxel is a pure function of the hydrology and the voxel: splitting a box into parts, in any order,
 * on any thread, gives the same bytes. */
void terrain_hydrology_fill_box(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box, uint16_t *out);

/* The same at one voxel. Voxels in bulk cost far less each through a box. */
uint16_t terrain_hydrology_block_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y, int32_t z);

/* For whatever opens the ground after the fill, as a cave does. Each voxel of blocks whose opened is not 0 becomes what such a
 * place holds: air, water or lava, or stone where it would otherwise let two bodies of fluid meet. Any other voxel, and any
 * voxel outside the world or on the bedrock, stays as it is. So nothing that is put here can flow: see the README. The box's x
 * and y must be within 2^30 of 0. As pure, and as free of allocation, as the fill. */
void terrain_hydrology_flood_box(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                 const uint8_t *opened, uint16_t *blocks);

/* What one opened voxel holds. Voxels in bulk cost far less each through a box. */
uint16_t terrain_hydrology_fluid_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y, int32_t z);

#endif
