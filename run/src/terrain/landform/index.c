/* See README.md. */
#include "terrain/landform/index.h"

#include "assert/index.h"

#include <math.h>
#include <string.h>

enum : uint32_t {
    /* Nodes one call can need: four for each column, if no two share any. */
    LANDFORM_NODES_MAX = 4u * TERRAIN_LANDFORM_BATCH_COLUMNS,
    /* Slots of the table that finds a node already asked for: a power of two, and twice the nodes, so a probe is short. */
    LANDFORM_SLOTS = 2u * LANDFORM_NODES_MAX,
    /* Nodes sampled at once, and so the arrays on the stack while sampling. */
    LANDFORM_PIECE = 128u,
};
static_assert((LANDFORM_SLOTS & (LANDFORM_SLOTS - 1u)) == 0u);
static_assert(LANDFORM_NODES_MAX < UINT16_MAX);

/* A knot: x, then y. The README says why these. Changing any number here changes every world. */
typedef struct landform_knot_t {
    float64_t x; /* 0 */
    float64_t y; /* 8 */
} landform_knot_t;
static_assert(sizeof(landform_knot_t) == 16);

/* Two tiers: a pediment, tier 1's face and rounded lip, its top dipping gently inland, tier 2's foot, face and lip, and its
 * level top. Master 3.4's table. */
static const landform_knot_t LANDFORM_KNOTS_PLATEAU[] = {
    {-1.0f64, 0.0f64},  {0.0f64, 0.0f64},   {0.10f64, 0.04f64}, {0.16f64, 0.14f64},
    {0.19f64, 0.40f64}, {0.21f64, 0.45f64}, {0.40f64, 0.48f64}, {0.48f64, 0.55f64},
    {0.53f64, 0.95f64}, {0.56f64, 1.00f64}, {1.0f64, 1.00f64},
};

/* The plateau input: L = U + LANDFORM_UPLIFT_OFFSET + LANDFORM_BROAD_WEIGHT * (b - LANDFORM_BROAD_MIDDLE), less the coast's
 * gate, so plateaus stand back from the coast. The offset holds tier 2 to about a tenth of the land: the spread uplift is
 * nearly even from -1 to 1, so without it more than a quarter of the land stood at 550 (Issues found). */
static const float64_t LANDFORM_UPLIFT_OFFSET = -0.3f64;
static const float64_t LANDFORM_BROAD_WEIGHT = 0.25f64;
static const float64_t LANDFORM_BROAD_MIDDLE = 0.1f64;
/* The coast's gate: L less LANDFORM_GATE_DEPTH * (1 - smoothstep(b; FROM, TO)). The coast lies about b = -0.41: the gate keeps
 * plateaus off the sea bed (a lifted sea pocket cut rivers off from the sea) and back from the coast, where rivers run. Wide,
 * as b is rough (its finest octave has a cell of 768): a narrow gate made L change five times faster than the faces are
 * drawn for, and stood walls in the sea (Issues found). */
static const float64_t LANDFORM_GATE_FROM = -0.7f64;
static const float64_t LANDFORM_GATE_TO = 0.2f64;
static const float64_t LANDFORM_GATE_DEPTH = 0.6f64;
/* The back edge's stretch: k = 1 - LANDFORM_STRETCH_DEPTH * smoothstep(b; FROM, TO). Below LANDFORM_STRETCH_PIVOT, L' = L +
 * (1 - k) * min(LANDFORM_STRETCH_PIVOT - L, LANDFORM_STRETCH_REACH): drawn 1/k times wider down through tier 2's rise, and
 * then only lifted, by at most (1 - k) * REACH, so far inland the low ground is not all raised to tier 1. */
static const float64_t LANDFORM_STRETCH_FROM = 0.2f64;
static const float64_t LANDFORM_STRETCH_TO = 0.5f64;
static const float64_t LANDFORM_STRETCH_DEPTH = 0.8f64;
static const float64_t LANDFORM_STRETCH_PIVOT = 0.53f64;
static const float64_t LANDFORM_STRETCH_REACH = 0.375f64;
/* The typical gradient of L, per block, from which face is worked out without a neighbour: master 3.4. */
static const float64_t LANDFORM_TYPICAL_GRADIENT = 4.0e-4f64;
/* Face is the slope over this: a slope of 2 is a face of 1. */
static const float64_t LANDFORM_FACE_SLOPE = 2.0f64;
/* How far the edge warp moves the point U is read at, in blocks, each way. */
static const float64_t LANDFORM_WARP_BLOCKS = 700.0f64;

