/* See run/src/terrain/biome/README.md. */
#ifndef MC_RUN_TERRAIN_BIOME_INDEX_H
#define MC_RUN_TERRAIN_BIOME_INDEX_H

#include "arena/index.h"
#include "tables/biomes/index.h"
#include "terrain/climate_sample/index.h"
#include "terrain/fields/index.h"
#include "terrain/hydrology/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : int32_t {
    /* An underground biome holds no voxel less than this far under its column's ground. */
    TERRAIN_BIOME_CAVE_ROOF = 16,
};
static_assert(BIOME_UNDERGROUND_COUNT <= 8u); /* terrain_biome_column_t's caves */

/* One world's biomes. Nothing changes it after terrain_biome_init, so any number of workers may read it. */
typedef struct terrain_biome_t {
    const terrain_fields_t *borrowed_fields;       /* 0 */
    const terrain_hydrology_t *borrowed_hydrology; /* 8 */
    uint64_t seed; /* 16: the world's, for the fallback climate where no sample is given */
} terrain_biome_t;
static_assert(sizeof(terrain_biome_t) == 24);

/* What decides a column's biome (plan 006 phase 03): the family from the climate at the column's own height, then among the
 * family's members the nearest box over the landform axes, variety and wetness. */
typedef struct terrain_biome_inputs_t {
    float32_t variety;         /* 0: the field's */
    float32_t wetness;         /* 4: the field's, biased by the yearly rain: terrain_biome_wetness */
    float32_t continentalness; /* 8 */
    float32_t erosion;         /* 12 */
    float32_t weirdness;       /* 16 */
    float32_t ground;          /* 20: terrain_hydrology_column_t's; the box reads terrain_biome_height(ground, lift) */
    float32_t lift;            /* 24: the same: the plateau under the column (plan 007 phase 03) */
    terrain_hydrology_kind_t kind; /* 28 */
    uint8_t family;                /* 29: a biome_family_t, terrain_biome_family's */
    uint8_t _pad0[2];              /* 30 */
    int32_t t_warm_cc;             /* 32: the climate at the column, for a biome's limits */
    int32_t t_cold_cc;             /* 36 */
    int32_t rain_mm_year;          /* 40 */
} terrain_biome_inputs_t;
static_assert(sizeof(terrain_biome_inputs_t) == 44);

/* What a column came to. */
typedef struct terrain_biome_column_t {
    float32_t ground;    /* 0: terrain_hydrology_column_t's */
    float32_t softness;  /* 4: the same */
    float32_t carve_top; /* 8: the same */
    uint8_t biome;       /* 12: a biome_id_t: the one on the surface */
    uint8_t caves;       /* 13: bit i says biome_underground[i]'s climate holds this column, at any z */
    uint8_t family;      /* 14: the biome_family_t its biome was chosen in */
    uint8_t dry;         /* 15: terrain_hydrology_column_t's: a dry basin, where nothing open holds water */
    /* Plan 006 phase 04: the climate the surface and features lay snow, ice and trees by, blended at the column's 4-block
     * lattice corner (z 0), as the biome reads it. */
    int16_t snow_line_z;   /* 16: blocks; TERRAIN_CLIMATE_NO_SNOW_LINE when snow never outlasts the summer */
    int16_t t_cold_cc;     /* 18: the coldest month's mean at sea level, centi-degrees C */
    uint16_t rain_mm_year; /* 20: yearly rain, mm, held to 65535 */
    uint8_t _pad1[2];      /* 22 */
} terrain_biome_column_t;
static_assert(sizeof(terrain_biome_column_t) == 24);
static_assert(offsetof(terrain_biome_column_t, snow_line_z) == 16);
static_assert(offsetof(terrain_biome_column_t, rain_mm_year) == 20);

