/* See README.md. */
#include "terrain/biome/index.h"

#include "assert/index.h"

#include <string.h>

enum : uint32_t {
    /* An area is worked through in tiles of this many blocks a side, lined up on the world, as hydrology works, so what a tile
     * holds fits on the stack whatever size the area is. */
    BIOME_TILE_SHIFT = 4u,
    BIOME_TILE = 1u << BIOME_TILE_SHIFT,
    BIOME_TILE_COLUMNS = BIOME_TILE * BIOME_TILE,
    /* The climate is read every 4 blocks, on the density's lattice, and interpolated. */
    BIOME_CELL_SHIFT = 2u,
    BIOME_CELL = 1u << BIOME_CELL_SHIFT,
    BIOME_LATTICE_SIDE = BIOME_TILE / BIOME_CELL + 2u,
    BIOME_LATTICE = BIOME_LATTICE_SIDE * BIOME_LATTICE_SIDE,
};
static_assert((uint32_t)BIOME_CELL == (uint32_t)TERRAIN_DENSITY_CELL_XY);

/* A block of height counts as this much of the climate's range. So 128 blocks is as far as one end of an axis from its
 * middle. Changing it changes every world. */
static const float32_t BIOME_HEIGHT_SCALE = 1.0f32 / 128.0f32;

/* Where each kind of column looks. A lake is land under water, and stays whatever land there would be. */
static const uint8_t BIOME_WHERE_OF_KIND[TERRAIN_HYDROLOGY_KIND_COUNT] = {
    [TERRAIN_HYDROLOGY_KIND_LAND] = BIOME_WHERE_LAND,
    [TERRAIN_HYDROLOGY_KIND_OCEAN] = BIOME_WHERE_OCEAN,
    [TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN] = BIOME_WHERE_DEEP_OCEAN,
    [TERRAIN_HYDROLOGY_KIND_RIVER] = BIOME_WHERE_RIVER,
    [TERRAIN_HYDROLOGY_KIND_LAKE] = BIOME_WHERE_LAND,
    [TERRAIN_HYDROLOGY_KIND_BEACH] = BIOME_WHERE_BEACH,
    [TERRAIN_HYDROLOGY_KIND_STONY_SHORE] = BIOME_WHERE_STONY_SHORE,
};

void terrain_biome_init(terrain_biome_t **out, arena_t *arena, const terrain_fields_t *fields,
                        const terrain_hydrology_t *hydrology) {
    terrain_biome_t *const biome = arena_alloc(arena, sizeof(*biome), alignof(terrain_biome_t));
    biome->borrowed_fields = fields;
    biome->borrowed_hydrology = hydrology;
    *out = biome;
}

void terrain_biome_deinit(terrain_biome_t *biome) {
    memset(biome, 0, sizeof(*biome));
}

/* How far a value is outside a range, squared. */
static float32_t biome_gap(float32_t value, float32_t low, float32_t high) {
    if (value < low) {
        return (low - value) * (low - value);
    }
    if (value > high) {
        return (value - high) * (value - high);
    }
    return 0.0f32;
}

static float32_t biome_climate_distance(const biome_def_t *def, const terrain_biome_inputs_t *inputs) {
    return biome_gap(inputs->temperature, def->temperature.low, def->temperature.high) +
           biome_gap(inputs->humidity, def->humidity.low, def->humidity.high) +
           biome_gap(inputs->continentalness, def->continentalness.low, def->continentalness.high) +
           biome_gap(inputs->erosion, def->erosion.low, def->erosion.high) +
           biome_gap(inputs->weirdness, def->weirdness.low, def->weirdness.high);
}

float32_t terrain_biome_distance(const biome_def_t *def, const terrain_biome_inputs_t *inputs) {
    return biome_climate_distance(def, inputs) + biome_gap(inputs->ground * BIOME_HEIGHT_SCALE,
                                                           def->height.low * BIOME_HEIGHT_SCALE,
                                                           def->height.high * BIOME_HEIGHT_SCALE);
}

biome_id_t terrain_biome_choose(const terrain_biome_inputs_t *inputs) {
    const uint8_t where = BIOME_WHERE_OF_KIND[inputs->kind];
    uint32_t best = BIOME_COUNT;
    float32_t best_distance = 0.0f32;
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        const biome_def_t *const def = &biome_defs[i];
        if (def->where != where) {
            continue;
        }
        const float32_t distance = terrain_biome_distance(def, inputs);
        if (best == BIOME_COUNT || distance < best_distance ||
            (distance == best_distance && def->priority > biome_defs[best].priority)) {
            best = i;
            best_distance = distance;
        }
    }
    ASSERT(best != BIOME_COUNT); /* the tool refuses a where that no biome fills */
    return (biome_id_t)best;
}

