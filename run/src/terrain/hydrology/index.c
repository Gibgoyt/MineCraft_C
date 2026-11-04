/* See README.md. */
#include "terrain/hydrology/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"

#include <string.h>

enum : uint32_t {
    /* A box is worked through in tiles of this many blocks a side, lined up on the world, so what a tile holds fits on the
     * stack whatever size the box is. */
    HYDROLOGY_TILE_SHIFT = 4u,
    HYDROLOGY_TILE = 1u << HYDROLOGY_TILE_SHIFT,
    HYDROLOGY_TILE_COLUMNS = HYDROLOGY_TILE * HYDROLOGY_TILE,
    /* The climate and the ground are read every 4 blocks, on the density's own lattice, and interpolated. */
    HYDROLOGY_CELL_SHIFT = 2u,
    HYDROLOGY_CELL = 1u << HYDROLOGY_CELL_SHIFT,
    /* A tile's lattice, with a block more all round for what the flood asks of the neighbouring columns. */
    HYDROLOGY_LATTICE_SIDE = HYDROLOGY_TILE / HYDROLOGY_CELL + 3u,
    HYDROLOGY_LATTICE = HYDROLOGY_LATTICE_SIDE * HYDROLOGY_LATTICE_SIDE,
    /* The tile, and one column more all round. */
    HYDROLOGY_BANDS = (HYDROLOGY_TILE + 2u) * (HYDROLOGY_TILE + 2u),
    /* How many solid layers down a column has no more to do with its surface. */
    HYDROLOGY_DEPTH_FULL = (uint32_t)TERRAIN_HYDROLOGY_LAYERS_MAX,
    /* Aquifer cells. */
    HYDROLOGY_AQUIFER_SHIFT = 5u,
    HYDROLOGY_AROUND = 27u,
    /* A site is this far into its cell on each axis, and this much further at most. The README shows why it stays inside
     * the 27 cells round any voxel. */
    HYDROLOGY_SITE_MIN = 10u,
    HYDROLOGY_SITE_RANGE = 13u,
    /* The density's lattice is lined up with these. */
    HYDROLOGY_DENSITY_REACH = TERRAIN_DENSITY_CELL_Z + 1u,
};
static_assert((uint32_t)HYDROLOGY_CELL == (uint32_t)TERRAIN_DENSITY_CELL_XY);
static_assert(TERRAIN_HYDROLOGY_AQUIFER_CELL == 1u << HYDROLOGY_AQUIFER_SHIFT);
static_assert(HYDROLOGY_SITE_MIN * 2u + HYDROLOGY_SITE_RANGE - 1u == TERRAIN_HYDROLOGY_AQUIFER_CELL);
static_assert((uint32_t)HYDROLOGY_TILE <=
              (uint32_t)TERRAIN_HYDROLOGY_AQUIFER_CELL); /* so a tile's columns share their aquifer cell */

/* The sizes in the README's formulas. Changing any number here changes every world. */
static const float32_t HYDROLOGY_RIVER_INNER = 0.03f32;
static const float32_t HYDROLOGY_RIVER_INNER_WIDE = 0.04f32;
static const float32_t HYDROLOGY_RIVER_BANK = 0.12f32;
/* How far inland a river runs: whole up to the first, and gone from the second, narrowing and shallowing between. */
static const float32_t HYDROLOGY_RIVER_WHOLE_C = -0.3f32;
static const float32_t HYDROLOGY_RIVER_GONE_C = 0.35f32;
static const float32_t HYDROLOGY_BED_EXTRA =
    (float32_t)(TERRAIN_HYDROLOGY_BED_SHALLOWEST - TERRAIN_HYDROLOGY_BED_DEEPEST);
static const float32_t HYDROLOGY_DEEP_OCEAN_BELOW = -60.0f32;
static const float32_t HYDROLOGY_OCEAN_C_MAX = -0.47f32;
static const float32_t HYDROLOGY_COAST_C_MAX = -0.25f32;
static const float32_t HYDROLOGY_SHORE_UNDER = -6.0f32;
static const float32_t HYDROLOGY_SAND_TOP_MIN = 2.0f32;
static const float32_t HYDROLOGY_STEEP_FROM = 1.5f32;
static const float32_t HYDROLOGY_RIVER_CUT_SHOWS = 0.05f32;

/* Materials: how thick, and from where. */
enum : uint32_t {
    HYDROLOGY_SAND_DEPTH = 4u,
    HYDROLOGY_FLOOR_DEPTH = 3u,
    HYDROLOGY_THIN_DEPTH = 2u,
};
static_assert(HYDROLOGY_SAND_DEPTH + 2u == HYDROLOGY_DEPTH_FULL);
static const int32_t HYDROLOGY_GRAVEL_BELOW = -50;
static const int32_t HYDROLOGY_CLAY_ABOVE = -30;
static const int32_t HYDROLOGY_BANK_TOP = 2;
static const float32_t HYDROLOGY_CLAY_FROM = 0.5f32;
static const float32_t HYDROLOGY_RIVER_GRAVEL_BELOW = -0.2f32;

/* Aquifers. */
static const float32_t HYDROLOGY_FLOOD_WET_FROM = 0.3f32;
static const float32_t HYDROLOGY_LAVA_FROM = 0.4f32;
static const int32_t HYDROLOGY_LEVEL_BAND = 40;
static const int32_t HYDROLOGY_LEVEL_MIDDLE = 20;
static const int32_t HYDROLOGY_LEVEL_STEP = 3;
static const float32_t HYDROLOGY_LEVEL_STEPS = 4.0f32;
static const int64_t HYDROLOGY_BARRIER_BLOCKS = 2;
static const int32_t HYDROLOGY_DRY = INT32_MIN;

