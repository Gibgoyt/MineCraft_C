/* See README.md. */
#include "terrain/cave/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <string.h>

enum : uint32_t {
    CAVE_CELL_SHIFT = 2u,
    /* A box is worked through in tiles of this many blocks a side, lined up on the world, so a tile's lattice fits on the stack
     * whatever size the box is. The size of a chunk, as that is what is asked for most. */
    CAVE_TILE_SHIFT = 4u,
    CAVE_TILE_SIDE = (1u << CAVE_TILE_SHIFT) / TERRAIN_CAVE_CELL + 1u,
    CAVE_TILE_LATTICE = CAVE_TILE_SIDE * CAVE_TILE_SIDE,
};
static_assert(TERRAIN_CAVE_CELL == 1u << CAVE_CELL_SHIFT);
static_assert(CAVE_TILE_LATTICE <= UINT8_MAX);

/* What each field looks like. The README's table says why these. Changing any number here changes every world. */
static const terrain_noise_shape_t CAVE_SHAPE_CHEESE = {
    .frequency_x = 1.0f64 / 80.0f64,
    .frequency_y = 1.0f64 / 80.0f64,
    .frequency_z = 1.0f64 / 40.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t CAVE_SHAPE_CHEESE_REGION = {
    .frequency_x = 1.0f64 / 256.0f64,
    .frequency_y = 1.0f64 / 256.0f64,
    .frequency_z = 1.0f64 / 128.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};
static const terrain_noise_shape_t CAVE_SHAPE_SPAGHETTI = {
    .frequency_x = 1.0f64 / 48.0f64,
    .frequency_y = 1.0f64 / 48.0f64,
    .frequency_z = 1.0f64 / 32.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};
static const terrain_noise_shape_t CAVE_SHAPE_SPAGHETTI_GATE = {
    .frequency_x = 1.0f64 / 128.0f64,
    .frequency_y = 1.0f64 / 128.0f64,
    .frequency_z = 1.0f64 / 128.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};
static const terrain_noise_shape_t CAVE_SHAPE_SPAGHETTI_WIDTH = {
    .frequency_x = 1.0f64 / 48.0f64,
    .frequency_y = 1.0f64 / 48.0f64,
    .frequency_z = 1.0f64 / 48.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};
static const terrain_noise_shape_t CAVE_SHAPE_NOODLE = {
    .frequency_x = 1.0f64 / 32.0f64,
    .frequency_y = 1.0f64 / 32.0f64,
    .frequency_z = 1.0f64 / 24.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};
static const terrain_noise_shape_t CAVE_SHAPE_NOODLE_GATE = {
    .frequency_x = 1.0f64 / 96.0f64,
    .frequency_y = 1.0f64 / 96.0f64,
    .frequency_z = 1.0f64 / 96.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 1u,
};

/* A raw value this far from 0 becomes -1 or 1: about twice what each measures as its standard deviation. */
static const float32_t CAVE_LIMIT_CHEESE = 0.40f32;
static const float32_t CAVE_LIMIT_ONE_OCTAVE = 0.54f32;

/* The sums in the README, under the same names. */
static const float32_t CAVE_CHEESE_REGION_SHARE = 0.35f32;
static const float32_t CAVE_CHEESE_THRESHOLD = 0.90f32;
static const float32_t CAVE_FLOOR_RAMP = 24.0f32;
static const float32_t CAVE_ROOF_RAMP = 12.0f32;
static const float32_t CAVE_ENTRANCE_DEPTH = 32.0f32;
static const float32_t CAVE_SPAGHETTI_GATE = -0.55f32;
static const float32_t CAVE_SPAGHETTI_GATE_ENTRANCE = 0.70f32;
static const float32_t CAVE_SPAGHETTI_GATE_RAMP = 0.2f32;
static const float32_t CAVE_SPAGHETTI_WIDTH_MIN = 0.065f32;
static const float32_t CAVE_SPAGHETTI_WIDTH_MAX = 0.13f32;
static const float32_t CAVE_NOODLE_GATE = 0.1f32;
static const float32_t CAVE_NOODLE_GATE_RAMP = 0.2f32;
static const float32_t CAVE_NOODLE_WIDTH = 0.055f32;

/* Corners at least this far from 0 mean a whole cell has that sign, rounding and all. */
static const float32_t CAVE_SURE_MARGIN = 0.001f32;

bool terrain_cave_init(terrain_cave_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                       const terrain_density_t *density) {
    *out = nullptr;
    terrain_cave_t *const cave = arena_alloc(arena, sizeof(*cave), alignof(terrain_cave_t));
    memset(cave, 0, sizeof(*cave));
    cave->borrowed_density = density;
    /* The order moves nothing: a field comes from its name. */
    if (!terrain_noise_bank_add(bank, "cave/cheese", &CAVE_SHAPE_CHEESE, &cave->borrowed_cheese) ||
        !terrain_noise_bank_add(bank, "cave/cheese_region", &CAVE_SHAPE_CHEESE_REGION, &cave->borrowed_cheese_region) ||
        !terrain_noise_bank_add(bank, "cave/spaghetti_a", &CAVE_SHAPE_SPAGHETTI, &cave->borrowed_spaghetti_a) ||
        !terrain_noise_bank_add(bank, "cave/spaghetti_b", &CAVE_SHAPE_SPAGHETTI, &cave->borrowed_spaghetti_b) ||
        !terrain_noise_bank_add(bank, "cave/spaghetti_gate", &CAVE_SHAPE_SPAGHETTI_GATE,
                                &cave->borrowed_spaghetti_gate) ||
        !terrain_noise_bank_add(bank, "cave/spaghetti_width", &CAVE_SHAPE_SPAGHETTI_WIDTH,
                                &cave->borrowed_spaghetti_width) ||
        !terrain_noise_bank_add(bank, "cave/noodle_a", &CAVE_SHAPE_NOODLE, &cave->borrowed_noodle_a) ||
        !terrain_noise_bank_add(bank, "cave/noodle_b", &CAVE_SHAPE_NOODLE, &cave->borrowed_noodle_b) ||
        !terrain_noise_bank_add(bank, "cave/noodle_gate", &CAVE_SHAPE_NOODLE_GATE, &cave->borrowed_noodle_gate)) {
        return false;
    }
    *out = cave;
    return true;
}

void terrain_cave_deinit(terrain_cave_t *cave) {
    memset(cave, 0, sizeof(*cave));
}

/* ---- The lattice ------------------------------------------------------------------------------------------------- */

/* The columns of one tile, and their ground. */
typedef struct cave_lattice_t {
    float32_t heights[CAVE_TILE_LATTICE];  /* 0 */
    float32_t softness[CAVE_TILE_LATTICE]; /* 100 */
    float64_t xs[CAVE_TILE_LATTICE];       /* 200 */
    float64_t ys[CAVE_TILE_LATTICE];       /* 400 */
    uint32_t count;                        /* 600 */
    uint8_t _pad0[4];                      /* 604 */
} cave_lattice_t;
static_assert(sizeof(cave_lattice_t) == 608);

/* One lattice level of a tile. Each value is a pure function of the caves and the point, except that a and b are worked out
 * only when something in reach asks for them. */
typedef struct cave_plane_t {
    float32_t cheese[CAVE_TILE_LATTICE];          /* 0: above 0 is a cavern */
    float32_t spaghetti_limit[CAVE_TILE_LATTICE]; /* 100: how far from the tube's middle is still tube */
    float32_t spaghetti_a[CAVE_TILE_LATTICE];     /* 200 */
    float32_t spaghetti_b[CAVE_TILE_LATTICE];     /* 300 */
    float32_t noodle_limit[CAVE_TILE_LATTICE];    /* 400 */
    float32_t noodle_a[CAVE_TILE_LATTICE];        /* 500 */
    float32_t noodle_b[CAVE_TILE_LATTICE];        /* 600 */
    float32_t z;                                  /* 700 */
    uint8_t any_cheese;                           /* 704 */
    uint8_t any_spaghetti;                        /* 705 */
    uint8_t any_noodle;                           /* 706 */
    uint8_t has_spaghetti_tube;                   /* 707 */
    uint8_t has_noodle_tube;                      /* 708 */
    uint8_t _pad0[3];                             /* 709 */
} cave_plane_t;
static_assert(sizeof(cave_plane_t) == 712);

/* As the climate spreads its fields, and for the same reason. */
static float32_t cave_spread(float32_t raw, float32_t limit) {
    float32_t unit = raw / limit;
    unit = unit > 1.0f32 ? 1.0f32 : unit;
    unit = unit < -1.0f32 ? -1.0f32 : unit;
    return unit * (1.5f32 - 0.5f32 * unit * unit);
}

static float32_t cave_clamp01(float32_t value) {
    return value < 0.0f32 ? 0.0f32 : (value > 1.0f32 ? 1.0f32 : value);
}

/* A field at some of a level's columns. */
static void cave_sample(const terrain_noise_field_t *field, const cave_lattice_t *lattice, float32_t z,
                        const uint8_t *which, uint32_t count, float32_t *raw) {
    float64_t xs[CAVE_TILE_LATTICE];
    float64_t ys[CAVE_TILE_LATTICE];
    float64_t zs[CAVE_TILE_LATTICE];
    for (uint32_t r = 0u; r < count; r++) {
        xs[r] = lattice->xs[which[r]];
        ys[r] = lattice->ys[which[r]];
        zs[r] = (float64_t)z;
    }
    terrain_noise_sample_points(field, xs, ys, zs, count, raw);
}

/* A kind's strength at each column of a level, from 0 for none to 1 for all of it, gathered per kind so that the noise of
 * a kind that cannot reach this level is never sampled. */
typedef struct cave_gates_t {
    float32_t cheese[CAVE_TILE_LATTICE];    /* 0 */
    float32_t spaghetti[CAVE_TILE_LATTICE]; /* 100 */
    float32_t noodle[CAVE_TILE_LATTICE];    /* 200 */
    float32_t surface[CAVE_TILE_LATTICE];   /* 300: 0 deep down, 1 at the ground */
} cave_gates_t;
static_assert(sizeof(cave_gates_t) == 400);

static void cave_gates_of(const cave_lattice_t *lattice, float32_t z, cave_gates_t *gates) {
    const float32_t floor_gate = cave_clamp01((z - (float32_t)TERRAIN_CAVE_FLOOR_TOP) / CAVE_FLOOR_RAMP);
    const bool sealed = z <= (float32_t)TERRAIN_CAVE_SEALED_TOP;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        const float32_t cover = lattice->heights[i] - z;
        /* Below this much cover the 3D noise and the lattice could already have taken all the ground away. */
        const float32_t uncertain = lattice->softness[i] + (float32_t)TERRAIN_DENSITY_CELL_Z;
        const float32_t certain = cover - uncertain;
        gates->cheese[i] = floor_gate * cave_clamp01((certain - (float32_t)TERRAIN_CAVE_ROOF_CHEESE) / CAVE_ROOF_RAMP);
        gates->noodle[i] = floor_gate * cave_clamp01((certain - (float32_t)TERRAIN_CAVE_ROOF_NOODLE) / CAVE_ROOF_RAMP);
        gates->surface[i] = cave_clamp01(1.0f32 - cover / CAVE_ENTRANCE_DEPTH);
        if (cover < -(uncertain + (float32_t)TERRAIN_CAVE_CELL)) {
            gates->spaghetti[i] = 0.0f32; /* all air here, and all the way to the next level */
        } else if (sealed) {
            gates->spaghetti[i] =
                floor_gate * cave_clamp01((certain - (float32_t)TERRAIN_CAVE_ROOF_SEA) / CAVE_ROOF_RAMP);
        } else {
            gates->spaghetti[i] = floor_gate;
        }
    }
}