/* Takes the biomes from arena. For use while terrain_init runs. climate and hydrology must outlive it. */
void terrain_biome_init(terrain_biome_t **out, arena_t *arena, uint64_t seed, const terrain_fields_t *fields,
                        const terrain_hydrology_t *hydrology);

/* Empties the handle. The memory goes back with the arena. */
void terrain_biome_deinit(terrain_biome_t *biome);

/* Plan 006 phase 03 and plan 007 phase 03: the **biome height**, the height a biome's landform box reads: the ground less the
 * plateau's lift under it, so a high plateau's grass counts as low ground, while the family still reads the absolute height
 * (terrain_biome_climate_z). The one place this is decided: hydrology's columns, the estimate and the tools all call it. */
float32_t terrain_biome_height(float32_t ground, float32_t lift);

/* The height a column's family is worked out at: the sea's top for ocean and deep ocean, where the climate is the water's,
 * and the ground, rounded down, for the rest. */
int32_t terrain_biome_climate_z(float32_t ground, terrain_hydrology_kind_t kind);

/* Plan 004 6.10's first match on a place's own numbers (terrain_climate_at at terrain_biome_climate_z): the class worked out at
 * the column's height, with Mediterranean where the nearest cell has a dry summer. Pure. */
biome_family_t terrain_biome_family(const terrain_climate_point_t *point);

/* The family a climate class names, for the survey; BIOME_FAMILY_COUNT for TERRAIN_CLIMATE_CLASS_NONE (sea). */
uint8_t terrain_biome_family_of_class(uint8_t climate_class);

/* The wetness a biome reads: the field plus half of the rain's bias, (rain - 1000 mm) / 1000 mm clamped to -1 to 1, the sum
 * clamped to -1 to 1. Plain float arithmetic. */
float32_t terrain_biome_wetness(float32_t field, int32_t rain_mm_year);

/* The README's choice, as plain arithmetic: how far inputs lies outside a row's box, its climate limits (t_warm, t_cold,
 * rain_mm, each only if the row gives it) counted as axes of 10 degrees and 1000 mm. 0 is inside it. */
float32_t terrain_biome_distance(const biome_def_t *def, const terrain_biome_inputs_t *inputs);

/* The README's choice: from what decides a column to its surface biome. Only rows of inputs' where and family are looked at,
 * and the nearest wins: never a biome of another family. */
biome_id_t terrain_biome_choose(const terrain_biome_inputs_t *inputs);

/* Which of biome_underground hold a column, as bits. */
uint8_t terrain_biome_caves_of(const terrain_biome_inputs_t *inputs);

/* The biome of the voxel at z in a column. */
biome_id_t terrain_biome_of_voxel(const terrain_biome_column_t *column, int32_t z);

/* Each column of an area, as the climate lays one out, but the steps must be above 0. columns holds
 * terrain_fields_area_columns(area) values. Allocates nothing, and each value is a pure function of the biomes, the climate
 * and the column: splitting an area into parts, in any order, on any thread, gives the same bytes.
 * Plan 006 phase 03: climate is the climate sample round the area (terrain_climate_at reads it; the column job's covers its
 * column and the 16 blocks round it), or nullptr where there is no climate: then each 16-block tile fills
 * terrain_climate_fallback's for itself, from the seed, so tests and benches need no sample. */
void terrain_biome_columns_area(const terrain_biome_t *biome, const terrain_climate_sample_t *climate,
                                const terrain_fields_area_t *area, terrain_biome_column_t *columns);

/* The same at one column, and one voxel. Columns in bulk cost far less each through an area. */
/* climate as for terrain_biome_columns_area: the sample round the column, or nullptr for the fallback. */
terrain_biome_column_t terrain_biome_column_at(const terrain_biome_t *biome, const terrain_climate_sample_t *climate,
                                               int32_t x, int32_t y);
biome_id_t terrain_biome_at(const terrain_biome_t *biome, const terrain_climate_sample_t *climate, int32_t x, int32_t y,
                            int32_t z);

#endif