/* Ruggedness: G = clamp(LANDFORM_RUGGED_MIDDLE + LANDFORM_RUGGED_HALF * spread(rugged), 0, 1). The README says why. */
static const float64_t LANDFORM_RUGGED_MIDDLE = 0.5f64;
static const float64_t LANDFORM_RUGGED_HALF = 0.5f64;

/* What each field looks like. */
static const terrain_noise_shape_t LANDFORM_SHAPE_UPLIFT = {
    .frequency_x = 1.0f64 / 24576.0f64,
    .frequency_y = 1.0f64 / 24576.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};
static const terrain_noise_shape_t LANDFORM_SHAPE_EDGE = {
    .frequency_x = 1.0f64 / 2048.0f64,
    .frequency_y = 1.0f64 / 2048.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};

/* Regions, some rolling and some alpine: master 3.6. */
static const terrain_noise_shape_t LANDFORM_SHAPE_RUGGED = {
    .frequency_x = 1.0f64 / 3072.0f64,
    .frequency_y = 1.0f64 / 3072.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
/* Knobs: a knob is the patch round a maximum of this, about 120 to 250 blocks across. Master 3.7. */
static const terrain_noise_shape_t LANDFORM_SHAPE_KNOBS = {
    .frequency_x = 1.0f64 / 320.0f64,
    .frequency_y = 1.0f64 / 320.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
/* The summit rock: ridged, so its creases are aretes. Master 3.6. */
static const terrain_noise_shape_t LANDFORM_SHAPE_PEAK = {
    .frequency_x = 1.0f64 / 48.0f64,
    .frequency_y = 1.0f64 / 48.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};

/* A raw value at this far from 0 becomes -1 or 1: about twice what each measures as its standard deviation
 * (tests/run/terrain/landform/ prints them). */
static const float64_t LANDFORM_LIMIT_UPLIFT = 0.35f64;
static const float64_t LANDFORM_LIMIT_EDGE = 0.39f64;
static const float64_t LANDFORM_LIMIT_RUGGED = 0.39f64;
static const float64_t LANDFORM_LIMIT_KNOBS = 0.39f64;
/* The knob field's second octave alone. */
static const float64_t LANDFORM_LIMIT_KNOB_SIZE = 0.17f64;
static const float64_t LANDFORM_LIMIT_PEAK = 0.39f64;

bool terrain_landform_init(terrain_landform_t **out, arena_t *arena, terrain_noise_bank_t *bank) {
    *out = nullptr;
    terrain_landform_t *const landform = arena_alloc(arena, sizeof(*landform), alignof(terrain_landform_t));
    memset(landform, 0, sizeof(*landform));
    enum : uint32_t { KNOTS = sizeof(LANDFORM_KNOTS_PLATEAU) / sizeof(LANDFORM_KNOTS_PLATEAU[0]) };
    float64_t xs[KNOTS];
    float64_t ys[KNOTS];
    for (uint32_t i = 0u; i < KNOTS; i++) {
        xs[i] = LANDFORM_KNOTS_PLATEAU[i].x;
        ys[i] = LANDFORM_KNOTS_PLATEAU[i].y;
    }
    terrain_density_spline_build(&landform->plateau, xs, ys, KNOTS);
    /* One at a time, and all of them, so that the log names the field the bank refused. */
    if (!terrain_noise_bank_add(bank, "landform/uplift", &LANDFORM_SHAPE_UPLIFT, &landform->borrowed_uplift) ||
        !terrain_noise_bank_add(bank, "landform/edge_x", &LANDFORM_SHAPE_EDGE, &landform->borrowed_edge_x) ||
        !terrain_noise_bank_add(bank, "landform/edge_y", &LANDFORM_SHAPE_EDGE, &landform->borrowed_edge_y) ||
        !terrain_noise_bank_add(bank, "landform/rugged", &LANDFORM_SHAPE_RUGGED, &landform->borrowed_rugged) ||
        !terrain_noise_bank_add(bank, "relief/knobs", &LANDFORM_SHAPE_KNOBS, &landform->borrowed_knobs) ||
        !terrain_noise_bank_add(bank, "landform/peak", &LANDFORM_SHAPE_PEAK, &landform->borrowed_peak)) {
        return false;
    }
    *out = landform;
    return true;
}

void terrain_landform_deinit(terrain_landform_t *landform) {
    memset(landform, 0, sizeof(*landform));
}

/* As the climate and the density spread their fields, and for the same reason. */
static float32_t landform_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* Up to LANDFORM_PIECE nodes. Kept out of its caller's frame, which holds a node set. */
[[gnu::noinline]]
static void landform_nodes_piece(const terrain_landform_t *landform, const int32_t *is, const int32_t *js,
                                 uint32_t count, float32_t *uplift, float32_t *rugged) {
    float64_t xs[LANDFORM_PIECE];
    float64_t ys[LANDFORM_PIECE];
    float32_t warp_x[LANDFORM_PIECE];
    float32_t warp_y[LANDFORM_PIECE];
    const float64_t zs[LANDFORM_PIECE] = {0.0f64}; /* no 2D field has a frequency on z */
    for (uint32_t n = 0u; n < count; n++) {
        xs[n] = (float64_t)((int64_t)is[n] * (int64_t)TERRAIN_LANDFORM_CELL);
        ys[n] = (float64_t)((int64_t)js[n] * (int64_t)TERRAIN_LANDFORM_CELL);
    }
    /* Ruggedness is read at the node itself: it has no edge to move. */
    terrain_noise_sample_points(landform->borrowed_rugged, xs, ys, zs, count, rugged);
    for (uint32_t n = 0u; n < count; n++) {
        float64_t g = LANDFORM_RUGGED_MIDDLE +
                      LANDFORM_RUGGED_HALF * (float64_t)landform_spread(rugged[n], LANDFORM_LIMIT_RUGGED);
        g = g > 1.0f64 ? 1.0f64 : (g < 0.0f64 ? 0.0f64 : g);
        rugged[n] = (float32_t)g;
    }
    terrain_noise_sample_points(landform->borrowed_edge_x, xs, ys, zs, count, warp_x);
    terrain_noise_sample_points(landform->borrowed_edge_y, xs, ys, zs, count, warp_y);
    for (uint32_t n = 0u; n < count; n++) {
        xs[n] += LANDFORM_WARP_BLOCKS * (float64_t)landform_spread(warp_x[n], LANDFORM_LIMIT_EDGE);
        ys[n] += LANDFORM_WARP_BLOCKS * (float64_t)landform_spread(warp_y[n], LANDFORM_LIMIT_EDGE);
    }
    terrain_noise_sample_points(landform->borrowed_uplift, xs, ys, zs, count, uplift);
    for (uint32_t n = 0u; n < count; n++) {
        uplift[n] = landform_spread(uplift[n], LANDFORM_LIMIT_UPLIFT);
    }
}

void terrain_landform_nodes(const terrain_landform_t *landform, const int32_t *is, const int32_t *js, uint32_t count,
                            float32_t *uplift, float32_t *rugged) {
    for (uint32_t done = 0u; done < count; done += LANDFORM_PIECE) {
        const uint32_t left = count - done;
        landform_nodes_piece(landform, is + done, js + done, left < LANDFORM_PIECE ? left : LANDFORM_PIECE,
                             uplift + done, rugged + done);
    }
}

/* The nodes one call needs, each once. */
typedef struct landform_node_set_t {
    int32_t is[LANDFORM_NODES_MAX];       /* 0 */
    int32_t js[LANDFORM_NODES_MAX];       /* 2048 */
    float32_t uplift[LANDFORM_NODES_MAX]; /* 4096 */
    float32_t rugged[LANDFORM_NODES_MAX]; /* 6144 */
    uint16_t slots[LANDFORM_SLOTS];       /* 8192: 0 is empty, else a node's index + 1 */
    uint32_t count;                       /* 10240 */
    uint8_t _pad0[4];                     /* 10244 */
} landform_node_set_t;
static_assert(sizeof(landform_node_set_t) == 10248);

/* The node (i, j)'s index in the set, adding it if it is new. */
static uint16_t landform_node_find(landform_node_set_t *set, int32_t i, int32_t j) {
    uint32_t slot = ((uint32_t)i * 73856093u ^ (uint32_t)j * 19349663u) & (LANDFORM_SLOTS - 1u);
    for (;;) {
        const uint16_t held = set->slots[slot];
        if (held == 0u) {
            ASSERT(set->count < LANDFORM_NODES_MAX);
            const uint16_t index = (uint16_t)set->count;
            set->is[index] = i;
            set->js[index] = j;
            set->count++;
            set->slots[slot] = (uint16_t)(index + 1u);
            return index;
        }
        if (set->is[held - 1u] == i && set->js[held - 1u] == j) {
            return (uint16_t)(held - 1u);
        }
        slot = (slot + 1u) & (LANDFORM_SLOTS - 1u);
    }
}

/* The blend of one value at the four corners: x first and then y, and a fraction of 0 takes its side as it is. */
static float64_t landform_blend(float64_t v00, float64_t v10, float64_t v01, float64_t v11, float64_t t_x,
                                float64_t t_y) {
    const float64_t south = t_x > 0.0f64 ? v00 + t_x * (v10 - v00) : v00;
    const float64_t north = t_x > 0.0f64 ? v01 + t_x * (v11 - v01) : v01;
    return t_y > 0.0f64 ? south + t_y * (north - south) : south;
}

[[gnu::noinline]]
void terrain_landform_lattice_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                     uint32_t count, float64_t *uplift, float64_t *rugged) {
    ASSERT(count <= TERRAIN_LANDFORM_BATCH_COLUMNS);
    landform_node_set_t set;
    memset(set.slots, 0, sizeof(set.slots));
    set.count = 0u;
    uint16_t corners[TERRAIN_LANDFORM_BATCH_COLUMNS][4];
    float64_t t_x[TERRAIN_LANDFORM_BATCH_COLUMNS];
    float64_t t_y[TERRAIN_LANDFORM_BATCH_COLUMNS];
    const float64_t per_cell = 1.0f64 / (float64_t)TERRAIN_LANDFORM_CELL;
    for (uint32_t c = 0u; c < count; c++) {
        const float64_t cell_x = floor(xs[c] * per_cell);
        const float64_t cell_y = floor(ys[c] * per_cell);
        /* A fraction of 0 reads only the node it sits on, and the blend below takes that node's value as it is. A rule of
         * the column alone, so a box that ends on a node line does not pay for the next row of nodes. */
        t_x[c] = (xs[c] - cell_x * (float64_t)TERRAIN_LANDFORM_CELL) * per_cell;
        t_y[c] = (ys[c] - cell_y * (float64_t)TERRAIN_LANDFORM_CELL) * per_cell;
        const int32_t i = (int32_t)cell_x;
        const int32_t j = (int32_t)cell_y;
        const int32_t east = t_x[c] > 0.0f64 ? i + 1 : i;
        const int32_t north = t_y[c] > 0.0f64 ? j + 1 : j;
        corners[c][0] = landform_node_find(&set, i, j);
        corners[c][1] = landform_node_find(&set, east, j);
        corners[c][2] = landform_node_find(&set, i, north);
        corners[c][3] = landform_node_find(&set, east, north);
    }
    terrain_landform_nodes(landform, set.is, set.js, set.count, set.uplift, set.rugged);
    for (uint32_t c = 0u; c < count; c++) {
        uplift[c] = landform_blend((float64_t)set.uplift[corners[c][0]], (float64_t)set.uplift[corners[c][1]],
                                   (float64_t)set.uplift[corners[c][2]], (float64_t)set.uplift[corners[c][3]], t_x[c],
                                   t_y[c]);
        if (rugged != nullptr) {
            rugged[c] = landform_blend((float64_t)set.rugged[corners[c][0]], (float64_t)set.rugged[corners[c][1]],
                                       (float64_t)set.rugged[corners[c][2]], (float64_t)set.rugged[corners[c][3]],
                                       t_x[c], t_y[c]);
        }
    }
}

void terrain_landform_uplift_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                    uint32_t count, float64_t *uplift) {
    terrain_landform_lattice_points(landform, xs, ys, count, uplift, nullptr);
}