static void cave_plane_cheese(const terrain_cave_t *cave, const cave_lattice_t *lattice, const float32_t *gates,
                              cave_plane_t *plane) {
    uint8_t which[CAVE_TILE_LATTICE];
    float32_t raw[CAVE_TILE_LATTICE];
    float32_t raw_region[CAVE_TILE_LATTICE];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        plane->cheese[i] = -1.0f32;
        if (gates[i] > 0.0f32) {
            which[count] = (uint8_t)i;
            count++;
        }
    }
    plane->any_cheese = 0u;
    if (count == 0u) {
        return;
    }
    cave_sample(cave->borrowed_cheese, lattice, plane->z, which, count, raw);
    cave_sample(cave->borrowed_cheese_region, lattice, plane->z, which, count, raw_region);
    for (uint32_t r = 0u; r < count; r++) {
        const uint32_t i = which[r];
        const float32_t wanted = cave_spread(raw[r], CAVE_LIMIT_CHEESE) +
                                 CAVE_CHEESE_REGION_SHARE * cave_spread(raw_region[r], CAVE_LIMIT_ONE_OCTAVE) -
                                 CAVE_CHEESE_THRESHOLD;
        plane->cheese[i] = gates[i] * wanted - (1.0f32 - gates[i]);
        plane->any_cheese = plane->cheese[i] > -CAVE_SURE_MARGIN ? 1u : plane->any_cheese;
    }
}

