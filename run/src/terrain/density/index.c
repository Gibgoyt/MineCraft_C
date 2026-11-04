/* See README.md. */
#include "terrain/density/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <string.h>

enum : uint32_t {
    /* Columns profiled at once, and so the size of the arrays on the stack. A whole number of 8s saves the kernel its tail. */
    DENSITY_BATCH_COLUMNS = 128u,
    DENSITY_CELL_XY_SHIFT = 2u,
    DENSITY_CELL_Z_SHIFT = 3u,
    /* A box is worked through in tiles of this many blocks a side, lined up on the world, so a tile's lattice fits on the stack
     * whatever size the box is. */
    DENSITY_TILE_SHIFT = 5u,
    DENSITY_TILE_LATTICE_SIDE = (1u << DENSITY_TILE_SHIFT) / TERRAIN_DENSITY_CELL_XY + 1u,
    DENSITY_TILE_LATTICE = DENSITY_TILE_LATTICE_SIDE * DENSITY_TILE_LATTICE_SIDE,
};
static_assert(TERRAIN_DENSITY_CELL_XY == 1u << DENSITY_CELL_XY_SHIFT);
static_assert(TERRAIN_DENSITY_CELL_Z == 1u << DENSITY_CELL_Z_SHIFT);
static_assert(DENSITY_TILE_LATTICE <= DENSITY_BATCH_COLUMNS);

/* A knot: x, then y. The README says why these. Changing any number here changes every world. */
typedef struct density_knot_t {
    float64_t x; /* 0 */
    float64_t y; /* 8 */
} density_knot_t;
static_assert(sizeof(density_knot_t) == 16);

