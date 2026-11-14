/* See run/src/terrain/density/README.md. */
#ifndef MC_RUN_TERRAIN_DENSITY_INDEX_H
#define MC_RUN_TERRAIN_DENSITY_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "terrain/fields/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* terrain/landform's, which reads this header for its spline: only a pointer to it is held here. */
struct terrain_landform_t;

enum : uint32_t {
    /* The noise fields one world's density takes from the bank. */
    TERRAIN_DENSITY_NOISE_FIELDS = 5u,
    /* Knots one spline can have. */
    TERRAIN_DENSITY_SPLINE_KNOTS_MAX = 12u,
    /* Density is worked out on a lattice this many blocks apart and interpolated between. */
    TERRAIN_DENSITY_CELL_XY = 4u,
    TERRAIN_DENSITY_CELL_Z = 8u,
};

/* What the splines can reach, whatever the climate. tests/run/terrain/density/ tries every corner of the climate against
 * these, and real columns of real seeds. Each is written out from the numbers in the README's table. Since plan 007 phase
 * 04 the top is base 100, plus the plateau and the range on it, which together reach at most 1000, plus the summit rock 40
 * and hills 30: 1170. A knob (90) stands only off a range, where it adds to at most 100 + 650 + 90 + 30 = 870. The limit
 * stays at 1190, the sum before plan 007, so that the world's top, the stone limit and the room for trees above it do not
 * move. The bottom is the sea bed's -110 less a sea hill's 6. */
enum : int32_t {
    TERRAIN_DENSITY_HEIGHT_MIN = -116,
    TERRAIN_DENSITY_HEIGHT_MAX = 1190,
    TERRAIN_DENSITY_SOFTNESS_MIN = 4,
    TERRAIN_DENSITY_SOFTNESS_MAX = 26,
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
    terrain_density_spline_t soft;                /* 296: a range's envelope to its share of height on rolling ground */
    terrain_density_spline_t sharp;               /* 592: the same on alpine ground */
    terrain_density_spline_t koppie;              /* 888: a knob's t to its share of height, flat-topped */
    terrain_density_spline_t valley;              /* 1184: ridges to what is left of the land's height */
    terrain_density_spline_t hill_land;           /* 1480: continentalness to how much hills show on land */
    terrain_density_spline_t hill_sea;            /* 1776: continentalness to how much they show on the sea bed */
    terrain_density_spline_t hill_relief;         /* 2072: erosion to how big the hills are */
    terrain_density_spline_t flank;               /* 2368: nearness to a range's line to how much range is there */
    terrain_density_spline_t massif;              /* 2664: the massif field to how tall that stretch stands */
    terrain_density_spline_t crest;               /* 2960: nearness to a crest to the share of the range's height */
    terrain_density_spline_t range_land;          /* 3256: broad continentalness to how much range the land allows */
    terrain_density_spline_t wetland;             /* 3552: erosion to how far a valley's floor sinks */
    terrain_density_spline_t basin;               /* 3848: how far into a dry basin to its share of the full sinking */
    const terrain_fields_t *borrowed_fields;      /* 4144 */
    const terrain_noise_field_t *borrowed_hills;  /* 4152 */
    const terrain_noise_field_t *borrowed_detail; /* 4160: the one field that changes with height */
    const terrain_noise_field_t *borrowed_range;  /* 4168 */
    const terrain_noise_field_t *borrowed_massif; /* 4176 */
    const terrain_noise_field_t *borrowed_crest;  /* 4184 */
    const struct terrain_landform_t *borrowed_landform; /* 4192: plateaus, ruggedness, knobs, summits and basins */
} terrain_density_t;
static_assert(sizeof(terrain_density_t) == 4200);

