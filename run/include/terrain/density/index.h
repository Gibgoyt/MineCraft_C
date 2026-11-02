/* See run/src/terrain/density/README.md. */
#ifndef MC_RUN_TERRAIN_DENSITY_INDEX_H
#define MC_RUN_TERRAIN_DENSITY_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "terrain/climate/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The noise fields one world's density takes from the bank. */
    TERRAIN_DENSITY_NOISE_FIELDS = 3u,
    /* Knots one spline can have. */
    TERRAIN_DENSITY_SPLINE_KNOTS_MAX = 12u,
    /* Density is worked out on a lattice this many blocks apart and interpolated between. */
    TERRAIN_DENSITY_CELL_XY = 4u,
    TERRAIN_DENSITY_CELL_Z = 8u,
};

/* What the splines can reach, whatever the climate. tests/run/terrain/density/ tries every corner of the climate against
 * these, and real columns of real seeds. Each is written out from the numbers in the README's table. */
enum : int32_t {
    TERRAIN_DENSITY_HEIGHT_MIN = -116,
    TERRAIN_DENSITY_HEIGHT_MAX = 454,
    TERRAIN_DENSITY_SOFTNESS_MIN = 4,
    TERRAIN_DENSITY_SOFTNESS_MAX = 30,
    /* From the bedrock's layer up to here is always stone before caves cut it. */
    TERRAIN_DENSITY_ALWAYS_SOLID_TOP =
        TERRAIN_DENSITY_HEIGHT_MIN - TERRAIN_DENSITY_SOFTNESS_MAX - (int32_t)TERRAIN_DENSITY_CELL_Z - 1,
    /* From here up is never stone. The room above it is for what grows on the ground: Stage 10.7. */
    TERRAIN_DENSITY_NEVER_SOLID_FROM =
        TERRAIN_DENSITY_HEIGHT_MAX + TERRAIN_DENSITY_SOFTNESS_MAX + (int32_t)TERRAIN_DENSITY_CELL_Z,
};
static_assert(TERRAIN_DENSITY_ALWAYS_SOLID_TOP > MC_WORLD_Z_MIN);
static_assert(TERRAIN_DENSITY_NEVER_SOLID_FROM <= MC_WORLD_Z_MAX);
static_assert(TERRAIN_DENSITY_HEIGHT_MIN < MC_WORLD_Z_SEA && TERRAIN_DENSITY_HEIGHT_MAX > MC_WORLD_Z_SEA);

/* A curve through knots, never going above or below its knots: see the README. */
typedef struct terrain_density_spline_t {
    float64_t x[TERRAIN_DENSITY_SPLINE_KNOTS_MAX];     /* 0: rising */
    float64_t y[TERRAIN_DENSITY_SPLINE_KNOTS_MAX];     /* 96 */
    float64_t slope[TERRAIN_DENSITY_SPLINE_KNOTS_MAX]; /* 192 */
    uint32_t count;                                    /* 288 */
    uint8_t _pad0[4];                                  /* 292 */
} terrain_density_spline_t;
static_assert(sizeof(terrain_density_spline_t) == 296);

/* One world's density. Nothing changes it after terrain_density_init, so any number of workers may read it. */
typedef struct terrain_density_t {
    terrain_density_spline_t continental;         /* 0: continentalness to blocks */
    terrain_density_spline_t inland;              /* 296: continentalness to how much mountain the ground allows */
    terrain_density_spline_t relief;              /* 592: erosion to how high the high ground goes */
    terrain_density_spline_t peak;                /* 888: ridges to the share of a mountain's height */
    terrain_density_spline_t valley;              /* 1184: ridges to what is left of the land's height */
    terrain_density_spline_t hill_land;           /* 1480: continentalness to how much hills show on land */
    terrain_density_spline_t hill_sea;            /* 1776: continentalness to how much they show on the sea bed */
    terrain_density_spline_t hill_relief;         /* 2072: erosion to how big the hills are */
    const terrain_climate_t *borrowed_climate;    /* 2368 */
    const terrain_noise_field_t *borrowed_hills;  /* 2376 */
    const terrain_noise_field_t *borrowed_jagged; /* 2384 */
    const terrain_noise_field_t *borrowed_detail; /* 2392: the one field that changes with height */
} terrain_density_t;
static_assert(sizeof(terrain_density_t) == 2400);