static void cave_plane_spaghetti(const terrain_cave_t *cave, const cave_lattice_t *lattice, const cave_gates_t *gates,
                                 cave_plane_t *plane) {
    uint8_t which[CAVE_TILE_LATTICE];
    float32_t raw_gate[CAVE_TILE_LATTICE];
    float32_t raw_width[CAVE_TILE_LATTICE];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        plane->spaghetti_limit[i] = 0.0f32;
        if (gates->spaghetti[i] > 0.0f32) {
            which[count] = (uint8_t)i;
            count++;
        }
    }
    plane->any_spaghetti = 0u;
    if (count == 0u) {
        return;
    }
    cave_sample(cave->borrowed_spaghetti_gate, lattice, plane->z, which, count, raw_gate);
    cave_sample(cave->borrowed_spaghetti_width, lattice, plane->z, which, count, raw_width);
    for (uint32_t r = 0u; r < count; r++) {
        const uint32_t i = which[r];
        const float32_t needed = CAVE_SPAGHETTI_GATE + CAVE_SPAGHETTI_GATE_ENTRANCE * gates->surface[i];
        const float32_t open =
            cave_clamp01((cave_spread(raw_gate[r], CAVE_LIMIT_ONE_OCTAVE) - needed) / CAVE_SPAGHETTI_GATE_RAMP);
        const float32_t share = 0.5f32 + 0.5f32 * cave_spread(raw_width[r], CAVE_LIMIT_ONE_OCTAVE);
        const float32_t width =
            CAVE_SPAGHETTI_WIDTH_MIN + (CAVE_SPAGHETTI_WIDTH_MAX - CAVE_SPAGHETTI_WIDTH_MIN) * share;
        plane->spaghetti_limit[i] = width * open * gates->spaghetti[i];
        plane->any_spaghetti = plane->spaghetti_limit[i] > 0.0f32 ? 1u : plane->any_spaghetti;
    }
}