/* What a column's ground is. */
typedef struct terrain_density_profile_t {
    float32_t height;   /* 0: in blocks, where the ground would be with no 3D noise */
    float32_t softness; /* 4: in blocks: the 3D noise can move the ground this far either way */
    float32_t lift; /* 8: in blocks: the plateau under it less a dry basin's sinking. Biomes read height less this */
    float32_t
        rivers;     /* 12: from 0 to 1, how much of a river hydrology may cut here: 0 on a dry basin's rim and in it */
    float32_t ring; /* 16: the ring field R of the README's "Dry basins": the rim is where it is 0.72 or more */
    float32_t knob; /* 20: in blocks, what a knob adds here: for tools that read the ground without its knobs */
    uint8_t dry;    /* 24: 1 in a dry basin: what is open at or below the sea's level there is air, not water */
    uint8_t _pad0[7]; /* 25 */
} terrain_density_profile_t;
static_assert(sizeof(terrain_density_profile_t) == 32);

/* The noise a profile takes that is not climate, each already spread out to -1 to 1. */
typedef struct terrain_density_inputs_t {
    float32_t continentalness; /* 0 */
    float32_t erosion;         /* 4 */
    float32_t ridges;          /* 8 */
    float32_t hills;           /* 12 */
    float32_t peak;  /* 16: the summit field; its ridged form 1 - |peak| is used. 0 where the envelope is 0.6 or less */
    float32_t range; /* 20: 0 on a range's line */
    float32_t massif;    /* 24 */
    float32_t crest;     /* 28: 0 on a crest */
    float32_t broad;     /* 32: continentalness from its largest octaves alone */
    float32_t plateau;   /* 36: from 0 to 1, a share of TERRAIN_LANDFORM_PLATEAU_BLOCKS: terrain/landform's */
    float32_t face;      /* 40: from 0 to 1, how steep the plateau is here: terrain/landform's */
    float32_t rugged;    /* 44: G, from 0 to 1, how sharp the region's mountains are: terrain/landform's */
    float32_t knobs;     /* 48: K, from -1 to 1: a knob stands where it is over 0.55. 0 in the sea */
    float32_t knob_size; /* 52: from 0 to 1, how tall a knob here is. 0 in the sea */
    float32_t basin;     /* 56: B, from -1 to 1: a dry basin sinks where it is high, inland and off the plateaus */
    float32_t inland;    /* 60: broad continentalness blended from nodes 2048 blocks apart: what keeps basins inland */
    float32_t uplift;    /* 64: U, from -1 to 1, terrain/landform's: basins sink only where the land is not lifted */
    uint8_t _pad0[4];    /* 68 */
} terrain_density_inputs_t;
static_assert(sizeof(terrain_density_inputs_t) == 72);

/* Where a profile's values go, each array holding one value a column. heights and softness are always written; the others
 * only when they are not nullptr. */
typedef struct terrain_density_profile_arrays_t {
    float32_t *heights;  /* 0 */
    float32_t *softness; /* 8 */
    float32_t *lift;     /* 16 */
    float32_t *rivers;   /* 24 */
    uint8_t *dry;        /* 32 */
    float32_t *ring;     /* 40: for tests and tools */
    float32_t *knob;     /* 48: what a knob adds, for tools (bench_landform's plateau surface) */
} terrain_density_profile_arrays_t;
static_assert(sizeof(terrain_density_profile_arrays_t) == 56);

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

/* Takes the density from arena and adds its fields to bank, for bank's seed. For use while terrain_init runs. fields and
 * landform must outlive it. If the bank refuses a field, which has logged why, sets *out to nullptr and returns false. Fields added before
 * that stay in the bank, so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_density_init(terrain_density_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                                        const terrain_fields_t *fields, const struct terrain_landform_t *landform);

/* Empties the handle. The memory goes back with the arena. */
void terrain_density_deinit(terrain_density_t *density);

/* Builds a spline through count knots (2 to TERRAIN_DENSITY_SPLINE_KNOTS_MAX), xs rising, with the same slopes terrain_density's
 * own splines have. For anything that wants that curve on its own knots, such as the climate's tables (plan 004 phase 04). */
void terrain_density_spline_build(terrain_density_spline_t *spline, const float64_t *xs, const float64_t *ys,
                                  uint32_t count);