/* What a column's ground is. */
typedef struct terrain_density_profile_t {
    float32_t height;   /* 0: in blocks, where the ground would be with no 3D noise */
    float32_t softness; /* 4: in blocks: the 3D noise can move the ground this far either way */
} terrain_density_profile_t;
static_assert(sizeof(terrain_density_profile_t) == 8);

/* The noise a profile takes that is not climate, each already spread out to -1 to 1. */
typedef struct terrain_density_inputs_t {
    float32_t continentalness; /* 0 */
    float32_t erosion;         /* 4 */
    float32_t ridges;          /* 8 */
    float32_t hills;           /* 12 */
    float32_t jagged;          /* 16 */
} terrain_density_inputs_t;
static_assert(sizeof(terrain_density_inputs_t) == 20);

/* Blocks next to each other. Filled with x changing fastest, then y, then z, as a section's voxels are stored:
 * out[ix + count_x * (iy + count_y * iz)] is the voxel origin + i. */
typedef struct terrain_density_box_t {
    int32_t origin_x; /* 0 */
    int32_t origin_y; /* 4 */
    int32_t origin_z; /* 8 */
    uint32_t count_x; /* 12 */
    uint32_t count_y; /* 16 */
    uint32_t count_z; /* 20 */
} terrain_density_box_t;
static_assert(sizeof(terrain_density_box_t) == 24);

/* Takes the density from arena and adds its fields to bank, for bank's seed. For use while terrain_init runs. climate must
 * outlive it. If the bank refuses a field, which has logged why, sets *out to nullptr and returns false. Fields added before
 * that stay in the bank, so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_density_init(terrain_density_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                                        const terrain_climate_t *climate);

/* Empties the handle. The memory goes back with the arena. */
void terrain_density_deinit(terrain_density_t *density);

/* A spline at x. Outside its knots it stays at the end's value. */
float64_t terrain_density_spline_at(const terrain_density_spline_t *spline, float64_t x);

/* The height splines: from noise already spread out to the ground it makes, in the README's formula. */
terrain_density_profile_t terrain_density_profile_from(const terrain_density_t *density,
                                                       const terrain_density_inputs_t *inputs);

/* A column's ground at each column of an area, as the climate lays one out. For anything that wants the shape of the land
 * without its blocks, such as hydrology and biomes. Allocates nothing, and each value is a pure function of the density and
 * the column. height and softness each hold terrain_climate_area_columns(area) values. */
void terrain_density_profile_area(const terrain_density_t *density, const terrain_climate_area_t *area,
                                  float32_t *heights, float32_t *softness);

/* How many values a box fills. */
uint64_t terrain_density_box_voxels(const terrain_density_box_t *box);

/* Fills out with the density at each voxel: above 0 is ground. Any z may be asked: the world's limits are not in this number,
 * they are in the blocks. Allocates nothing, and a value is a pure function of the density and the voxel: splitting a box
 * into parts, in any order, on any thread, gives the same bits. */
void terrain_density_sample_box(const terrain_density_t *density, const terrain_density_box_t *box, float32_t *out);

/* Fills out with each voxel's block id: bedrock at z = MC_WORLD_Z_MIN; stone where the density is above 0; water in what is
 * left, up to z = MC_WORLD_Z_SEA; air above that. Outside the world's limits it is air, so a box may cover whole sections. As
 * pure, and as free of allocation, as the numbers. */
void terrain_density_fill_box(const terrain_density_t *density, const terrain_density_box_t *box, uint16_t *out);

/* The same at one voxel, for something that asks about a single place. Voxels in bulk cost far less each through a box. */
float32_t terrain_density_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z);
uint16_t terrain_density_block_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z);

#endif