uint8_t terrain_biome_caves_of(const terrain_biome_inputs_t *inputs) {
    uint32_t caves = 0u;
    for (uint32_t i = 0u; i < BIOME_UNDERGROUND_COUNT; i++) {
        if (biome_climate_distance(&biome_defs[biome_underground[i]], inputs) == 0.0f32) {
            caves |= 1u << i;
        }
    }
    return (uint8_t)caves;
}

biome_id_t terrain_biome_of_voxel(const terrain_biome_column_t *column, int32_t z) {
    if (column->caves != 0u && (float32_t)z <= column->ground - (float32_t)TERRAIN_BIOME_CAVE_ROOF) {
        for (uint32_t i = 0u; i < BIOME_UNDERGROUND_COUNT; i++) {
            if ((column->caves & (1u << i)) == 0u) {
                continue;
            }
            const biome_range_t range = biome_defs[biome_underground[i]].height;
            if ((float32_t)z >= range.low && (float32_t)z <= range.high) {
                return (biome_id_t)biome_underground[i];
            }
        }
    }
    return (biome_id_t)column->biome;
}

static float32_t biome_bilinear(const float32_t *values, uint32_t at, uint32_t side, float32_t t_x, float32_t t_y) {
    const float32_t south = values[at] + t_x * (values[at + 1u] - values[at]);
    const float32_t north = values[at + side] + t_x * (values[at + side + 1u] - values[at + side]);
    return south + t_y * (north - south);
}

/* One tile's worth, already known to fit in it. */
typedef struct biome_span_t {
    int64_t x_first;  /* 0 */
    int64_t y_first;  /* 8 */
    int64_t step_x;   /* 16 */
    int64_t step_y;   /* 24 */
    uint32_t ix;      /* 32: of its first column in the area */
    uint32_t iy;      /* 36 */
    uint32_t count_x; /* 40 */
    uint32_t count_y; /* 44 */
} biome_span_t;
static_assert(sizeof(biome_span_t) == 48);

static void biome_tile(const terrain_biome_t *biome, const terrain_fields_area_t *area, const biome_span_t *span,
                       terrain_biome_column_t *columns) {
    const int64_t x_last = span->x_first + span->step_x * (int64_t)(span->count_x - 1u);
    const int64_t y_last = span->y_first + span->step_y * (int64_t)(span->count_y - 1u);
    const int64_t cell_x = span->x_first >> BIOME_CELL_SHIFT;
    const int64_t cell_y = span->y_first >> BIOME_CELL_SHIFT;
    const uint32_t side_x = (uint32_t)((x_last >> BIOME_CELL_SHIFT) - cell_x) + 2u;
    const uint32_t side_y = (uint32_t)((y_last >> BIOME_CELL_SHIFT) - cell_y) + 2u;
    ASSERT(side_x <= BIOME_LATTICE_SIDE && side_y <= BIOME_LATTICE_SIDE);

    float32_t temperature[BIOME_LATTICE];
    float32_t humidity[BIOME_LATTICE];
    float32_t continentalness[BIOME_LATTICE];
    float32_t erosion[BIOME_LATTICE];
    float32_t weirdness[BIOME_LATTICE];
    const terrain_fields_area_t lattice = {
        .origin_x = (int32_t)(cell_x * (int64_t)BIOME_CELL),
        .origin_y = (int32_t)(cell_y * (int64_t)BIOME_CELL),
        .step_x = (int32_t)BIOME_CELL,
        .step_y = (int32_t)BIOME_CELL,
        .count_x = side_x,
        .count_y = side_y,
    };
    const terrain_fields_arrays_t arrays = {
        .continentalness = continentalness,
        .erosion = erosion,
        .temperature = temperature,
        .humidity = humidity,
        .weirdness = weirdness,
    };
    terrain_fields_sample_area(biome->borrowed_fields, &lattice, &arrays);

    terrain_hydrology_column_t water[BIOME_TILE_COLUMNS];
    const terrain_fields_area_t tile = {
        .origin_x = (int32_t)span->x_first,
        .origin_y = (int32_t)span->y_first,
        .step_x = (int32_t)span->step_x,
        .step_y = (int32_t)span->step_y,
        .count_x = span->count_x,
        .count_y = span->count_y,
    };
    terrain_hydrology_columns_area(biome->borrowed_hydrology, &tile, water);

    const float32_t per_block = 1.0f32 / (float32_t)BIOME_CELL;
    const int64_t mask = (int64_t)BIOME_CELL - 1;
    for (uint32_t iy = 0u; iy < span->count_y; iy++) {
        const int64_t y = span->y_first + span->step_y * (int64_t)iy;
        const uint32_t ly = (uint32_t)((y >> BIOME_CELL_SHIFT) - cell_y);
        const float32_t t_y = (float32_t)(y & mask) * per_block;
        for (uint32_t ix = 0u; ix < span->count_x; ix++) {
            const int64_t x = span->x_first + span->step_x * (int64_t)ix;
            const uint32_t at = (uint32_t)((x >> BIOME_CELL_SHIFT) - cell_x) + side_x * ly;
            const float32_t t_x = (float32_t)(x & mask) * per_block;
            const terrain_hydrology_column_t *const own = &water[ix + span->count_x * iy];
            const terrain_biome_inputs_t inputs = {
                .temperature = biome_bilinear(temperature, at, side_x, t_x, t_y),
                .humidity = biome_bilinear(humidity, at, side_x, t_x, t_y),
                .continentalness = biome_bilinear(continentalness, at, side_x, t_x, t_y),
                .erosion = biome_bilinear(erosion, at, side_x, t_x, t_y),
                .weirdness = biome_bilinear(weirdness, at, side_x, t_x, t_y),
                .ground = own->ground,
                .kind = own->kind,
            };
            columns[(uint64_t)(span->ix + ix) + (uint64_t)area->count_x * (span->iy + iy)] = (terrain_biome_column_t){
                .ground = own->ground,
                .softness = own->softness,
                .carve_top = own->carve_top,
                .biome = (uint8_t)terrain_biome_choose(&inputs),
                .caves = terrain_biome_caves_of(&inputs),
            };
        }
    }
}