static void cave_plane_noodle(const terrain_cave_t *cave, const cave_lattice_t *lattice, const float32_t *gates,
                              cave_plane_t *plane) {
    uint8_t which[CAVE_TILE_LATTICE];
    float32_t raw_gate[CAVE_TILE_LATTICE];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        plane->noodle_limit[i] = 0.0f32;
        if (gates[i] > 0.0f32) {
            which[count] = (uint8_t)i;
            count++;
        }
    }
    plane->any_noodle = 0u;
    if (count == 0u) {
        return;
    }
    cave_sample(cave->borrowed_noodle_gate, lattice, plane->z, which, count, raw_gate);
    for (uint32_t r = 0u; r < count; r++) {
        const uint32_t i = which[r];
        const float32_t open =
            cave_clamp01((cave_spread(raw_gate[r], CAVE_LIMIT_ONE_OCTAVE) - CAVE_NOODLE_GATE) / CAVE_NOODLE_GATE_RAMP);
        plane->noodle_limit[i] = CAVE_NOODLE_WIDTH * open * gates[i];
        plane->any_noodle = plane->noodle_limit[i] > 0.0f32 ? 1u : plane->any_noodle;
    }
}

static void cave_plane_fill(const terrain_cave_t *cave, const cave_lattice_t *lattice, int64_t level,
                            cave_plane_t *plane) {
    cave_gates_t gates;
    plane->z = (float32_t)((int64_t)MC_WORLD_Z_MIN + level * (int64_t)TERRAIN_CAVE_CELL);
    plane->has_spaghetti_tube = 0u;
    plane->has_noodle_tube = 0u;
    cave_gates_of(lattice, plane->z, &gates);
    cave_plane_cheese(cave, lattice, gates.cheese, plane);
    cave_plane_spaghetti(cave, lattice, &gates, plane);
    cave_plane_noodle(cave, lattice, gates.noodle, plane);
}

/* A tube's two fields, at every column of a level. Only done when a cell touching this level can have that tube in it, as most
 * levels have none, and the two cost the most. Whether it is done cannot change a voxel: nothing reads them otherwise. */
static void cave_tube_fill(const terrain_noise_field_t *field_a, const terrain_noise_field_t *field_b,
                           const cave_lattice_t *lattice, float32_t z, float32_t *a, float32_t *b) {
    uint8_t which[CAVE_TILE_LATTICE];
    for (uint32_t i = 0u; i < lattice->count; i++) {
        which[i] = (uint8_t)i;
    }
    cave_sample(field_a, lattice, z, which, lattice->count, a);
    cave_sample(field_b, lattice, z, which, lattice->count, b);
}

/* ---- A tile ------------------------------------------------------------------------------------------------------ */

/* Where a tile's output goes. Exactly one of the two is set. */
typedef struct cave_output_t {
    const terrain_density_box_t *borrowed_box; /* 0 */
    uint8_t *mask;                             /* 8 */
    uint16_t *blocks;                          /* 16 */
    uint64_t changed;                          /* 24 */
} cave_output_t;
static_assert(sizeof(cave_output_t) == 32);

