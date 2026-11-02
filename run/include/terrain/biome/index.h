/* See run/src/terrain/biome/README.md. */
#ifndef MC_RUN_TERRAIN_BIOME_INDEX_H
#define MC_RUN_TERRAIN_BIOME_INDEX_H

#include "arena/index.h"
#include "tables/biomes/index.h"
#include "terrain/climate/index.h"
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
    const terrain_climate_t *borrowed_climate;     /* 0 */
    const terrain_hydrology_t *borrowed_hydrology; /* 8 */
} terrain_biome_t;
static_assert(sizeof(terrain_biome_t) == 16);

/* What decides a column's biome. */
typedef struct terrain_biome_inputs_t {
    float32_t temperature;         /* 0 */
    float32_t humidity;            /* 4 */
    float32_t continentalness;     /* 8 */
    float32_t erosion;             /* 12 */
    float32_t weirdness;           /* 16 */
    float32_t ground;              /* 20: terrain_hydrology_column_t's */
    terrain_hydrology_kind_t kind; /* 24 */
    uint8_t _pad0[3];              /* 25 */
} terrain_biome_inputs_t;
static_assert(sizeof(terrain_biome_inputs_t) == 28);

/* What a column came to. */
typedef struct terrain_biome_column_t {
    float32_t ground;    /* 0: terrain_hydrology_column_t's */
    float32_t softness;  /* 4: the same */
    float32_t carve_top; /* 8: the same */
    uint8_t biome;       /* 12: a biome_id_t: the one on the surface */
    uint8_t caves;       /* 13: bit i says biome_underground[i]'s climate holds this column, at any z */
    uint8_t _pad0[2];    /* 14 */
} terrain_biome_column_t;
static_assert(sizeof(terrain_biome_column_t) == 16);

/* Takes the biomes from arena. For use while terrain_init runs. climate and hydrology must outlive it. */
void terrain_biome_init(terrain_biome_t **out, arena_t *arena, const terrain_climate_t *climate,
                        const terrain_hydrology_t *hydrology);

/* Empties the handle. The memory goes back with the arena. */
void terrain_biome_deinit(terrain_biome_t *biome);

/* The README's choice, as plain arithmetic: how far inputs lies outside a row's box. 0 is inside it. */
float32_t terrain_biome_distance(const biome_def_t *def, const terrain_biome_inputs_t *inputs);

/* The README's choice: from what decides a column to its surface biome. */
biome_id_t terrain_biome_choose(const terrain_biome_inputs_t *inputs);

/* Which of biome_underground hold a column, as bits. */
uint8_t terrain_biome_caves_of(const terrain_biome_inputs_t *inputs);

/* The biome of the voxel at z in a column. */
biome_id_t terrain_biome_of_voxel(const terrain_biome_column_t *column, int32_t z);

/* Each column of an area, as the climate lays one out, but the steps must be above 0. columns holds
 * terrain_climate_area_columns(area) values. Allocates nothing, and each value is a pure function of the biomes and the
 * column: splitting an area into parts, in any order, on any thread, gives the same bytes. */
void terrain_biome_columns_area(const terrain_biome_t *biome, const terrain_climate_area_t *area,
                                terrain_biome_column_t *columns);

/* The same at one column, and one voxel. Columns in bulk cost far less each through an area. */
terrain_biome_column_t terrain_biome_column_at(const terrain_biome_t *biome, int32_t x, int32_t y);
biome_id_t terrain_biome_at(const terrain_biome_t *biome, int32_t x, int32_t y, int32_t z);

#endif