void terrain_landform_knobs_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                   uint64_t count, float32_t *knobs, float32_t *size) {
    float32_t first[LANDFORM_PIECE];
    const float64_t zs[LANDFORM_PIECE] = {0.0f64};
    for (uint64_t done = 0u; done < count; done += LANDFORM_PIECE) {
        const uint64_t left = count - done;
        const uint32_t piece = left < LANDFORM_PIECE ? (uint32_t)left : LANDFORM_PIECE;
        terrain_noise_sample_points_split(landform->borrowed_knobs, xs + done, ys + done, zs, piece, 1u, first,
                                          knobs + done);
        for (uint32_t n = 0u; n < piece; n++) {
            /* The second octave alone is what is left when the first is taken away. */
            const float32_t second = knobs[done + n] - first[n];
            size[done + n] =
                (float32_t)(0.5f64 + 0.5f64 * (float64_t)landform_spread(second, LANDFORM_LIMIT_KNOB_SIZE));
            knobs[done + n] = landform_spread(knobs[done + n], LANDFORM_LIMIT_KNOBS);
        }
    }
}

void terrain_landform_peak_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                  uint64_t count, float32_t *peak) {
    const float64_t zs[LANDFORM_PIECE] = {0.0f64};
    for (uint64_t done = 0u; done < count; done += LANDFORM_PIECE) {
        const uint64_t left = count - done;
        const uint32_t piece = left < LANDFORM_PIECE ? (uint32_t)left : LANDFORM_PIECE;
        terrain_noise_sample_points(landform->borrowed_peak, xs + done, ys + done, zs, piece, peak + done);
        for (uint32_t n = 0u; n < piece; n++) {
            peak[done + n] = landform_spread(peak[done + n], LANDFORM_LIMIT_PEAK);
        }
    }
}