static const density_knot_t DENSITY_KNOTS_CONTINENTAL[] = {
    {-1.0f64, -110.0f64}, {-0.88f64, -90.0f64}, {-0.77f64, -64.0f64}, {-0.6f64, -36.0f64}, {-0.5f64, -10.0f64},
    {-0.45f64, 3.0f64},   {-0.34f64, 10.0f64},  {-0.07f64, 24.0f64},  {0.43f64, 44.0f64},  {1.0f64, 64.0f64},
};
static const density_knot_t DENSITY_KNOTS_INLAND[] = {
    {-1.0f64, 0.0f64}, {-0.5f64, 0.0f64}, {-0.34f64, 0.1f64}, {-0.07f64, 0.6f64}, {0.21f64, 1.0f64}, {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_RELIEF[] = {
    {-1.0f64, 1.0f64}, {-0.5f64, 0.9f64}, {-0.2f64, 0.55f64}, {0.1f64, 0.2f64},
    {0.4f64, 0.05f64}, {0.7f64, 0.0f64},  {1.0f64, 0.0f64},
};
/* (2^(3 * (r + 1)) - 1) / 63 at these r: an exponential, so that most of the world's ridges are low and a few are huge. */
static const density_knot_t DENSITY_KNOTS_PEAK[] = {
    {-1.0f64, 0.0f64},     {-0.5f64, 0.029023f64}, {0.0f64, 0.111111f64}, {0.25f64, 0.197688f64},
    {0.5f64, 0.343292f64}, {0.75f64, 0.588169f64}, {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_VALLEY[] = {
    {-1.0f64, 0.3f64},
    {-0.6f64, 0.65f64},
    {-0.2f64, 1.0f64},
    {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_HILL_LAND[] = {
    {-1.0f64, 0.0f64},
    {-0.45f64, 0.0f64},
    {-0.2f64, 1.0f64},
    {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_HILL_SEA[] = {
    {-1.0f64, 1.0f64},
    {-0.58f64, 1.0f64},
    {-0.51f64, 0.0f64},
    {1.0f64, 0.0f64},
};
static const density_knot_t DENSITY_KNOTS_HILL_RELIEF[] = {
    {-1.0f64, 1.0f64},
    {0.0f64, 0.5f64},
    {0.6f64, 0.12f64},
    {1.0f64, 0.0f64},
};

/* The height formula's sizes, in blocks. The limits in the header are their sum. */
static const float64_t DENSITY_MOUNTAIN_BLOCKS = 320.0f64;
static const float64_t DENSITY_JAGGED_BLOCKS = 34.0f64;
static const float64_t DENSITY_HILL_BLOCKS_MIN = 6.0f64;
static const float64_t DENSITY_HILL_BLOCKS_EXTRA = 30.0f64;
static const float64_t DENSITY_HILL_BLOCKS_SEA = 6.0f64;
static const float64_t DENSITY_SOFTNESS_MIN = 4.0f64;
static const float64_t DENSITY_SOFTNESS_EXTRA = 26.0f64;

/* What each field looks like. */
static const terrain_noise_shape_t DENSITY_SHAPE_HILLS = {
    .frequency_x = 1.0f64 / 192.0f64,
    .frequency_y = 1.0f64 / 192.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};
static const terrain_noise_shape_t DENSITY_SHAPE_JAGGED = {
    .frequency_x = 1.0f64 / 32.0f64,
    .frequency_y = 1.0f64 / 32.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
/* Its last octave's cell is 16 blocks, as fine as a lattice 8 apart can hold. */
static const terrain_noise_shape_t DENSITY_SHAPE_DETAIL = {
    .frequency_x = 1.0f64 / 64.0f64,
    .frequency_y = 1.0f64 / 64.0f64,
    .frequency_z = 1.0f64 / 64.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};

/* A raw value at this far from 0 becomes -1 or 1: about twice what each measures as its standard deviation. */
static const float64_t DENSITY_LIMIT_HILLS = 0.34f64;
static const float64_t DENSITY_LIMIT_JAGGED = 0.39f64;
static const float64_t DENSITY_LIMIT_DETAIL = 0.35f64;

/* Corners at least this far from 0 mean a whole cell has that sign, rounding and all. The largest density is about 180, and
 * a float32_t's rounding there is about 0.00002. */
static const float32_t DENSITY_SURE_MARGIN = 0.001f32;

/* ---- Splines ----------------------------------------------------------------------------------------------------- */

/* Fritsch and Carlson's slopes, in the form with a weighted harmonic mean: 0 where the curve turns, and never more than three
 * times the shallower side. That is what keeps each piece between its two knots, so a spline's limits are its knots' limits,
 * and the header's limits can be added up by hand. Worked out here, and not typed, so that a knot can move on its own. */
static void density_spline_build(terrain_density_spline_t *spline, const density_knot_t *knots, uint32_t count) {
    float64_t secants[TERRAIN_DENSITY_SPLINE_KNOTS_MAX];
    float64_t widths[TERRAIN_DENSITY_SPLINE_KNOTS_MAX];
    ASSERT(count >= 2u && count <= TERRAIN_DENSITY_SPLINE_KNOTS_MAX);
    spline->count = count;
    for (uint32_t i = 0u; i < count; i++) {
        spline->x[i] = knots[i].x;
        spline->y[i] = knots[i].y;
    }
    for (uint32_t i = 0u; i + 1u < count; i++) {
        widths[i] = knots[i + 1u].x - knots[i].x;
        ASSERT(widths[i] > 0.0f64);
        secants[i] = (knots[i + 1u].y - knots[i].y) / widths[i];
    }
    spline->slope[0] = secants[0];
    spline->slope[count - 1u] = secants[count - 2u];
    for (uint32_t i = 1u; i + 1u < count; i++) {
        const float64_t before = secants[i - 1u];
        const float64_t after = secants[i];
        if (before * after <= 0.0f64) {
            spline->slope[i] = 0.0f64;
            continue;
        }
        const float64_t weight_before = 2.0f64 * widths[i] + widths[i - 1u];
        const float64_t weight_after = widths[i] + 2.0f64 * widths[i - 1u];
        spline->slope[i] = (weight_before + weight_after) / (weight_before / before + weight_after / after);
    }
}

float64_t terrain_density_spline_at(const terrain_density_spline_t *spline, float64_t x) {
    const uint32_t last = spline->count - 1u;
    if (x <= spline->x[0]) {
        return spline->y[0];
    }
    if (x >= spline->x[last]) {
        return spline->y[last];
    }
    uint32_t i = 0u;
    while (x >= spline->x[i + 1u]) {
        i++;
    }
    const float64_t width = spline->x[i + 1u] - spline->x[i];
    const float64_t t = (x - spline->x[i]) / width;
    const float64_t t2 = t * t;
    const float64_t t3 = t2 * t;
    const float64_t y0 = spline->y[i];
    const float64_t y1 = spline->y[i + 1u];
    const float64_t value = (2.0f64 * t3 - 3.0f64 * t2 + 1.0f64) * y0 +
                            (t3 - 2.0f64 * t2 + t) * width * spline->slope[i] + (3.0f64 * t2 - 2.0f64 * t3) * y1 +
                            (t3 - t2) * width * spline->slope[i + 1u];
    /* The slopes keep it between the knots exactly. This is for rounding, so a limit is a limit to the last bit. */
    const float64_t low = y0 < y1 ? y0 : y1;
    const float64_t high = y0 < y1 ? y1 : y0;
    return value < low ? low : (value > high ? high : value);
}

terrain_density_profile_t terrain_density_profile_from(const terrain_density_t *density,
                                                       const terrain_density_inputs_t *inputs) {
    const float64_t continentalness = (float64_t)inputs->continentalness;
    const float64_t erosion = (float64_t)inputs->erosion;
    const float64_t ridges = (float64_t)inputs->ridges;

    float64_t base = terrain_density_spline_at(&density->continental, continentalness);
    if (base > 0.0f64) {
        base *= terrain_density_spline_at(&density->valley, ridges);
    }
    const float64_t high_ground = terrain_density_spline_at(&density->inland, continentalness) *
                                  terrain_density_spline_at(&density->relief, erosion);
    const float64_t mountain = high_ground * terrain_density_spline_at(&density->peak, ridges);
    const float64_t hill_blocks =
        terrain_density_spline_at(&density->hill_land, continentalness) *
            (DENSITY_HILL_BLOCKS_MIN +
             DENSITY_HILL_BLOCKS_EXTRA * terrain_density_spline_at(&density->hill_relief, erosion)) +
        terrain_density_spline_at(&density->hill_sea, continentalness) * DENSITY_HILL_BLOCKS_SEA;

    const float64_t height = base + DENSITY_MOUNTAIN_BLOCKS * mountain + hill_blocks * (float64_t)inputs->hills +
                             DENSITY_JAGGED_BLOCKS * mountain * (float64_t)inputs->jagged;
    return (terrain_density_profile_t){
        .height = (float32_t)height,
        .softness = (float32_t)(DENSITY_SOFTNESS_MIN + DENSITY_SOFTNESS_EXTRA * high_ground),
    };
}

/* ---- Init -------------------------------------------------------------------------------------------------------- */

bool terrain_density_init(terrain_density_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                          const terrain_climate_t *climate) {
    *out = nullptr;
    terrain_density_t *const density = arena_alloc(arena, sizeof(*density), alignof(terrain_density_t));
    memset(density, 0, sizeof(*density));
    density->borrowed_climate = climate;
#define DENSITY_BUILD(field, knots) density_spline_build(&density->field, knots, sizeof(knots) / sizeof(knots[0]))
    DENSITY_BUILD(continental, DENSITY_KNOTS_CONTINENTAL);
    DENSITY_BUILD(inland, DENSITY_KNOTS_INLAND);
    DENSITY_BUILD(relief, DENSITY_KNOTS_RELIEF);
    DENSITY_BUILD(peak, DENSITY_KNOTS_PEAK);
    DENSITY_BUILD(valley, DENSITY_KNOTS_VALLEY);
    DENSITY_BUILD(hill_land, DENSITY_KNOTS_HILL_LAND);
    DENSITY_BUILD(hill_sea, DENSITY_KNOTS_HILL_SEA);
    DENSITY_BUILD(hill_relief, DENSITY_KNOTS_HILL_RELIEF);
#undef DENSITY_BUILD
    /* One at a time, and all of them, so that the log names the field the bank refused. The order moves nothing: a field
     * comes from its name. */
    if (!terrain_noise_bank_add(bank, "density/hills", &DENSITY_SHAPE_HILLS, &density->borrowed_hills) ||
        !terrain_noise_bank_add(bank, "density/jagged", &DENSITY_SHAPE_JAGGED, &density->borrowed_jagged) ||
        !terrain_noise_bank_add(bank, "density/detail", &DENSITY_SHAPE_DETAIL, &density->borrowed_detail)) {
        return false;
    }
    *out = density;
    return true;
}

void terrain_density_deinit(terrain_density_t *density) {
    memset(density, 0, sizeof(*density));
}

/* ---- Profiles ---------------------------------------------------------------------------------------------------- */

/* As the climate spreads its fields, and for the same reason. */
static float32_t density_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* Up to DENSITY_BATCH_COLUMNS columns. */
static void density_profile_batch(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                  uint32_t count, float32_t *heights, float32_t *softness) {
    float32_t continentalness[DENSITY_BATCH_COLUMNS];
    float32_t erosion[DENSITY_BATCH_COLUMNS];
    float32_t ridges[DENSITY_BATCH_COLUMNS];
    float32_t hills[DENSITY_BATCH_COLUMNS];
    float32_t jagged[DENSITY_BATCH_COLUMNS];
    const float64_t zs[DENSITY_BATCH_COLUMNS] = {0.0f64}; /* neither 2D field has a frequency on z */

    const terrain_climate_arrays_t climate = {
        .continentalness = continentalness,
        .erosion = erosion,
        .ridges = ridges,
    };
    terrain_climate_sample_points(density->borrowed_climate, xs, ys, count, &climate);
    terrain_noise_sample_points(density->borrowed_hills, xs, ys, zs, count, hills);
    terrain_noise_sample_points(density->borrowed_jagged, xs, ys, zs, count, jagged);
    for (uint32_t i = 0u; i < count; i++) {
        const terrain_density_inputs_t inputs = {
            .continentalness = continentalness[i],
            .erosion = erosion[i],
            .ridges = ridges[i],
            .hills = density_spread(hills[i], DENSITY_LIMIT_HILLS),
            .jagged = density_spread(jagged[i], DENSITY_LIMIT_JAGGED),
        };
        const terrain_density_profile_t profile = terrain_density_profile_from(density, &inputs);
        heights[i] = profile.height;
        softness[i] = profile.softness;
    }
}

void terrain_density_profile_area(const terrain_density_t *density, const terrain_climate_area_t *area,
                                  float32_t *heights, float32_t *softness) {
    float64_t xs[DENSITY_BATCH_COLUMNS];
    float64_t ys[DENSITY_BATCH_COLUMNS];
    uint32_t held = 0u;
    uint64_t done = 0u;
    for (uint32_t iy = 0u; iy < area->count_y; iy++) {
        const float64_t y = (float64_t)((int64_t)area->origin_y + (int64_t)area->step_y * (int64_t)iy);
        for (uint32_t ix = 0u; ix < area->count_x; ix++) {
            xs[held] = (float64_t)((int64_t)area->origin_x + (int64_t)area->step_x * (int64_t)ix);
            ys[held] = y;
            held++;
            if (held == DENSITY_BATCH_COLUMNS) {
                density_profile_batch(density, xs, ys, held, heights + done, softness + done);
                done += held;
                held = 0u;
            }
        }
    }
    if (held > 0u) {
        density_profile_batch(density, xs, ys, held, heights + done, softness + done);
    }
}

/* ---- The lattice ------------------------------------------------------------------------------------------------- */

/* The columns of one tile that a box needs, and their ground. */
typedef struct density_lattice_t {
    float32_t heights[DENSITY_TILE_LATTICE];  /* 0 */
    float32_t softness[DENSITY_TILE_LATTICE]; /* 324 */
    float64_t xs[DENSITY_TILE_LATTICE];       /* 648 */
    float64_t ys[DENSITY_TILE_LATTICE];       /* 1296 */
    uint32_t count;                           /* 1944 */
    uint8_t _pad0[4];                         /* 1948 */
} density_lattice_t;
static_assert(sizeof(density_lattice_t) == 1952);

/* What the 3D noise is allowed to do at a height. All of it within the softness of the column's height, so that the ground
 * stays within it, and none of it from twice that, so that nothing has to be sampled there. Between, it fades, to leave no
 * step. The sign cannot change outside the softness: past 1, the size of what is added is less than the value it is added
 * to. */
static float32_t density_fade(float32_t depth) {
    const float32_t size = depth < 0.0f32 ? -depth : depth;
    if (size <= 1.0f32) {
        return 1.0f32;
    }
    return size >= 2.0f32 ? 0.0f32 : 2.0f32 - size;
}

/* The density of every lattice column at one height. Only those within reach of the noise sample it, and that is most of
 * why this costs so little: nearly all of a column is deep underground or high in the air. */
static void density_plane(const terrain_density_t *density, const density_lattice_t *lattice, int64_t level,
                          float32_t *plane) {
    float64_t xs[DENSITY_TILE_LATTICE];
    float64_t ys[DENSITY_TILE_LATTICE];
    float64_t zs[DENSITY_TILE_LATTICE];
    float32_t raw[DENSITY_TILE_LATTICE];
    uint8_t which[DENSITY_TILE_LATTICE];
    static_assert(DENSITY_TILE_LATTICE <= UINT8_MAX);

    const int64_t z_whole = (int64_t)MC_WORLD_Z_MIN + level * (int64_t)TERRAIN_DENSITY_CELL_Z;
    const float32_t z = (float32_t)z_whole;
    uint32_t reached = 0u;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        const float32_t depth = (lattice->heights[i] - z) / lattice->softness[i];
        plane[i] = depth;
        if (density_fade(depth) > 0.0f32) {
            xs[reached] = lattice->xs[i];
            ys[reached] = lattice->ys[i];
            zs[reached] = (float64_t)z_whole;
            which[reached] = (uint8_t)i;
            reached++;
        }
    }
    if (reached == 0u) {
        return;
    }
    terrain_noise_sample_points(density->borrowed_detail, xs, ys, zs, reached, raw);
    for (uint32_t r = 0u; r < reached; r++) {
        const uint32_t i = which[r];
        plane[i] += density_spread(raw[r], DENSITY_LIMIT_DETAIL) * density_fade(plane[i]);
    }
}

/* ---- Blocks ------------------------------------------------------------------------------------------------------ */

static uint16_t density_block_of(int32_t z, bool solid) {
    if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
        return BLOCK_ID_AIR;
    }
    if (z == MC_WORLD_Z_MIN) {
        return BLOCK_ID_BEDROCK;
    }
    if (solid) {
        return BLOCK_ID_STONE;
    }
    return z <= MC_WORLD_Z_SEA ? BLOCK_ID_WATER : BLOCK_ID_AIR;
}

/* ---- A box ------------------------------------------------------------------------------------------------------- */

/* Where a tile's output goes. Exactly one of the two is set. */
typedef struct density_output_t {
    const terrain_density_box_t *borrowed_box; /* 0 */
    float32_t *values;                         /* 8 */
    uint16_t *blocks;                          /* 16 */
} density_output_t;
static_assert(sizeof(density_output_t) == 24);

/* The part of the box in one tile, both ends included, and the lattice columns round it. */
typedef struct density_span_t {
    int64_t x_first; /* 0 */
    int64_t x_last;  /* 8 */
    int64_t y_first; /* 16 */
    int64_t y_last;  /* 24 */
    int64_t cell_x;  /* 32: the lattice column at index 0 */
    int64_t cell_y;  /* 40 */
    uint32_t side_x; /* 48: lattice columns on a side */
    uint32_t side_y; /* 52 */
} density_span_t;
static_assert(sizeof(density_span_t) == 56);

static void density_lattice_fill(const terrain_density_t *density, const density_span_t *span,
                                 density_lattice_t *lattice) {
    lattice->count = span->side_x * span->side_y;
    for (uint32_t iy = 0u; iy < span->side_y; iy++) {
        for (uint32_t ix = 0u; ix < span->side_x; ix++) {
            const uint32_t i = ix + span->side_x * iy;
            lattice->xs[i] = (float64_t)((span->cell_x + (int64_t)ix) * (int64_t)TERRAIN_DENSITY_CELL_XY);
            lattice->ys[i] = (float64_t)((span->cell_y + (int64_t)iy) * (int64_t)TERRAIN_DENSITY_CELL_XY);
        }
    }
    density_profile_batch(density, lattice->xs, lattice->ys, lattice->count, lattice->heights, lattice->softness);
}

/* One z's voxels of a tile, when the whole of it is sure of its sign. */
static void density_write_sure(const density_output_t *output, const density_span_t *span, int64_t z, bool solid) {
    const terrain_density_box_t *const box = output->borrowed_box;
    const uint16_t block = density_block_of((int32_t)z, solid);
    const uint64_t z_at = (uint64_t)(z - box->origin_z) * box->count_y;
    for (int64_t y = span->y_first; y <= span->y_last; y++) {
        uint16_t *const row = output->blocks + (z_at + (uint64_t)(y - box->origin_y)) * box->count_x;
        for (int64_t x = span->x_first; x <= span->x_last; x++) {
            row[x - box->origin_x] = block;
        }
    }
}

/* One z's voxels of a tile, each from the four lattice columns round it, already interpolated to this z in plane. Always x
 * first, then y, whichever box asked, so the bits cannot depend on the box. */
static void density_write_voxels(const density_output_t *output, const density_span_t *span, int64_t z,
                                 const float32_t *plane) {
    const terrain_density_box_t *const box = output->borrowed_box;
    const uint64_t z_at = (uint64_t)(z - box->origin_z) * box->count_y;
    const float32_t per_block = 1.0f32 / (float32_t)TERRAIN_DENSITY_CELL_XY;
    for (int64_t y = span->y_first; y <= span->y_last; y++) {
        const uint64_t row_at = (z_at + (uint64_t)(y - box->origin_y)) * box->count_x;
        const int64_t cell_y = y >> DENSITY_CELL_XY_SHIFT;
        const float32_t t_y = (float32_t)(y & (int64_t)(TERRAIN_DENSITY_CELL_XY - 1u)) * per_block;
        const float32_t *const south = plane + (uint64_t)(cell_y - span->cell_y) * span->side_x;
        const float32_t *const north = south + span->side_x;
        for (int64_t x = span->x_first; x <= span->x_last; x++) {
            const uint64_t cell_x = (uint64_t)((x >> DENSITY_CELL_XY_SHIFT) - span->cell_x);
            const float32_t t_x = (float32_t)(x & (int64_t)(TERRAIN_DENSITY_CELL_XY - 1u)) * per_block;
            const float32_t near = south[cell_x] + t_x * (south[cell_x + 1u] - south[cell_x]);
            const float32_t far = north[cell_x] + t_x * (north[cell_x + 1u] - north[cell_x]);
            const float32_t value = near + t_y * (far - near);
            if (output->values != nullptr) {
                output->values[row_at + (uint64_t)(x - box->origin_x)] = value;
            } else {
                output->blocks[row_at + (uint64_t)(x - box->origin_x)] = density_block_of((int32_t)z, value > 0.0f32);
            }
        }
    }
}

/* Every z of the box in one tile, working up through the lattice's levels and keeping the plane two levels share. */
static void density_tile(const terrain_density_t *density, const density_output_t *output, const density_span_t *span) {
    const terrain_density_box_t *const box = output->borrowed_box;
    density_lattice_t lattice;
    float32_t plane_a[DENSITY_TILE_LATTICE];
    float32_t plane_b[DENSITY_TILE_LATTICE];
    float32_t interpolated[DENSITY_TILE_LATTICE];
    density_lattice_fill(density, span, &lattice);

    const int64_t z_first = box->origin_z;
    const int64_t z_last = z_first + (int64_t)box->count_z - 1;
    const int64_t level_first = (z_first - (int64_t)MC_WORLD_Z_MIN) >> DENSITY_CELL_Z_SHIFT;
    const int64_t level_last = (z_last - (int64_t)MC_WORLD_Z_MIN) >> DENSITY_CELL_Z_SHIFT;
    const float32_t per_block = 1.0f32 / (float32_t)TERRAIN_DENSITY_CELL_Z;

    float32_t *below = plane_a;
    float32_t *above = plane_b;
    density_plane(density, &lattice, level_first, below);
    for (int64_t level = level_first; level <= level_last; level++) {
        density_plane(density, &lattice, level + 1, above);
        const int64_t level_z = (int64_t)MC_WORLD_Z_MIN + level * (int64_t)TERRAIN_DENSITY_CELL_Z;
        const int64_t from = level_z > z_first ? level_z : z_first;
        const int64_t to = level_z + (int64_t)TERRAIN_DENSITY_CELL_Z - 1 < z_last
                               ? level_z + (int64_t)TERRAIN_DENSITY_CELL_Z - 1
                               : z_last;

        /* Blocks only ask for a sign, and most levels of most columns have only one. */
        bool sure_solid = false;
        bool sure_open = false;
        if (output->blocks != nullptr) {
            sure_solid = true;
            sure_open = true;
            for (uint32_t i = 0u; i < lattice.count; i++) {
                sure_solid = sure_solid && below[i] > DENSITY_SURE_MARGIN && above[i] > DENSITY_SURE_MARGIN;
                sure_open = sure_open && below[i] < -DENSITY_SURE_MARGIN && above[i] < -DENSITY_SURE_MARGIN;
            }
        }
        for (int64_t z = from; z <= to; z++) {
            if (sure_solid || sure_open) {
                density_write_sure(output, span, z, sure_solid);
                continue;
            }
            const float32_t t_z = (float32_t)(z - level_z) * per_block;
            for (uint32_t i = 0u; i < lattice.count; i++) {
                interpolated[i] = below[i] + t_z * (above[i] - below[i]);
            }
            density_write_voxels(output, span, z, interpolated);
        }

        float32_t *const swap = below;
        below = above;
        above = swap;
    }
}

/* The box, tile by tile. Tiles are lined up on the world only to keep the stack small: what a voxel gets does not depend on
 * which tile it was in, as the lattice is lined up on the world too. */
static void density_box(const terrain_density_t *density, const density_output_t *output) {
    const terrain_density_box_t *const box = output->borrowed_box;
    if (box->count_x == 0u || box->count_y == 0u || box->count_z == 0u) {
        return;
    }
    const int64_t x_end = (int64_t)box->origin_x + (int64_t)box->count_x;
    const int64_t y_end = (int64_t)box->origin_y + (int64_t)box->count_y;
    const int64_t tile = 1ll << DENSITY_TILE_SHIFT;
    for (int64_t y = box->origin_y; y < y_end;) {
        const int64_t y_next = ((y >> DENSITY_TILE_SHIFT) + 1) * tile;
        const int64_t y_last = (y_next < y_end ? y_next : y_end) - 1;
        for (int64_t x = box->origin_x; x < x_end;) {
            const int64_t x_next = ((x >> DENSITY_TILE_SHIFT) + 1) * tile;
            const int64_t x_last = (x_next < x_end ? x_next : x_end) - 1;
            const int64_t cell_x = x >> DENSITY_CELL_XY_SHIFT;
            const int64_t cell_y = y >> DENSITY_CELL_XY_SHIFT;
            const density_span_t span = {
                .x_first = x,
                .x_last = x_last,
                .y_first = y,
                .y_last = y_last,
                .cell_x = cell_x,
                .cell_y = cell_y,
                .side_x = (uint32_t)((x_last >> DENSITY_CELL_XY_SHIFT) - cell_x) + 2u,
                .side_y = (uint32_t)((y_last >> DENSITY_CELL_XY_SHIFT) - cell_y) + 2u,
            };
            density_tile(density, output, &span);
            x = x_next;
        }
        y = y_next;
    }
}

uint64_t terrain_density_box_voxels(const terrain_density_box_t *box) {
    return (uint64_t)box->count_x * box->count_y * box->count_z;
}

void terrain_density_sample_box(const terrain_density_t *density, const terrain_density_box_t *box, float32_t *out) {
    const density_output_t output = {.borrowed_box = box, .values = out};
    density_box(density, &output);
}

void terrain_density_fill_box(const terrain_density_t *density, const terrain_density_box_t *box, uint16_t *out) {
    const density_output_t output = {.borrowed_box = box, .blocks = out};
    density_box(density, &output);
}

float32_t terrain_density_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = {
        .origin_x = x, .origin_y = y, .origin_z = z, .count_x = 1u, .count_y = 1u, .count_z = 1u};
    float32_t value = 0.0f32;
    terrain_density_sample_box(density, &box, &value);
    return value;
}

uint16_t terrain_density_block_at(const terrain_density_t *density, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = {
        .origin_x = x, .origin_y = y, .origin_z = z, .count_x = 1u, .count_y = 1u, .count_z = 1u};
    uint16_t block = BLOCK_ID_AIR;
    terrain_density_fill_box(density, &box, &block);
    return block;
}