/* The part of the box in one tile, both ends included, and the lattice columns round it. */
typedef struct cave_span_t {
    int64_t x_first; /* 0 */
    int64_t x_last;  /* 8 */
    int64_t y_first; /* 16 */
    int64_t y_last;  /* 24 */
    int64_t cell_x;  /* 32: the lattice column at index 0 */
    int64_t cell_y;  /* 40 */
    uint32_t side_x; /* 48: lattice columns on a side */
    uint32_t side_y; /* 52 */
} cave_span_t;
static_assert(sizeof(cave_span_t) == 56);

/* The four columns of one cell. */
typedef struct cave_corners_t {
    uint32_t i00; /* 0 */
    uint32_t i10; /* 4 */
    uint32_t i01; /* 8 */
    uint32_t i11; /* 12 */
} cave_corners_t;
static_assert(sizeof(cave_corners_t) == 16);

static float32_t cave_lerp(float32_t a, float32_t b, float32_t t) {
    return a + t * (b - a);
}

/* A voxel's value, from the 8 corners round it: first in z, then x, then y, always, so the bits cannot depend on the box. */
static float32_t cave_blend(const float32_t *below, const float32_t *above, const cave_corners_t *c, float32_t t_x,
                            float32_t t_y, float32_t t_z) {
    const float32_t v00 = cave_lerp(below[c->i00], above[c->i00], t_z);
    const float32_t v10 = cave_lerp(below[c->i10], above[c->i10], t_z);
    const float32_t v01 = cave_lerp(below[c->i01], above[c->i01], t_z);
    const float32_t v11 = cave_lerp(below[c->i11], above[c->i11], t_z);
    return cave_lerp(cave_lerp(v00, v10, t_x), cave_lerp(v01, v11, t_x), t_y);
}

static float32_t cave_largest(const float32_t *below, const float32_t *above, const cave_corners_t *c) {
    const float32_t values[8] = {below[c->i00], below[c->i10], below[c->i01], below[c->i11],
                                 above[c->i00], above[c->i10], above[c->i01], above[c->i11]};
    float32_t largest = values[0];
    for (uint32_t i = 1u; i < 8u; i++) {
        largest = values[i] > largest ? values[i] : largest;
    }
    return largest;
}

/* How close to 0 a blended value could get in this cell. */
static float32_t cave_closest_to_zero(const float32_t *below, const float32_t *above, const cave_corners_t *c) {
    const float32_t values[8] = {below[c->i00], below[c->i10], below[c->i01], below[c->i11],
                                 above[c->i00], above[c->i10], above[c->i01], above[c->i11]};
    float32_t smallest = values[0];
    float32_t largest = values[0];
    for (uint32_t i = 1u; i < 8u; i++) {
        smallest = values[i] < smallest ? values[i] : smallest;
        largest = values[i] > largest ? values[i] : largest;
    }
    if (smallest > 0.0f32) {
        return smallest;
    }
    return largest < 0.0f32 ? -largest : 0.0f32;
}

/* Whether a tube can pass through this cell at all. Interpolation stays between the corners, so a tube needs its two fields near
 * 0 somewhere and its limit above 0 somewhere. Nearly every cell fails, and that is what makes tubes cheap. */
static bool cave_tube_can_touch(const float32_t *limit_below, const float32_t *limit_above, const float32_t *a_below,
                                const float32_t *a_above, const float32_t *b_below, const float32_t *b_above,
                                const cave_corners_t *c) {
    const float32_t limit = cave_largest(limit_below, limit_above, c);
    if (limit <= 0.0f32) {
        return false;
    }
    const float32_t reach = limit + CAVE_SURE_MARGIN;
    return cave_closest_to_zero(a_below, a_above, c) < reach && cave_closest_to_zero(b_below, b_above, c) < reach;
}

static bool cave_tube_holds(const float32_t *limit_below, const float32_t *limit_above, const float32_t *a_below,
                            const float32_t *a_above, const float32_t *b_below, const float32_t *b_above,
                            const cave_corners_t *c, float32_t t_x, float32_t t_y, float32_t t_z) {
    const float32_t limit = cave_blend(limit_below, limit_above, c, t_x, t_y, t_z);
    if (limit <= 0.0f32) {
        return false;
    }
    const float32_t a = cave_blend(a_below, a_above, c, t_x, t_y, t_z);
    const float32_t b = cave_blend(b_below, b_above, c, t_x, t_y, t_z);
    return a * a + b * b < limit * limit;
}

