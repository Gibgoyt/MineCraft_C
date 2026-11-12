/* See README.md. */
#include "terrain/biome/index.h"

#include "assert/index.h"
#include "machine/index.h"

#include <math.h>
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

void terrain_biome_init(terrain_biome_t **out, arena_t *arena, uint64_t seed, const terrain_fields_t *fields,
                        const terrain_hydrology_t *hydrology) {
    terrain_biome_t *const biome = arena_alloc(arena, sizeof(*biome), alignof(terrain_biome_t));
    biome->seed = seed;
    biome->borrowed_fields = fields;
    biome->borrowed_hydrology = hydrology;
    *out = biome;
}

void terrain_biome_deinit(terrain_biome_t *biome) {
    memset(biome, 0, sizeof(*biome));
}

/* Plan 004 6.10's classes as the families they name. */
static const uint8_t BIOME_FAMILY_OF_CLASS[TERRAIN_CLIMATE_CLASS_COUNT] = {
    [TERRAIN_CLIMATE_CLASS_NONE] = BIOME_FAMILY_COUNT,
    [TERRAIN_CLIMATE_CLASS_ICE] = BIOME_FAMILY_ICE,
    [TERRAIN_CLIMATE_CLASS_TUNDRA] = BIOME_FAMILY_TUNDRA,
    [TERRAIN_CLIMATE_CLASS_DESERT] = BIOME_FAMILY_DESERT,
    [TERRAIN_CLIMATE_CLASS_STEPPE] = BIOME_FAMILY_STEPPE,
    [TERRAIN_CLIMATE_CLASS_RAINFOREST] = BIOME_FAMILY_RAINFOREST,
    [TERRAIN_CLIMATE_CLASS_MONSOON] = BIOME_FAMILY_MONSOON,
    [TERRAIN_CLIMATE_CLASS_SAVANNA] = BIOME_FAMILY_SAVANNA,
    [TERRAIN_CLIMATE_CLASS_MEDITERRANEAN] = BIOME_FAMILY_MEDITERRANEAN,
    [TERRAIN_CLIMATE_CLASS_TEMPERATE] = BIOME_FAMILY_TEMPERATE,
    [TERRAIN_CLIMATE_CLASS_CONTINENTAL] = BIOME_FAMILY_CONTINENTAL,
    [TERRAIN_CLIMATE_CLASS_BOREAL] = BIOME_FAMILY_BOREAL,
};

/* The rain that is neither wet nor dry for a biome's wetness, and how far from it is all the way. */
static const float32_t BIOME_WETNESS_RAIN_MIDDLE_MM = 1000.0f32;
static const float32_t BIOME_WETNESS_RAIN_SPAN_MM = 1000.0f32;
static const float32_t BIOME_WETNESS_RAIN_WEIGHT = 0.5f32;
/* A biome's climate limits count as axes this long: 10 degrees, and 1000 mm of rain a year. */
static const float32_t BIOME_LIMIT_CC = 1000.0f32;
static const float32_t BIOME_LIMIT_MM = 1000.0f32;

float32_t terrain_biome_height(float32_t ground) {
    return ground; /* plan 007 phase 03: less the plateau's lift */
}

int32_t terrain_biome_climate_z(float32_t ground, terrain_hydrology_kind_t kind) {
    if (kind == TERRAIN_HYDROLOGY_KIND_OCEAN || kind == TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN) {
        return (int32_t)MC_WORLD_Z_SEA;
    }
    return (int32_t)floorf(ground);
}

uint8_t terrain_biome_family_of_class(uint8_t climate_class) {
    return climate_class < TERRAIN_CLIMATE_CLASS_COUNT ? BIOME_FAMILY_OF_CLASS[climate_class] : BIOME_FAMILY_COUNT;
}