/* A spline at x. Outside its knots it stays at the end's value. */
float64_t terrain_density_spline_at(const terrain_density_spline_t *spline, float64_t x);

/* The same spline's slope at x, dy/dx of the very cubic terrain_density_spline_at works out: 0 outside its knots. Plain
 * arithmetic, no libm. */
float64_t terrain_density_spline_slope_at(const terrain_density_spline_t *spline, float64_t x);

/* A range's share of its height at envelope m (0 to 1) for ruggedness G (0 to 1): (1 - G) soft(m) + G sharp(m). */
float64_t terrain_density_shape_at(const terrain_density_t *density, float64_t envelope, float64_t rugged);

/* A knob's share of its height at t (0 at its foot, 1 at its top) for ruggedness G: a dome, 1 - (1 - t)^2, blended by G
 * with a flat-topped koppie. */
float64_t terrain_density_knob_shape_at(const terrain_density_t *density, float64_t t, float64_t rugged);

/* The height splines: from noise already spread out to the ground it makes, in the README's formula. */
terrain_density_profile_t terrain_density_profile_from(const terrain_density_t *density,
                                                       const terrain_density_inputs_t *inputs);

/* A column's ground at each column of an area, as the climate lays one out. For anything that wants the shape of the land
 * without its blocks, such as hydrology and biomes. Allocates nothing, and each value is a pure function of the density and
 * the column. height and softness each hold terrain_fields_area_columns(area) values. */
void terrain_density_profile_area(const terrain_density_t *density, const terrain_fields_area_t *area,
                                  float32_t *heights, float32_t *softness);
/* The same, and each column's lift into lift, which holds as many: for what reads relief and not height, such as biomes. */
void terrain_density_profile_area_lift(const terrain_density_t *density, const terrain_fields_area_t *area,
                                       float32_t *heights, float32_t *softness, float32_t *lift);
/* The same, with every value the profile has: for hydrology, which also reads where rivers may cut and what is dry. */
void terrain_density_profile_area_arrays(const terrain_density_t *density, const terrain_fields_area_t *area,
                                         const terrain_density_profile_arrays_t *out);

/* The profile at count columns anywhere, in blocks, through the same batch the box uses: the very bits a box's lattice
 * gets at a whole-number column. Allocates nothing. Must be finite. */
void terrain_density_profile_points(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                    uint64_t count, float32_t *heights, float32_t *softness);
/* The same, and each column's lift into lift. */
void terrain_density_profile_points_lift(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                         uint64_t count, float32_t *heights, float32_t *softness, float32_t *lift);
/* The same, with every value the profile has. */
void terrain_density_profile_points_arrays(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                           uint64_t count, const terrain_density_profile_arrays_t *out);

/* How many values a box fills. */
uint64_t terrain_density_box_voxels(const terrain_density_box_t *box);

/* Fills out with the density at each voxel: above 0 is ground. Any z may be asked: the world's limits are not in this number,
 * they are in the blocks. Allocates nothing, and a value is a pure function of the density and the voxel: splitting a box
 * into parts, in any order, on any thread, gives the same bits. */
void terrain_density_sample_box(const terrain_density_t *density, const terrain_density_box_t *box, float32_t *out);

/* Fills out with each voxel's block id: bedrock at z = MC_WORLD_Z_MIN; stone where the density is above 0; water in what is
 * left, up to z = MC_WORLD_Z_SEA, but air where any of the four lattice columns round the voxel is dry (a dry basin); air
 * above that. Outside the world's limits it is air, so a box may cover whole sections. As
 * pure, and as free of allocation, as the numbers. */
void terrain_density_fill_box(const terrain_density_t *density, const terrain_density_box_t *box, uint16_t *out);

/* The same at one voxel, for something that asks about a single place. Voxels in bulk cost far less each through a box. */
float32_t terrain_density_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z);
uint16_t terrain_density_block_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z);

#endif