/* Every voxel of the box in one cell, given which kinds could be in it. */
static void cave_cell_write(cave_output_t *output, const cave_span_t *span, const cave_plane_t *below,
                            const cave_plane_t *above, const cave_corners_t *c, uint8_t kinds, int64_t cell_x,
                            int64_t cell_y, int64_t level_z, int64_t z_first, int64_t z_last) {
    const terrain_density_box_t *const box = output->borrowed_box;
    const float32_t per_block = 1.0f32 / (float32_t)TERRAIN_CAVE_CELL;
    const int64_t x_cell_start = cell_x * (int64_t)TERRAIN_CAVE_CELL;
    const int64_t y_cell_start = cell_y * (int64_t)TERRAIN_CAVE_CELL;
    const int64_t x_from = x_cell_start > span->x_first ? x_cell_start : span->x_first;
    const int64_t y_from = y_cell_start > span->y_first ? y_cell_start : span->y_first;
    const int64_t x_end = x_cell_start + (int64_t)TERRAIN_CAVE_CELL - 1;
    const int64_t y_end = y_cell_start + (int64_t)TERRAIN_CAVE_CELL - 1;
    const int64_t x_to = x_end < span->x_last ? x_end : span->x_last;
    const int64_t y_to = y_end < span->y_last ? y_end : span->y_last;
    const int64_t z_end = level_z + (int64_t)TERRAIN_CAVE_CELL - 1;
    const int64_t z_from = level_z > z_first ? level_z : z_first;
    const int64_t z_to = z_end < z_last ? z_end : z_last;
    for (int64_t z = z_from; z <= z_to; z++) {
        const float32_t t_z = (float32_t)(z - level_z) * per_block;
        const uint64_t z_at = (uint64_t)(z - box->origin_z) * box->count_y;
        for (int64_t y = y_from; y <= y_to; y++) {
            const float32_t t_y = (float32_t)(y - y_cell_start) * per_block;
            const uint64_t row_at = (z_at + (uint64_t)(y - box->origin_y)) * box->count_x;
            for (int64_t x = x_from; x <= x_to; x++) {
                const float32_t t_x = (float32_t)(x - x_cell_start) * per_block;
                uint8_t bits = 0u;
                if ((kinds & TERRAIN_CAVE_CHEESE) != 0u &&
                    cave_blend(below->cheese, above->cheese, c, t_x, t_y, t_z) > 0.0f32) {
                    bits |= TERRAIN_CAVE_CHEESE;
                }
                if ((kinds & TERRAIN_CAVE_SPAGHETTI) != 0u &&
                    cave_tube_holds(below->spaghetti_limit, above->spaghetti_limit, below->spaghetti_a,
                                    above->spaghetti_a, below->spaghetti_b, above->spaghetti_b, c, t_x, t_y, t_z)) {
                    bits |= TERRAIN_CAVE_SPAGHETTI;
                }
                if ((kinds & TERRAIN_CAVE_NOODLE) != 0u &&
                    cave_tube_holds(below->noodle_limit, above->noodle_limit, below->noodle_a, above->noodle_a,
                                    below->noodle_b, above->noodle_b, c, t_x, t_y, t_z)) {
                    bits |= TERRAIN_CAVE_NOODLE;
                }
                if (bits == 0u) {
                    continue;
                }
                const uint64_t at = row_at + (uint64_t)(x - box->origin_x);
                if (output->mask != nullptr) {
                    output->mask[at] = bits;
                } else {
                    const uint16_t block = output->blocks[at];
                    if (block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA &&
                        block != BLOCK_ID_BEDROCK) {
                        output->blocks[at] = BLOCK_ID_AIR;
                        output->changed++;
                    }
                }
            }
        }
    }
}