/* What each field looks like. */
static const terrain_noise_shape_t HYDROLOGY_SHAPE_WIDTH = {
    .frequency_x = 1.0f64 / 512.0f64,
    .frequency_y = 1.0f64 / 512.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t HYDROLOGY_SHAPE_SHORE = {
    .frequency_x = 1.0f64 / 48.0f64,
    .frequency_y = 1.0f64 / 48.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t HYDROLOGY_SHAPE_FLOOD = {
    .frequency_x = 1.0f64 / 128.0f64,
    .frequency_y = 1.0f64 / 128.0f64,
    .frequency_z = 1.0f64 / 128.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t HYDROLOGY_SHAPE_LEVEL = {
    .frequency_x = 1.0f64 / 96.0f64,
    .frequency_y = 1.0f64 / 96.0f64,
    .frequency_z = 1.0f64 / 96.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t HYDROLOGY_SHAPE_LAVA = {
    .frequency_x = 1.0f64 / 96.0f64,
    .frequency_y = 1.0f64 / 96.0f64,
    .frequency_z = 1.0f64 / 96.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};

/* A raw value at this far from 0 becomes -1 or 1: about twice what each measures as its standard deviation. */
static const float64_t HYDROLOGY_LIMIT_WIDTH = 0.42f64;
static const float64_t HYDROLOGY_LIMIT_SHORE = 0.42f64;
static const float64_t HYDROLOGY_LIMIT_FLOOD = 0.42f64;
static const float64_t HYDROLOGY_LIMIT_LEVEL = 0.42f64;
static const float64_t HYDROLOGY_LIMIT_LAVA = 0.54f64;

/* ---- Init --------------------------------------------------------------------------------------------------------- */

bool terrain_hydrology_init(terrain_hydrology_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                            const terrain_density_t *density, const terrain_climate_t *climate) {
    *out = nullptr;
    terrain_hydrology_t *const hydrology = arena_alloc(arena, sizeof(*hydrology), alignof(terrain_hydrology_t));
    memset(hydrology, 0, sizeof(*hydrology));
    hydrology->borrowed_density = density;
    hydrology->borrowed_climate = climate;
    hydrology->aquifer_seed = terrain_rng_derive(bank->seed, terrain_rng_salt("hydrology/aquifer"));
    if (!terrain_noise_bank_add(bank, "hydrology/width", &HYDROLOGY_SHAPE_WIDTH, &hydrology->borrowed_width) ||
        !terrain_noise_bank_add(bank, "hydrology/shore", &HYDROLOGY_SHAPE_SHORE, &hydrology->borrowed_shore) ||
        !terrain_noise_bank_add(bank, "hydrology/flood", &HYDROLOGY_SHAPE_FLOOD, &hydrology->borrowed_flood) ||
        !terrain_noise_bank_add(bank, "hydrology/level", &HYDROLOGY_SHAPE_LEVEL, &hydrology->borrowed_level) ||
        !terrain_noise_bank_add(bank, "hydrology/lava", &HYDROLOGY_SHAPE_LAVA, &hydrology->borrowed_lava)) {
        return false;
    }
    *out = hydrology;
    return true;
}

void terrain_hydrology_deinit(terrain_hydrology_t *hydrology) {
    memset(hydrology, 0, sizeof(*hydrology));
}

/* ---- A column ----------------------------------------------------------------------------------------------------- */

/* As the climate spreads its fields, and for the same reason. */
static float32_t hydrology_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

static terrain_hydrology_kind_t hydrology_kind_of(const terrain_hydrology_inputs_t *inputs, float32_t ground,
                                                  float32_t river) {
    const float32_t sea = (float32_t)MC_WORLD_Z_SEA;
    const float32_t sand_top = HYDROLOGY_SAND_TOP_MIN + 1.5f32 * (inputs->shore + 1.0f32);
    if (inputs->height >= sea && ground <= sea) {
        return TERRAIN_HYDROLOGY_KIND_RIVER;
    }
    if (river < 0.5f32 && inputs->continentalness <= HYDROLOGY_COAST_C_MAX && ground >= HYDROLOGY_SHORE_UNDER &&
        ground <= sand_top + 1.0f32) {
        return inputs->steepness >= HYDROLOGY_STEEP_FROM ? TERRAIN_HYDROLOGY_KIND_STONY_SHORE
                                                         : TERRAIN_HYDROLOGY_KIND_BEACH;
    }
    if (ground >= sea) {
        return TERRAIN_HYDROLOGY_KIND_LAND;
    }
    if (inputs->continentalness > HYDROLOGY_OCEAN_C_MAX) {
        return TERRAIN_HYDROLOGY_KIND_LAKE;
    }
    return ground < HYDROLOGY_DEEP_OCEAN_BELOW ? TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN : TERRAIN_HYDROLOGY_KIND_OCEAN;
}

/* 1 near the coast and 0 far inland, smoothly: how much of a river there is at this continentalness. */
static float32_t hydrology_river_reach(float32_t continentalness) {
    if (continentalness <= HYDROLOGY_RIVER_WHOLE_C) {
        return 1.0f32;
    }
    if (continentalness >= HYDROLOGY_RIVER_GONE_C) {
        return 0.0f32;
    }
    const float32_t t = (HYDROLOGY_RIVER_GONE_C - continentalness) / (HYDROLOGY_RIVER_GONE_C - HYDROLOGY_RIVER_WHOLE_C);
    return t * t * (3.0f32 - 2.0f32 * t);
}

terrain_hydrology_column_t terrain_hydrology_column_from(const terrain_hydrology_inputs_t *inputs) {
    const float32_t wide = (inputs->width + 1.0f32) * 0.5f32;
    const float32_t near = inputs->weirdness < 0.0f32 ? -inputs->weirdness : inputs->weirdness;
    const float32_t reach = hydrology_river_reach(inputs->continentalness);
    const float32_t bank = HYDROLOGY_RIVER_BANK * reach;
    const float32_t inner = (HYDROLOGY_RIVER_INNER + HYDROLOGY_RIVER_INNER_WIDE * wide) * reach;
    const float32_t outer = inner + bank;

    float32_t river = 0.0f32;
    if (near <= inner) {
        river = reach;
    } else if (near < outer) {
        const float32_t t = (outer - near) / bank;
        river = reach * t * t * (3.0f32 - 2.0f32 * t);
    }

    /* From above any ground this column could have, at no river, down to the bed in the channel's middle. */
    const float32_t top = inputs->height + inputs->softness + (float32_t)HYDROLOGY_DENSITY_REACH;
    const float32_t bed = (float32_t)TERRAIN_HYDROLOGY_BED_SHALLOWEST - HYDROLOGY_BED_EXTRA * wide;
    const float32_t rest = 1.0f32 - river;
    const float32_t carve_top = river > 0.0f32 ? bed + (top - bed) * rest * rest : top;
    const float32_t ground = carve_top < inputs->height ? carve_top : inputs->height;

    return (terrain_hydrology_column_t){
        .height = inputs->height,
        .softness = inputs->softness,
        .ground = ground,
        .carve_top = carve_top,
        .river = river,
        .shore = inputs->shore,
        .kind = hydrology_kind_of(inputs, ground, river),
    };
}

/* ---- The lattice -------------------------------------------------------------------------------------------------- */

/* What the density's lattice holds for a tile. */
typedef struct hydrology_lattice_t {
    int64_t cell_x;                               /* 0: the lattice column at index 0, in cells */
    int64_t cell_y;                               /* 8 */
    uint32_t side_x;                              /* 16 */
    uint32_t side_y;                              /* 20 */
    float32_t heights[HYDROLOGY_LATTICE];         /* 24 */
    float32_t softness[HYDROLOGY_LATTICE];        /* 220 */
    float32_t continentalness[HYDROLOGY_LATTICE]; /* 416 */
    float32_t weirdness[HYDROLOGY_LATTICE];       /* 612 */
    float32_t width[HYDROLOGY_LATTICE];           /* 808 */
    float32_t shore[HYDROLOGY_LATTICE];           /* 1004 */
    float32_t ground[HYDROLOGY_LATTICE];          /* 1200: for how steep a cell is */
    uint8_t _pad0[4];                             /* 1396 */
} hydrology_lattice_t;
static_assert(sizeof(hydrology_lattice_t) == 1400);

/* Columns from one tile, both ends included. */
typedef struct hydrology_span_t {
    int64_t x_first; /* 0 */
    int64_t x_last;  /* 8 */
    int64_t y_first; /* 16 */
    int64_t y_last;  /* 24 */
} hydrology_span_t;
static_assert(sizeof(hydrology_span_t) == 32);

/* The lattice columns round a span, and ring blocks more on every side. */
static void hydrology_lattice_fill(const terrain_hydrology_t *hydrology, const hydrology_span_t *span, int64_t ring,
                                   hydrology_lattice_t *lattice) {
    lattice->cell_x = (span->x_first - ring) >> HYDROLOGY_CELL_SHIFT;
    lattice->cell_y = (span->y_first - ring) >> HYDROLOGY_CELL_SHIFT;
    lattice->side_x = (uint32_t)(((span->x_last + ring) >> HYDROLOGY_CELL_SHIFT) - lattice->cell_x) + 2u;
    lattice->side_y = (uint32_t)(((span->y_last + ring) >> HYDROLOGY_CELL_SHIFT) - lattice->cell_y) + 2u;
    ASSERT(lattice->side_x * lattice->side_y <= HYDROLOGY_LATTICE);

    const terrain_climate_area_t area = {
        .origin_x = (int32_t)(lattice->cell_x * (int64_t)HYDROLOGY_CELL),
        .origin_y = (int32_t)(lattice->cell_y * (int64_t)HYDROLOGY_CELL),
        .step_x = (int32_t)HYDROLOGY_CELL,
        .step_y = (int32_t)HYDROLOGY_CELL,
        .count_x = lattice->side_x,
        .count_y = lattice->side_y,
    };
    const terrain_climate_arrays_t climate = {
        .continentalness = lattice->continentalness,
        .weirdness = lattice->weirdness,
    };
    terrain_density_profile_area(hydrology->borrowed_density, &area, lattice->heights, lattice->softness);
    terrain_climate_sample_area(hydrology->borrowed_climate, &area, &climate);

    float64_t xs[HYDROLOGY_LATTICE];
    float64_t ys[HYDROLOGY_LATTICE];
    const float64_t zs[HYDROLOGY_LATTICE] = {0.0f64}; /* neither 2D field has a frequency on z */
    const uint32_t count = lattice->side_x * lattice->side_y;
    for (uint32_t iy = 0u; iy < lattice->side_y; iy++) {
        for (uint32_t ix = 0u; ix < lattice->side_x; ix++) {
            const uint32_t i = ix + lattice->side_x * iy;
            xs[i] = (float64_t)((lattice->cell_x + (int64_t)ix) * (int64_t)HYDROLOGY_CELL);
            ys[i] = (float64_t)((lattice->cell_y + (int64_t)iy) * (int64_t)HYDROLOGY_CELL);
        }
    }
    terrain_noise_sample_points(hydrology->borrowed_width, xs, ys, zs, count, lattice->width);
    terrain_noise_sample_points(hydrology->borrowed_shore, xs, ys, zs, count, lattice->shore);
    for (uint32_t i = 0u; i < count; i++) {
        lattice->width[i] = hydrology_spread(lattice->width[i], HYDROLOGY_LIMIT_WIDTH);
        lattice->shore[i] = hydrology_spread(lattice->shore[i], HYDROLOGY_LIMIT_SHORE);
        const terrain_hydrology_inputs_t inputs = {
            .height = lattice->heights[i],
            .softness = lattice->softness[i],
            .continentalness = lattice->continentalness[i],
            .weirdness = lattice->weirdness[i],
            .width = lattice->width[i],
            .shore = lattice->shore[i],
        };
        const terrain_hydrology_column_t column = terrain_hydrology_column_from(&inputs);
        lattice->ground[i] = column.ground;
    }
}

static float32_t hydrology_bilinear(const float32_t *values, uint32_t at, uint32_t side, float32_t t_x, float32_t t_y) {
    const float32_t south = values[at] + t_x * (values[at + 1u] - values[at]);
    const float32_t north = values[at + side] + t_x * (values[at + side + 1u] - values[at + side]);
    return south + t_y * (north - south);
}

static float32_t hydrology_size(float32_t value) {
    return value < 0.0f32 ? -value : value;
}

/* A column inside what the lattice was filled for. Always the same four corners and the same order, so the bits cannot depend
 * on the box or the tile that asked. */
static terrain_hydrology_column_t hydrology_column_in(const hydrology_lattice_t *lattice, int64_t x, int64_t y) {
    const float32_t per_block = 1.0f32 / (float32_t)HYDROLOGY_CELL;
    const uint32_t ix = (uint32_t)((x >> HYDROLOGY_CELL_SHIFT) - lattice->cell_x);
    const uint32_t iy = (uint32_t)((y >> HYDROLOGY_CELL_SHIFT) - lattice->cell_y);
    ASSERT(ix + 1u < lattice->side_x && iy + 1u < lattice->side_y);
    const uint32_t side = lattice->side_x;
    const uint32_t at = ix + side * iy;
    const float32_t t_x = (float32_t)(x & (int64_t)(HYDROLOGY_CELL - 1u)) * per_block;
    const float32_t t_y = (float32_t)(y & (int64_t)(HYDROLOGY_CELL - 1u)) * per_block;

    const float32_t *const ground = lattice->ground;
    float32_t steepest = hydrology_size(ground[at + 1u] - ground[at]);
    const float32_t north = hydrology_size(ground[at + side + 1u] - ground[at + side]);
    const float32_t west = hydrology_size(ground[at + side] - ground[at]);
    const float32_t east = hydrology_size(ground[at + side + 1u] - ground[at + 1u]);
    steepest = north > steepest ? north : steepest;
    steepest = west > steepest ? west : steepest;
    steepest = east > steepest ? east : steepest;

    const terrain_hydrology_inputs_t inputs = {
        .height = hydrology_bilinear(lattice->heights, at, side, t_x, t_y),
        .softness = hydrology_bilinear(lattice->softness, at, side, t_x, t_y),
        .continentalness = hydrology_bilinear(lattice->continentalness, at, side, t_x, t_y),
        .weirdness = hydrology_bilinear(lattice->weirdness, at, side, t_x, t_y),
        .width = hydrology_bilinear(lattice->width, at, side, t_x, t_y),
        .shore = hydrology_bilinear(lattice->shore, at, side, t_x, t_y),
        .steepness = steepest * per_block,
    };
    return terrain_hydrology_column_from(&inputs);
}

void terrain_hydrology_columns_area(const terrain_hydrology_t *hydrology, const terrain_climate_area_t *area,
                                    terrain_hydrology_column_t *columns) {
    ASSERT(area->step_x > 0 && area->step_y > 0);
    const int64_t tile = (int64_t)HYDROLOGY_TILE;
    const int64_t step_x = (int64_t)area->step_x;
    const int64_t step_y = (int64_t)area->step_y;
    hydrology_lattice_t lattice;
    for (uint32_t iy_first = 0u; iy_first < area->count_y;) {
        const int64_t y_first = (int64_t)area->origin_y + step_y * (int64_t)iy_first;
        const int64_t y_end = ((y_first >> HYDROLOGY_TILE_SHIFT) + 1) * tile;
        uint32_t iy_last = iy_first;
        while (iy_last + 1u < area->count_y && (int64_t)area->origin_y + step_y * (int64_t)(iy_last + 1u) < y_end) {
            iy_last++;
        }
        for (uint32_t ix_first = 0u; ix_first < area->count_x;) {
            const int64_t x_first = (int64_t)area->origin_x + step_x * (int64_t)ix_first;
            const int64_t x_end = ((x_first >> HYDROLOGY_TILE_SHIFT) + 1) * tile;
            uint32_t ix_last = ix_first;
            while (ix_last + 1u < area->count_x && (int64_t)area->origin_x + step_x * (int64_t)(ix_last + 1u) < x_end) {
                ix_last++;
            }
            const hydrology_span_t span = {
                .x_first = x_first,
                .x_last = (int64_t)area->origin_x + step_x * (int64_t)ix_last,
                .y_first = y_first,
                .y_last = (int64_t)area->origin_y + step_y * (int64_t)iy_last,
            };
            hydrology_lattice_fill(hydrology, &span, 0, &lattice);
            for (uint32_t iy = iy_first; iy <= iy_last; iy++) {
                const int64_t y = (int64_t)area->origin_y + step_y * (int64_t)iy;
                for (uint32_t ix = ix_first; ix <= ix_last; ix++) {
                    const int64_t x = (int64_t)area->origin_x + step_x * (int64_t)ix;
                    columns[(uint64_t)ix + (uint64_t)area->count_x * iy] = hydrology_column_in(&lattice, x, y);
                }
            }
            ix_first = ix_last + 1u;
        }
        iy_first = iy_last + 1u;
    }
}

terrain_hydrology_column_t terrain_hydrology_column_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y) {
    const terrain_climate_area_t area = {
        .origin_x = x, .origin_y = y, .step_x = 1, .step_y = 1, .count_x = 1u, .count_y = 1u};
    terrain_hydrology_column_t column;
    terrain_hydrology_columns_area(hydrology, &area, &column);
    return column;
}

/* ---- The surface -------------------------------------------------------------------------------------------------- */

/* What a stone voxel becomes that has depth solid voxels between it and what is open above it. wet says that is water. */
static uint16_t hydrology_material(const terrain_hydrology_column_t *column, int32_t z, uint32_t depth, bool wet) {
    switch (column->kind) {
    case TERRAIN_HYDROLOGY_KIND_OCEAN:
    case TERRAIN_HYDROLOGY_KIND_DEEP_OCEAN:
        if (!wet) {
            return BLOCK_ID_STONE;
        }
        if (z < HYDROLOGY_GRAVEL_BELOW) {
            return depth < HYDROLOGY_THIN_DEPTH ? BLOCK_ID_GRAVEL : BLOCK_ID_STONE;
        }
        if (z >= HYDROLOGY_CLAY_ABOVE && column->shore >= HYDROLOGY_CLAY_FROM && depth < HYDROLOGY_THIN_DEPTH) {
            return BLOCK_ID_CLAY;
        }
        return depth < HYDROLOGY_FLOOR_DEPTH ? BLOCK_ID_SAND : BLOCK_ID_STONE;
    case TERRAIN_HYDROLOGY_KIND_RIVER:
    case TERRAIN_HYDROLOGY_KIND_LAKE:
        if (!wet) {
            return BLOCK_ID_STONE;
        }
        if (column->shore >= HYDROLOGY_CLAY_FROM && depth < HYDROLOGY_THIN_DEPTH) {
            return BLOCK_ID_CLAY;
        }
        if (depth >= HYDROLOGY_FLOOR_DEPTH) {
            return BLOCK_ID_STONE;
        }
        return column->shore < HYDROLOGY_RIVER_GRAVEL_BELOW ? BLOCK_ID_GRAVEL : BLOCK_ID_SAND;
    case TERRAIN_HYDROLOGY_KIND_BEACH: {
        const float32_t sand_top = HYDROLOGY_SAND_TOP_MIN + 1.5f32 * (column->shore + 1.0f32);
        if ((float32_t)z > sand_top) {
            return BLOCK_ID_STONE;
        }
        if (depth < HYDROLOGY_SAND_DEPTH) {
            return BLOCK_ID_SAND;
        }
        return depth < HYDROLOGY_DEPTH_FULL ? BLOCK_ID_SANDSTONE : BLOCK_ID_STONE;
    }
    case TERRAIN_HYDROLOGY_KIND_STONY_SHORE:
        return wet && depth < HYDROLOGY_THIN_DEPTH ? BLOCK_ID_GRAVEL : BLOCK_ID_STONE;
    case TERRAIN_HYDROLOGY_KIND_LAND:
    case TERRAIN_HYDROLOGY_KIND_COUNT:
        break;
    }
    /* Land. Only a river's banks, where the cut has left them low enough to be washed. */
    if (column->river >= HYDROLOGY_RIVER_CUT_SHOWS && z <= HYDROLOGY_BANK_TOP && depth < HYDROLOGY_THIN_DEPTH) {
        return column->shore < HYDROLOGY_RIVER_GRAVEL_BELOW ? BLOCK_ID_GRAVEL : BLOCK_ID_SAND;
    }
    return BLOCK_ID_STONE;
}

/* ---- The fill ----------------------------------------------------------------------------------------------------- */

/* Which layers of a tile can have anything to do. */
typedef struct hydrology_reach_t {
    int64_t open_from; /* 0: from here up, nothing is solid */
    int64_t solid_to;  /* 8: from here down, everything is solid, up to the bedrock */
} hydrology_reach_t;
static_assert(sizeof(hydrology_reach_t) == 16);

static int64_t hydrology_floor(float32_t value) {
    const int64_t whole = (int64_t)value;
    return (float32_t)whole > value ? whole - 1 : whole;
}

/* What the density's sums say of the tile's lattice, as its README shows them, and what the cut takes off. */
static hydrology_reach_t hydrology_reach_of(const hydrology_lattice_t *lattice,
                                            const terrain_hydrology_column_t *columns, uint32_t count) {
    float32_t highest = lattice->heights[0] + lattice->softness[0];
    float32_t lowest = lattice->heights[0] - lattice->softness[0];
    const uint32_t corners = lattice->side_x * lattice->side_y;
    for (uint32_t i = 1u; i < corners; i++) {
        const float32_t high = lattice->heights[i] + lattice->softness[i];
        const float32_t low = lattice->heights[i] - lattice->softness[i];
        highest = high > highest ? high : highest;
        lowest = low < lowest ? low : lowest;
    }
    int64_t solid_to = hydrology_floor(lowest) - (int64_t)HYDROLOGY_DENSITY_REACH;
    for (uint32_t i = 0u; i < count; i++) {
        const int64_t cut = hydrology_floor(columns[i].carve_top);
        solid_to = cut < solid_to ? cut : solid_to;
    }
    return (hydrology_reach_t){
        .open_from = hydrology_floor(highest) + (int64_t)HYDROLOGY_DENSITY_REACH + 1,
        .solid_to = solid_to,
    };
}

/* Where a tile's layers are in an array of blocks laid out like a box. */
typedef struct hydrology_layers_t {
    uint16_t *blocks; /* 0 */
    int64_t origin_x; /* 8 */
    int64_t origin_y; /* 16 */
    int64_t origin_z; /* 24 */
    uint32_t count_x; /* 32 */
    uint32_t count_y; /* 36 */
} hydrology_layers_t;
static_assert(sizeof(hydrology_layers_t) == 40);

/* What the walk down a tile has seen of each column so far. */
typedef struct hydrology_walk_t {
    uint8_t
        depth[HYDROLOGY_TILE_COLUMNS];   /* 0: solid voxels since the last open one, stopping at HYDROLOGY_DEPTH_FULL */
    uint8_t wet[HYDROLOGY_TILE_COLUMNS]; /* 256: that open voxel was water */
} hydrology_walk_t;
static_assert(sizeof(hydrology_walk_t) == 512);

/* Down through the layers from z_top to z_bottom, both included. It cuts what the river takes, and writes materials only if
 * materials is true: the layers above a box are only read from, to know what lies over its top. */
static void hydrology_walk_down(const hydrology_layers_t *layers, const hydrology_span_t *span,
                                const terrain_hydrology_column_t *columns, int64_t z_top, int64_t z_bottom,
                                bool materials, hydrology_walk_t *walk) {
    const uint32_t width = (uint32_t)(span->x_last - span->x_first) + 1u;
    const uint32_t height = (uint32_t)(span->y_last - span->y_first) + 1u;
    const uint64_t row_stride = layers->count_x;
    const uint64_t layer_stride = row_stride * layers->count_y;
    const uint64_t corner =
        (uint64_t)(span->x_first - layers->origin_x) + row_stride * (uint64_t)(span->y_first - layers->origin_y);
    for (int64_t z = z_top; z >= z_bottom; z--) {
        uint16_t *const layer = layers->blocks + (uint64_t)(z - layers->origin_z) * layer_stride + corner;
        const float32_t z_float = (float32_t)z;
        for (uint32_t iy = 0u; iy < height; iy++) {
            uint16_t *const row = layer + row_stride * iy;
            for (uint32_t ix = 0u; ix < width; ix++) {
                const uint32_t c = ix + width * iy;
                uint16_t block = row[ix];
                if (block == BLOCK_ID_STONE && z_float > columns[c].carve_top) {
                    block = z <= (int64_t)MC_WORLD_Z_SEA ? BLOCK_ID_WATER : BLOCK_ID_AIR;
                    row[ix] = block;
                }
                if (block == BLOCK_ID_AIR || block == BLOCK_ID_WATER) {
                    walk->depth[c] = 0u;
                    walk->wet[c] = block == BLOCK_ID_WATER ? 1u : 0u;
                    continue;
                }
                const uint32_t depth = walk->depth[c];
                if (depth >= HYDROLOGY_DEPTH_FULL) {
                    continue;
                }
                if (materials && block == BLOCK_ID_STONE) {
                    row[ix] = hydrology_material(&columns[c], (int32_t)z, depth, walk->wet[c] != 0u);
                }
                walk->depth[c] = (uint8_t)(depth + 1u);
            }
        }
    }
}

/* The layers of one tile of a box already holding the density's blocks. */
static void hydrology_fill_tile(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                const hydrology_span_t *span, uint16_t *out) {
    hydrology_lattice_t lattice;
    terrain_hydrology_column_t columns[HYDROLOGY_TILE_COLUMNS];
    hydrology_walk_t walk;
    hydrology_lattice_fill(hydrology, span, 0, &lattice);

    const uint32_t width = (uint32_t)(span->x_last - span->x_first) + 1u;
    const uint32_t height = (uint32_t)(span->y_last - span->y_first) + 1u;
    for (uint32_t iy = 0u; iy < height; iy++) {
        for (uint32_t ix = 0u; ix < width; ix++) {
            columns[ix + width * iy] =
                hydrology_column_in(&lattice, span->x_first + (int64_t)ix, span->y_first + (int64_t)iy);
        }
    }
    const hydrology_reach_t reach = hydrology_reach_of(&lattice, columns, width * height);

    const int64_t box_top = (int64_t)box->origin_z + (int64_t)box->count_z - 1;
    const int64_t full = (int64_t)HYDROLOGY_DEPTH_FULL;
    const hydrology_layers_t layers = {
        .blocks = out,
        .origin_x = box->origin_x,
        .origin_y = box->origin_y,
        .origin_z = box->origin_z,
        .count_x = box->count_x,
        .count_y = box->count_y,
    };

    /* What lies over the top of what is walked. All open, all deep inside the ground, or else it has to be looked at: that
     * is what makes a voxel come out the same whichever box it was in. */
    int64_t walk_top = box_top;
    if (box_top + 1 >= reach.open_from) {
        walk_top = reach.open_from - 1;
        memset(walk.depth, 0, sizeof(walk.depth));
        memset(walk.wet, walk_top + 1 <= (int64_t)MC_WORLD_Z_SEA ? 1 : 0, sizeof(walk.wet));
    } else if (box_top + full <= reach.solid_to) {
        memset(walk.depth, (int32_t)HYDROLOGY_DEPTH_FULL, sizeof(walk.depth));
        memset(walk.wet, 0, sizeof(walk.wet));
    } else {
        uint16_t above[HYDROLOGY_TILE_COLUMNS * HYDROLOGY_DEPTH_FULL];
        const terrain_density_box_t above_box = {
            .origin_x = (int32_t)span->x_first,
            .origin_y = (int32_t)span->y_first,
            .origin_z = (int32_t)(box_top + 1),
            .count_x = width,
            .count_y = height,
            .count_z = HYDROLOGY_DEPTH_FULL,
        };
        const hydrology_layers_t above_layers = {
            .blocks = above,
            .origin_x = span->x_first,
            .origin_y = span->y_first,
            .origin_z = box_top + 1,
            .count_x = width,
            .count_y = height,
        };
        terrain_density_fill_box(hydrology->borrowed_density, &above_box, above);
        /* Whatever is above the pile is taken as more of the same: a surface further up than its layers are thick has no
         * say in what is below. So the pile's top is counted as full, if it is solid. */
        memset(walk.depth, (int32_t)HYDROLOGY_DEPTH_FULL, sizeof(walk.depth));
        memset(walk.wet, 0, sizeof(walk.wet));
        hydrology_walk_down(&above_layers, span, columns, box_top + full, box_top + 1, false, &walk);
    }
    const int64_t bottom_reach = reach.solid_to - full;
    const int64_t walk_bottom = bottom_reach > (int64_t)box->origin_z ? bottom_reach : (int64_t)box->origin_z;
    hydrology_walk_down(&layers, span, columns, walk_top, walk_bottom, true, &walk);
}

/* A box, tile by tile. */
typedef void hydrology_tile_function_t(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                       const hydrology_span_t *span, const uint8_t *opened, uint16_t *blocks);

static void hydrology_box_tiles(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                const uint8_t *opened, uint16_t *blocks, hydrology_tile_function_t *function) {
    const int64_t x_end = (int64_t)box->origin_x + (int64_t)box->count_x;
    const int64_t y_end = (int64_t)box->origin_y + (int64_t)box->count_y;
    const int64_t tile = (int64_t)HYDROLOGY_TILE;
    for (int64_t y = box->origin_y; y < y_end;) {
        const int64_t y_next = ((y >> HYDROLOGY_TILE_SHIFT) + 1) * tile;
        const int64_t y_last = (y_next < y_end ? y_next : y_end) - 1;
        for (int64_t x = box->origin_x; x < x_end;) {
            const int64_t x_next = ((x >> HYDROLOGY_TILE_SHIFT) + 1) * tile;
            const int64_t x_last = (x_next < x_end ? x_next : x_end) - 1;
            const hydrology_span_t span = {.x_first = x, .x_last = x_last, .y_first = y, .y_last = y_last};
            function(hydrology, box, &span, opened, blocks);
            x = x_next;
        }
        y = y_next;
    }
}

static void hydrology_fill_tile_function(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                         const hydrology_span_t *span, const uint8_t *opened, uint16_t *blocks) {
    (void)opened;
    hydrology_fill_tile(hydrology, box, span, blocks);
}

void terrain_hydrology_fill_box(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box, uint16_t *out) {
    if (box->count_x == 0u || box->count_y == 0u || box->count_z == 0u) {
        return;
    }
    terrain_density_fill_box(hydrology->borrowed_density, box, out);
    hydrology_box_tiles(hydrology, box, nullptr, out, hydrology_fill_tile_function);
}

uint16_t terrain_hydrology_block_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = {
        .origin_x = x, .origin_y = y, .origin_z = z, .count_x = 1u, .count_y = 1u, .count_z = 1u};
    uint16_t block = BLOCK_ID_AIR;
    terrain_hydrology_fill_box(hydrology, &box, &block);
    return block;
}

/* ---- Aquifers ----------------------------------------------------------------------------------------------------- */

/* One cell: its site, and what it holds. */
typedef struct hydrology_cell_t {
    int32_t level;  /* 0: the top block holding fluid, or HYDROLOGY_DRY */
    int16_t site_x; /* 4: from the corner of the cell the voxel is in */
    int16_t site_y; /* 6 */
    int16_t site_z; /* 8 */
    uint16_t fluid; /* 10 */
} hydrology_cell_t;
static_assert(sizeof(hydrology_cell_t) == 12);

/* The 27 cells round a voxel's cell. */
typedef struct hydrology_around_t {
    int64_t cell_x;                           /* 0 */
    int64_t cell_y;                           /* 8 */
    int64_t cell_z;                           /* 16 */
    hydrology_cell_t cells[HYDROLOGY_AROUND]; /* 24 */
    int32_t top_level;                        /* 348: above this, none of the 27 hold anything */
    uint8_t filled;                           /* 352 */
    uint8_t _pad0[7];                         /* 353 */
} hydrology_around_t;
static_assert(sizeof(hydrology_around_t) == 360);

static int64_t hydrology_floor_div(int64_t value, int64_t by) {
    const int64_t whole = value / by;
    return (value % by != 0 && (value < 0) != (by < 0)) ? whole - 1 : whole;
}

static void hydrology_around_fill(const terrain_hydrology_t *hydrology, int64_t cell_x, int64_t cell_y, int64_t cell_z,
                                  hydrology_around_t *around) {
    float64_t xs[HYDROLOGY_AROUND];
    float64_t ys[HYDROLOGY_AROUND];
    float64_t zs[HYDROLOGY_AROUND];
    float32_t flood[HYDROLOGY_AROUND];
    float32_t spread[HYDROLOGY_AROUND];
    float32_t lava[HYDROLOGY_AROUND];
    const int64_t size = (int64_t)TERRAIN_HYDROLOGY_AQUIFER_CELL;

    around->cell_x = cell_x;
    around->cell_y = cell_y;
    around->cell_z = cell_z;
    around->filled = 1u;
    for (uint32_t i = 0u; i < HYDROLOGY_AROUND; i++) {
        const int64_t dx = (int64_t)(i % 3u) - 1;
        const int64_t dy = (int64_t)((i / 3u) % 3u) - 1;
        const int64_t dz = (int64_t)(i / 9u) - 1;
        terrain_rng_t rng = terrain_rng_start(terrain_rng_hash3(hydrology->aquifer_seed, (int32_t)(cell_x + dx),
                                                                (int32_t)(cell_y + dy), (int32_t)(cell_z + dz)));
        const int64_t site_x =
            dx * size + (int64_t)(HYDROLOGY_SITE_MIN + terrain_rng_next_below(&rng, HYDROLOGY_SITE_RANGE));
        const int64_t site_y =
            dy * size + (int64_t)(HYDROLOGY_SITE_MIN + terrain_rng_next_below(&rng, HYDROLOGY_SITE_RANGE));
        const int64_t site_z =
            dz * size + (int64_t)(HYDROLOGY_SITE_MIN + terrain_rng_next_below(&rng, HYDROLOGY_SITE_RANGE));
        around->cells[i].site_x = (int16_t)site_x;
        around->cells[i].site_y = (int16_t)site_y;
        around->cells[i].site_z = (int16_t)site_z;
        xs[i] = (float64_t)(cell_x * size + site_x);
        ys[i] = (float64_t)(cell_y * size + site_y);
        zs[i] = (float64_t)(cell_z * size + site_z);
    }
    terrain_noise_sample_points(hydrology->borrowed_flood, xs, ys, zs, HYDROLOGY_AROUND, flood);
    terrain_noise_sample_points(hydrology->borrowed_level, xs, ys, zs, HYDROLOGY_AROUND, spread);
    terrain_noise_sample_points(hydrology->borrowed_lava, xs, ys, zs, HYDROLOGY_AROUND, lava);

    int32_t top_level = HYDROLOGY_DRY;
    for (uint32_t i = 0u; i < HYDROLOGY_AROUND; i++) {
        hydrology_cell_t *const cell = &around->cells[i];
        cell->fluid = BLOCK_ID_AIR;
        cell->level = HYDROLOGY_DRY;
        if (hydrology_spread(flood[i], HYDROLOGY_LIMIT_FLOOD) < HYDROLOGY_FLOOD_WET_FROM) {
            continue;
        }
        const int64_t band = hydrology_floor_div((int64_t)zs[i], (int64_t)HYDROLOGY_LEVEL_BAND);
        const int32_t steps = (int32_t)(hydrology_spread(spread[i], HYDROLOGY_LIMIT_LEVEL) * HYDROLOGY_LEVEL_STEPS);
        cell->level =
            (int32_t)(band * (int64_t)HYDROLOGY_LEVEL_BAND) + HYDROLOGY_LEVEL_MIDDLE + steps * HYDROLOGY_LEVEL_STEP;
        cell->fluid = BLOCK_ID_WATER;
        if (cell->level <= TERRAIN_HYDROLOGY_AQUIFER_LAVA_TOP &&
            hydrology_spread(lava[i], HYDROLOGY_LIMIT_LAVA) >= HYDROLOGY_LAVA_FROM) {
            cell->fluid = BLOCK_ID_LAVA;
        }
        top_level = cell->level > top_level ? cell->level : top_level;
    }
    around->top_level = top_level;
}

/* What a voxel deep in the ground holds: what its nearest site's cell holds, unless it is within the barrier of the plane
 * halfway to a cell holding something else. Whole numbers, so that no sqrt is needed: the difference of two squared distances
 * is the distance from that plane, times twice the distance between the sites. */
static uint16_t hydrology_aquifer_block(const hydrology_around_t *around, int64_t x, int64_t y, int64_t z) {
    if (z > (int64_t)around->top_level) {
        return BLOCK_ID_AIR;
    }
    const int64_t size = (int64_t)TERRAIN_HYDROLOGY_AQUIFER_CELL;
    const int64_t local_x = x - around->cell_x * size;
    const int64_t local_y = y - around->cell_y * size;
    const int64_t local_z = z - around->cell_z * size;
    int64_t squares[HYDROLOGY_AROUND];
    uint32_t nearest = 0u;
    for (uint32_t i = 0u; i < HYDROLOGY_AROUND; i++) {
        const int64_t dx = (int64_t)around->cells[i].site_x - local_x;
        const int64_t dy = (int64_t)around->cells[i].site_y - local_y;
        const int64_t dz = (int64_t)around->cells[i].site_z - local_z;
        squares[i] = dx * dx + dy * dy + dz * dz;
        nearest = squares[i] < squares[nearest] ? i : nearest;
    }
    const hydrology_cell_t *const own = &around->cells[nearest];
    const bool own_holds = (int64_t)own->level >= z;
    for (uint32_t i = 0u; i < HYDROLOGY_AROUND; i++) {
        const hydrology_cell_t *const other = &around->cells[i];
        if (i == nearest || (other->level == own->level && other->fluid == own->fluid)) {
            continue;
        }
        if (!own_holds && (int64_t)other->level < z) {
            continue;
        }
        const int64_t apart_x = (int64_t)other->site_x - (int64_t)own->site_x;
        const int64_t apart_y = (int64_t)other->site_y - (int64_t)own->site_y;
        const int64_t apart_z = (int64_t)other->site_z - (int64_t)own->site_z;
        const int64_t apart = apart_x * apart_x + apart_y * apart_y + apart_z * apart_z;
        const int64_t difference = squares[i] - squares[nearest];
        const int64_t reach = 2 * HYDROLOGY_BARRIER_BLOCKS;
        if (difference * difference <= reach * reach * apart) {
            return BLOCK_ID_STONE;
        }
    }
    return own_holds ? own->fluid : BLOCK_ID_AIR;
}

/* ---- The flood ---------------------------------------------------------------------------------------------------- */

/* What an opened voxel holds. See the README's table. band_here is where the near-surface rule starts on this column, and
 * band_nearby the lowest it starts on the four beside it. */
static uint16_t hydrology_opened_block(const terrain_hydrology_t *hydrology, hydrology_around_t *around, int64_t x,
                                       int64_t y, int64_t z, float32_t band_here, float32_t band_nearby) {
    const float32_t z_float = (float32_t)z;
    const int64_t sea = (int64_t)MC_WORLD_Z_SEA;
    if (z <= (int64_t)TERRAIN_HYDROLOGY_LAVA_TOP) {
        return BLOCK_ID_LAVA;
    }
    if (z_float >= band_here) {
        return z <= sea ? BLOCK_ID_WATER : BLOCK_ID_AIR;
    }
    const int64_t cell_x = x >> HYDROLOGY_AQUIFER_SHIFT;
    const int64_t cell_y = y >> HYDROLOGY_AQUIFER_SHIFT;
    const int64_t cell_z = z >> HYDROLOGY_AQUIFER_SHIFT;
    if (around->filled == 0u || around->cell_x != cell_x || around->cell_y != cell_y || around->cell_z != cell_z) {
        hydrology_around_fill(hydrology, cell_x, cell_y, cell_z, around);
    }
    const uint16_t block = hydrology_aquifer_block(around, x, y, z);

    /* Against the sea, or the open air, right beside it or just above it. */
    const bool beside = z_float >= band_nearby;
    const bool under = z_float + 1.0f32 >= band_here;
    if ((beside && z <= sea) || (under && z + 1 <= sea)) {
        return BLOCK_ID_STONE;
    }
    if ((beside || under) && block != BLOCK_ID_AIR) {
        return BLOCK_ID_STONE;
    }
    /* Against the lava under the world, from above. */
    if (z == (int64_t)TERRAIN_HYDROLOGY_LAVA_TOP + 1 && block == BLOCK_ID_WATER) {
        return BLOCK_ID_STONE;
    }
    return block;
}

static void hydrology_flood_tile(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                 const hydrology_span_t *span, const uint8_t *opened, uint16_t *blocks) {
    const uint32_t width = (uint32_t)(span->x_last - span->x_first) + 1u;
    const uint32_t height = (uint32_t)(span->y_last - span->y_first) + 1u;
    const uint64_t row_stride = box->count_x;
    const uint64_t layer_stride = row_stride * box->count_y;
    const uint64_t corner = (uint64_t)(span->x_first - (int64_t)box->origin_x) +
                            row_stride * (uint64_t)(span->y_first - (int64_t)box->origin_y);

    /* Most tiles have nothing opened, and they should cost nothing. */
    bool any = false;
    for (uint32_t iz = 0u; iz < box->count_z && !any; iz++) {
        const uint8_t *const layer = opened + (uint64_t)iz * layer_stride + corner;
        for (uint32_t iy = 0u; iy < height && !any; iy++) {
            for (uint32_t ix = 0u; ix < width && !any; ix++) {
                any = layer[ix + row_stride * iy] != 0u;
            }
        }
    }
    if (!any) {
        return;
    }

    ASSERT(span->x_first > -(1ll << 30) && span->x_last < (1ll << 30));
    ASSERT(span->y_first > -(1ll << 30) && span->y_last < (1ll << 30));
    hydrology_lattice_t lattice;
    hydrology_lattice_fill(hydrology, span, 1, &lattice);

    float32_t bands[HYDROLOGY_BANDS];
    const uint32_t band_side = width + 2u;
    for (uint32_t iy = 0u; iy < height + 2u; iy++) {
        for (uint32_t ix = 0u; ix < band_side; ix++) {
            const terrain_hydrology_column_t column =
                hydrology_column_in(&lattice, span->x_first + (int64_t)ix - 1, span->y_first + (int64_t)iy - 1);
            bands[ix + band_side * iy] = column.ground - column.softness - (float32_t)TERRAIN_HYDROLOGY_SURFACE_BAND;
        }
    }

    hydrology_around_t around;
    around.filled = 0u;
    for (uint32_t iz = 0u; iz < box->count_z; iz++) {
        const int64_t z = (int64_t)box->origin_z + (int64_t)iz;
        if (z <= (int64_t)MC_WORLD_Z_MIN || z > (int64_t)MC_WORLD_Z_MAX) {
            continue;
        }
        const uint64_t layer_at = (uint64_t)iz * layer_stride + corner;
        for (uint32_t iy = 0u; iy < height; iy++) {
            for (uint32_t ix = 0u; ix < width; ix++) {
                const uint64_t at = layer_at + ix + row_stride * iy;
                if (opened[at] == 0u) {
                    continue;
                }
                const uint32_t b = (ix + 1u) + band_side * (iy + 1u);
                float32_t nearby = bands[b - 1u];
                nearby = bands[b + 1u] < nearby ? bands[b + 1u] : nearby;
                nearby = bands[b - band_side] < nearby ? bands[b - band_side] : nearby;
                nearby = bands[b + band_side] < nearby ? bands[b + band_side] : nearby;
                blocks[at] = hydrology_opened_block(hydrology, &around, span->x_first + (int64_t)ix,
                                                    span->y_first + (int64_t)iy, z, bands[b], nearby);
            }
        }
    }
}

void terrain_hydrology_flood_box(const terrain_hydrology_t *hydrology, const terrain_density_box_t *box,
                                 const uint8_t *opened, uint16_t *blocks) {
    if (box->count_x == 0u || box->count_y == 0u || box->count_z == 0u) {
        return;
    }
    hydrology_box_tiles(hydrology, box, opened, blocks, hydrology_flood_tile);
}

uint16_t terrain_hydrology_fluid_at(const terrain_hydrology_t *hydrology, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = {
        .origin_x = x, .origin_y = y, .origin_z = z, .count_x = 1u, .count_y = 1u, .count_z = 1u};
    const uint8_t opened = 1u;
    uint16_t block = BLOCK_ID_AIR;
    terrain_hydrology_flood_box(hydrology, &box, &opened, &block);
    return block;
}