biome_family_t terrain_biome_family(const terrain_climate_point_t *point) {
    /* Warmth from the column's own height: a peak is colder than its valley. */
    if (point->t_warm_cc < 0) {
        return BIOME_FAMILY_ICE;
    }
    if (point->t_warm_cc < TERRAIN_CLIMATE_TUNDRA_MAX_CC) {
        return BIOME_FAMILY_TUNDRA;
    }
    /* Dryness from the cell: its rain against plan 004's threshold at the cell's own mean ground, which its class says. A
     * column a few blocks above or below that ground would otherwise cross P_th = 20 T + 140 on its own, and a Desert cell
     * would grow Steppe's grass at its foot and Steppe's dunes on its hills. Water, which has no class, is worked out here. */
    if (point->climate_class == TERRAIN_CLIMATE_CLASS_DESERT || point->climate_class == TERRAIN_CLIMATE_CLASS_STEPPE) {
        return (biome_family_t)BIOME_FAMILY_OF_CLASS[point->climate_class];
    }
    if (point->climate_class == TERRAIN_CLIMATE_CLASS_NONE) {
        const uint8_t klass =
            terrain_climate_classify(point->t_mean_cc, point->t_warm_cc, point->t_cold_cc, point->rain_mm_year,
                                     point->dry_month_mm, (point->flags & TERRAIN_CLIMATE_MONSOON) != 0u);
        if (klass == TERRAIN_CLIMATE_CLASS_DESERT || klass == TERRAIN_CLIMATE_CLASS_STEPPE) {
            return (biome_family_t)BIOME_FAMILY_OF_CLASS[klass];
        }
    }
    /* The rest of 6.10's order, from the column's warmth and the cell's seasons. */
    if (point->t_cold_cc >= TERRAIN_CLIMATE_TROPICAL_MIN_CC) {
        if (point->dry_month_mm >= TERRAIN_CLIMATE_WET_MONTH_MM) {
            return BIOME_FAMILY_RAINFOREST;
        }
        return (point->flags & TERRAIN_CLIMATE_MONSOON) != 0u ? BIOME_FAMILY_MONSOON : BIOME_FAMILY_SAVANNA;
    }
    if (point->t_cold_cc > TERRAIN_CLIMATE_TEMPERATE_MIN_CC) {
        return (point->flags & TERRAIN_CLIMATE_DRY_SUMMER) != 0u ? BIOME_FAMILY_MEDITERRANEAN : BIOME_FAMILY_TEMPERATE;
    }
    return point->t_warm_cc >= TERRAIN_CLIMATE_CONTINENTAL_MAX_CC ? BIOME_FAMILY_CONTINENTAL : BIOME_FAMILY_BOREAL;
}

float32_t terrain_biome_wetness(float32_t field, int32_t rain_mm_year) {
    float32_t rain = ((float32_t)rain_mm_year - BIOME_WETNESS_RAIN_MIDDLE_MM) / BIOME_WETNESS_RAIN_SPAN_MM;
    rain = rain > 1.0f32 ? 1.0f32 : (rain < -1.0f32 ? -1.0f32 : rain);
    const float32_t wetness = field + BIOME_WETNESS_RAIN_WEIGHT * rain;
    return wetness > 1.0f32 ? 1.0f32 : (wetness < -1.0f32 ? -1.0f32 : wetness);
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
    return biome_gap(inputs->variety, def->variety.low, def->variety.high) +
           biome_gap(inputs->wetness, def->wetness.low, def->wetness.high) +
           biome_gap(inputs->continentalness, def->continentalness.low, def->continentalness.high) +
           biome_gap(inputs->erosion, def->erosion.low, def->erosion.high) +
           biome_gap(inputs->weirdness, def->weirdness.low, def->weirdness.high);
}

/* A limit's gap, squared, on an axis of span: integers in, so the same bits wherever. */
static float32_t biome_limit_gap(int32_t value, int32_t low, int32_t high, float32_t span) {
    return biome_gap((float32_t)value / span, (float32_t)low / span, (float32_t)high / span);
}

float32_t terrain_biome_distance(const biome_def_t *def, const terrain_biome_inputs_t *inputs) {
    float32_t distance = biome_climate_distance(def, inputs) +
                         biome_gap(terrain_biome_height(inputs->ground) * BIOME_HEIGHT_SCALE,
                                   def->height.low * BIOME_HEIGHT_SCALE, def->height.high * BIOME_HEIGHT_SCALE);
    if ((def->limits & BIOME_LIMIT_T_WARM) != 0u) {
        distance += biome_limit_gap(inputs->t_warm_cc, def->t_warm_low_cc, def->t_warm_high_cc, BIOME_LIMIT_CC);
    }
    if ((def->limits & BIOME_LIMIT_T_COLD) != 0u) {
        distance += biome_limit_gap(inputs->t_cold_cc, def->t_cold_low_cc, def->t_cold_high_cc, BIOME_LIMIT_CC);
    }
    if ((def->limits & BIOME_LIMIT_RAIN) != 0u) {
        distance += biome_limit_gap(inputs->rain_mm_year, def->rain_low_mm, def->rain_high_mm, BIOME_LIMIT_MM);
    }
    return distance;
}

/* Whether a row is nearer than the best so far: lower distance, then higher priority, then the first by name. */
static bool biome_nearer(uint32_t row, float32_t distance, uint32_t best, float32_t best_distance) {
    return best == BIOME_COUNT || distance < best_distance ||
           (distance == best_distance && biome_defs[row].priority > biome_defs[best].priority);
}