void terrain_biome_columns_area(const terrain_biome_t *biome, const terrain_fields_area_t *area,
                                terrain_biome_column_t *columns) {
    ASSERT(area->step_x > 0 && area->step_y > 0);
    const int64_t tile = (int64_t)BIOME_TILE;
    const int64_t step_x = (int64_t)area->step_x;
    const int64_t step_y = (int64_t)area->step_y;
    for (uint32_t iy_first = 0u; iy_first < area->count_y;) {
        const int64_t y_first = (int64_t)area->origin_y + step_y * (int64_t)iy_first;
        const int64_t y_end = ((y_first >> BIOME_TILE_SHIFT) + 1) * tile;
        uint32_t count_y = 1u;
        while (iy_first + count_y < area->count_y &&
               (int64_t)area->origin_y + step_y * (int64_t)(iy_first + count_y) < y_end) {
            count_y++;
        }
        for (uint32_t ix_first = 0u; ix_first < area->count_x;) {
            const int64_t x_first = (int64_t)area->origin_x + step_x * (int64_t)ix_first;
            const int64_t x_end = ((x_first >> BIOME_TILE_SHIFT) + 1) * tile;
            uint32_t count_x = 1u;
            while (ix_first + count_x < area->count_x &&
                   (int64_t)area->origin_x + step_x * (int64_t)(ix_first + count_x) < x_end) {
                count_x++;
            }
            const biome_span_t span = {
                .x_first = x_first,
                .y_first = y_first,
                .step_x = step_x,
                .step_y = step_y,
                .ix = ix_first,
                .iy = iy_first,
                .count_x = count_x,
                .count_y = count_y,
            };
            biome_tile(biome, area, &span, columns);
            ix_first += count_x;
        }
        iy_first += count_y;
    }
}

terrain_biome_column_t terrain_biome_column_at(const terrain_biome_t *biome, int32_t x, int32_t y) {
    const terrain_fields_area_t area = {
        .origin_x = x, .origin_y = y, .step_x = 1, .step_y = 1, .count_x = 1u, .count_y = 1u};
    terrain_biome_column_t column;
    terrain_biome_columns_area(biome, &area, &column);
    return column;
}

biome_id_t terrain_biome_at(const terrain_biome_t *biome, int32_t x, int32_t y, int32_t z) {
    const terrain_biome_column_t column = terrain_biome_column_at(biome, x, y);
    return terrain_biome_of_voxel(&column, z);
}