/* 3t^2 - 2t^3 of t = (x - from) / (to - from), held to 0 and 1. */
static float64_t landform_smoothstep(float64_t x, float64_t from, float64_t to) {
    float64_t t = (x - from) / (to - from);
    t = t > 1.0f64 ? 1.0f64 : (t < 0.0f64 ? 0.0f64 : t);
    return t * t * (3.0f64 - 2.0f64 * t);
}

terrain_landform_shape_t terrain_landform_shape_from(const terrain_landform_t *landform, float64_t uplift,
                                                     float64_t broad) {
    float64_t input = uplift + LANDFORM_UPLIFT_OFFSET + LANDFORM_BROAD_WEIGHT * (broad - LANDFORM_BROAD_MIDDLE) -
                      LANDFORM_GATE_DEPTH * (1.0f64 - landform_smoothstep(broad, LANDFORM_GATE_FROM, LANDFORM_GATE_TO));
    input = input > 1.0f64 ? 1.0f64 : (input < -1.0f64 ? -1.0f64 : input);
    /* An edge far inland, where broad continentalness is high, is where the uplift fades, not where the plateau meets the
     * coastal plain: drawn wider, so the ground comes down slowly. */
    const float64_t stretch =
        1.0f64 - LANDFORM_STRETCH_DEPTH * landform_smoothstep(broad, LANDFORM_STRETCH_FROM, LANDFORM_STRETCH_TO);
    if (input < LANDFORM_STRETCH_PIVOT) {
        const float64_t below = LANDFORM_STRETCH_PIVOT - input;
        input += (1.0f64 - stretch) * (below < LANDFORM_STRETCH_REACH ? below : LANDFORM_STRETCH_REACH);
    }
    const float64_t slope = terrain_density_spline_slope_at(&landform->plateau, input) * stretch *
                            LANDFORM_TYPICAL_GRADIENT * (float64_t)TERRAIN_LANDFORM_PLATEAU_BLOCKS /
                            LANDFORM_FACE_SLOPE;
    return (terrain_landform_shape_t){
        .plateau = terrain_density_spline_at(&landform->plateau, input),
        .face = slope > 1.0f64 ? 1.0f64 : slope,
        .input = input,
        .stretch = stretch,
    };
}