/* The cells between two levels. */
static void cave_level_write(cave_output_t *output, const cave_span_t *span, const cave_plane_t *below,
                             const cave_plane_t *above, int64_t level_z, int64_t z_first, int64_t z_last) {
    const bool spaghetti = (below->any_spaghetti | above->any_spaghetti) != 0u;
    const bool noodle = (below->any_noodle | above->any_noodle) != 0u;
    const bool cheese = (below->any_cheese | above->any_cheese) != 0u;
    if (!spaghetti && !noodle && !cheese) {
        return;
    }
    for (uint32_t iy = 0u; iy + 1u < span->side_y; iy++) {
        const int64_t cell_y = span->cell_y + (int64_t)iy;
        for (uint32_t ix = 0u; ix + 1u < span->side_x; ix++) {
            const uint32_t at = ix + span->side_x * iy;
            const cave_corners_t c = {at, at + 1u, at + span->side_x, at + span->side_x + 1u};
            uint8_t kinds = 0u;
            if (cheese && cave_largest(below->cheese, above->cheese, &c) > -CAVE_SURE_MARGIN) {
                kinds |= TERRAIN_CAVE_CHEESE;
            }
            if (spaghetti && cave_tube_can_touch(below->spaghetti_limit, above->spaghetti_limit, below->spaghetti_a,
                                                 above->spaghetti_a, below->spaghetti_b, above->spaghetti_b, &c)) {
                kinds |= TERRAIN_CAVE_SPAGHETTI;
            }
            if (noodle && cave_tube_can_touch(below->noodle_limit, above->noodle_limit, below->noodle_a,
                                              above->noodle_a, below->noodle_b, above->noodle_b, &c)) {
                kinds |= TERRAIN_CAVE_NOODLE;
            }
            if (kinds != 0u) {
                cave_cell_write(output, span, below, above, &c, kinds, span->cell_x + (int64_t)ix, cell_y, level_z,
                                z_first, z_last);
            }
        }
    }
}

/* Makes sure both levels have a tube's fields if either level has any of that tube. */
static void cave_tubes_ensure(const terrain_cave_t *cave, const cave_lattice_t *lattice, cave_plane_t *below,
                              cave_plane_t *above) {
    if ((below->any_spaghetti | above->any_spaghetti) != 0u) {
        if (below->has_spaghetti_tube == 0u) {
            cave_tube_fill(cave->borrowed_spaghetti_a, cave->borrowed_spaghetti_b, lattice, below->z,
                           below->spaghetti_a, below->spaghetti_b);
            below->has_spaghetti_tube = 1u;
        }
        if (above->has_spaghetti_tube == 0u) {
            cave_tube_fill(cave->borrowed_spaghetti_a, cave->borrowed_spaghetti_b, lattice, above->z,
                           above->spaghetti_a, above->spaghetti_b);
            above->has_spaghetti_tube = 1u;
        }
    }
    if ((below->any_noodle | above->any_noodle) != 0u) {
        if (below->has_noodle_tube == 0u) {
            cave_tube_fill(cave->borrowed_noodle_a, cave->borrowed_noodle_b, lattice, below->z, below->noodle_a,
                           below->noodle_b);
            below->has_noodle_tube = 1u;
        }
        if (above->has_noodle_tube == 0u) {
            cave_tube_fill(cave->borrowed_noodle_a, cave->borrowed_noodle_b, lattice, above->z, above->noodle_a,
                           above->noodle_b);
            above->has_noodle_tube = 1u;
        }
    }
}

/* Every z of the box in one tile. Only the levels that can hold a cave are worked out: no noise above the highest ground the
 * tile's columns allow, or under the cave floor. */
static void cave_tile(const terrain_cave_t *cave, cave_output_t *output, const cave_span_t *span) {
    const terrain_density_box_t *const box = output->borrowed_box;
    cave_lattice_t lattice;
    cave_plane_t plane_a;
    cave_plane_t plane_b;
    lattice.count = span->side_x * span->side_y;
    const terrain_climate_area_t area = {
        .origin_x = (int32_t)(span->cell_x * (int64_t)TERRAIN_CAVE_CELL),
        .origin_y = (int32_t)(span->cell_y * (int64_t)TERRAIN_CAVE_CELL),
        .step_x = (int32_t)TERRAIN_CAVE_CELL,
        .step_y = (int32_t)TERRAIN_CAVE_CELL,
        .count_x = span->side_x,
        .count_y = span->side_y,
    };
    terrain_density_profile_area(cave->borrowed_density, &area, lattice.heights, lattice.softness);
    float32_t top = (float32_t)INT32_MIN;
    for (uint32_t iy = 0u; iy < span->side_y; iy++) {
        for (uint32_t ix = 0u; ix < span->side_x; ix++) {
            const uint32_t i = ix + span->side_x * iy;
            lattice.xs[i] = (float64_t)((span->cell_x + (int64_t)ix) * (int64_t)TERRAIN_CAVE_CELL);
            lattice.ys[i] = (float64_t)((span->cell_y + (int64_t)iy) * (int64_t)TERRAIN_CAVE_CELL);
            const float32_t reach = lattice.heights[i] + lattice.softness[i] + (float32_t)TERRAIN_DENSITY_CELL_Z +
                                    2.0f32 * (float32_t)TERRAIN_CAVE_CELL;
            top = reach > top ? reach : top;
        }
    }

    const int64_t z_first = box->origin_z;
    const int64_t z_last = z_first + (int64_t)box->count_z - 1;
    const int64_t low = z_first > (int64_t)TERRAIN_CAVE_FLOOR_TOP ? z_first : (int64_t)TERRAIN_CAVE_FLOOR_TOP;
    const int64_t high = z_last < (int64_t)top ? z_last : (int64_t)top;
    if (low > high) {
        return;
    }
    const int64_t level_first = (low - (int64_t)MC_WORLD_Z_MIN) >> CAVE_CELL_SHIFT;
    const int64_t level_last = (high - (int64_t)MC_WORLD_Z_MIN) >> CAVE_CELL_SHIFT;

    cave_plane_t *below = &plane_a;
    cave_plane_t *above = &plane_b;
    cave_plane_fill(cave, &lattice, level_first, below);
    for (int64_t level = level_first; level <= level_last; level++) {
        cave_plane_fill(cave, &lattice, level + 1, above);
        cave_tubes_ensure(cave, &lattice, below, above);
        cave_level_write(output, span, below, above, (int64_t)MC_WORLD_Z_MIN + level * (int64_t)TERRAIN_CAVE_CELL, low,
                         high);
        cave_plane_t *const swap = below;
        below = above;
        above = swap;
    }
}