biome_id_t terrain_biome_choose(const terrain_biome_inputs_t *inputs) {
    const uint8_t where = BIOME_WHERE_OF_KIND[inputs->kind];
    ASSERT(inputs->family < BIOME_FAMILY_COUNT);
    const uint32_t family = 1u << inputs->family;
    uint32_t best = BIOME_COUNT;
    float32_t best_distance = 0.0f32;
    for (uint32_t i = 0u; i < BIOME_COUNT; i++) {
        const biome_def_t *const def = &biome_defs[i];
        if (def->where != where || (def->families & family) == 0u) {
            continue;
        }
        const float32_t distance = terrain_biome_distance(def, inputs);
        if (biome_nearer(i, distance, best, best_distance)) {
            best = i;
            best_distance = distance;
        }
    }
    ASSERT(best != BIOME_COUNT); /* the tool refuses a family with no biome for a where */
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

static void biome_tile(const terrain_biome_t *biome, const terrain_climate_sample_t *climate,
                       const terrain_fields_area_t *area, const biome_span_t *span, terrain_biome_column_t *columns) {
    /* With no sample, the fallback's for this tile: a pure function of the seed and the place, like any other. */
    terrain_climate_sample_t fallback;
    if (climate == nullptr) {
        terrain_climate_fallback(biome->seed, span->x_first, span->y_first, &fallback);
        climate = &fallback;
    }

    const int64_t x_last = span->x_first + span->step_x * (int64_t)(span->count_x - 1u);
    const int64_t y_last = span->y_first + span->step_y * (int64_t)(span->count_y - 1u);
    const int64_t cell_x = span->x_first >> BIOME_CELL_SHIFT;
    const int64_t cell_y = span->y_first >> BIOME_CELL_SHIFT;
    const uint32_t side_x = (uint32_t)((x_last >> BIOME_CELL_SHIFT) - cell_x) + 2u;
    const uint32_t side_y = (uint32_t)((y_last >> BIOME_CELL_SHIFT) - cell_y) + 2u;
    ASSERT(side_x <= BIOME_LATTICE_SIDE && side_y <= BIOME_LATTICE_SIDE);

    float32_t variety[BIOME_LATTICE];
    float32_t wetness[BIOME_LATTICE];
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
        .weirdness = weirdness,
        .variety = variety,
        .wetness = wetness,
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
            /* Plan 006 phase 03: the climate first, at the column's own height, then the choice inside its family. */
            const int32_t z = terrain_biome_climate_z(own->ground, own->kind);
            /* The family from the column's own cell at its own height; the rain that biases wetness blended, so it is smooth. */
            const terrain_climate_point_t point = terrain_climate_at(climate, x, y, z);
            const terrain_climate_point_t cell = terrain_climate_nearest_at(climate, x, y, z);
            const terrain_biome_inputs_t inputs = {
                .variety = biome_bilinear(variety, at, side_x, t_x, t_y),
                .wetness = terrain_biome_wetness(biome_bilinear(wetness, at, side_x, t_x, t_y), point.rain_mm_year),
                .continentalness = biome_bilinear(continentalness, at, side_x, t_x, t_y),
                .erosion = biome_bilinear(erosion, at, side_x, t_x, t_y),
                .weirdness = biome_bilinear(weirdness, at, side_x, t_x, t_y),
                .ground = own->ground,
                .kind = own->kind,
                .family = (uint8_t)terrain_biome_family(&cell),
                .t_warm_cc = cell.t_warm_cc,
                .t_cold_cc = cell.t_cold_cc,
                .rain_mm_year = point.rain_mm_year,
            };
            columns[(uint64_t)(span->ix + ix) + (uint64_t)area->count_x * (span->iy + iy)] = (terrain_biome_column_t){
                .ground = own->ground,
                .softness = own->softness,
                .carve_top = own->carve_top,
                .biome = (uint8_t)terrain_biome_choose(&inputs),
                .caves = terrain_biome_caves_of(&inputs),
                .family = inputs.family,
            };
        }
    }
}

void terrain_biome_columns_area(const terrain_biome_t *biome, const terrain_climate_sample_t *climate,
                                const terrain_fields_area_t *area, terrain_biome_column_t *columns) {
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
            biome_tile(biome, climate, area, &span, columns);
            ix_first += count_x;
        }
        iy_first += count_y;
    }
}

terrain_biome_column_t terrain_biome_column_at(const terrain_biome_t *biome, const terrain_climate_sample_t *climate,
                                               int32_t x, int32_t y) {
    const terrain_fields_area_t area = {
        .origin_x = x, .origin_y = y, .step_x = 1, .step_y = 1, .count_x = 1u, .count_y = 1u};
    terrain_biome_column_t column;
    terrain_biome_columns_area(biome, climate, &area, &column);
    return column;
}

biome_id_t terrain_biome_at(const terrain_biome_t *biome, const terrain_climate_sample_t *climate, int32_t x, int32_t y,
                            int32_t z) {
    const terrain_biome_column_t column = terrain_biome_column_at(biome, climate, x, y);
    return terrain_biome_of_voxel(&column, z);
}