/* The box, tile by tile. Tiles are lined up on the world only to keep the stack small: what a voxel gets does not depend on
 * which tile it was in, as the lattice is lined up on the world too. */
static void cave_box(const terrain_cave_t *cave, cave_output_t *output) {
    const terrain_density_box_t *const box = output->borrowed_box;
    if (box->count_x == 0u || box->count_y == 0u || box->count_z == 0u) {
        return;
    }
    const int64_t x_end = (int64_t)box->origin_x + (int64_t)box->count_x;
    const int64_t y_end = (int64_t)box->origin_y + (int64_t)box->count_y;
    const int64_t tile = 1ll << CAVE_TILE_SHIFT;
    for (int64_t y = box->origin_y; y < y_end;) {
        const int64_t y_next = ((y >> CAVE_TILE_SHIFT) + 1) * tile;
        const int64_t y_last = (y_next < y_end ? y_next : y_end) - 1;
        for (int64_t x = box->origin_x; x < x_end;) {
            const int64_t x_next = ((x >> CAVE_TILE_SHIFT) + 1) * tile;
            const int64_t x_last = (x_next < x_end ? x_next : x_end) - 1;
            const int64_t cell_x = x >> CAVE_CELL_SHIFT;
            const int64_t cell_y = y >> CAVE_CELL_SHIFT;
            const cave_span_t span = {
                .x_first = x,
                .x_last = x_last,
                .y_first = y,
                .y_last = y_last,
                .cell_x = cell_x,
                .cell_y = cell_y,
                .side_x = (uint32_t)((x_last >> CAVE_CELL_SHIFT) - cell_x) + 2u,
                .side_y = (uint32_t)((y_last >> CAVE_CELL_SHIFT) - cell_y) + 2u,
            };
            cave_tile(cave, output, &span);
            x = x_next;
        }
        y = y_next;
    }
}

void terrain_cave_mask_box(const terrain_cave_t *cave, const terrain_density_box_t *box, uint8_t *out) {
    memset(out, 0, terrain_density_box_voxels(box));
    cave_output_t output = {.borrowed_box = box, .mask = out};
    cave_box(cave, &output);
}

uint64_t terrain_cave_carve_box(const terrain_cave_t *cave, const terrain_density_box_t *box, uint16_t *blocks) {
    cave_output_t output = {.borrowed_box = box, .blocks = blocks};
    cave_box(cave, &output);
    return output.changed;
}

void terrain_cave_cut_with_mask(const terrain_density_box_t *box, uint8_t *mask, uint16_t *blocks) {
    const uint64_t voxels = terrain_density_box_voxels(box);
    for (uint64_t i = 0u; i < voxels; i++) {
        const uint16_t block = blocks[i];
        const bool cut = mask[i] != 0u && block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA &&
                         block != BLOCK_ID_BEDROCK;
        mask[i] = cut ? 1u : 0u;
        if (cut) {
            blocks[i] = BLOCK_ID_AIR;
        }
    }
}

uint8_t terrain_cave_at(const terrain_cave_t *cave, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = {
        .origin_x = x, .origin_y = y, .origin_z = z, .count_x = 1u, .count_y = 1u, .count_z = 1u};
    uint8_t kinds = 0u;
    terrain_cave_mask_box(cave, &box, &kinds);
    return kinds;
}
