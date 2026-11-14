/* See README.md. */
#include "terrain/density/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "terrain/landform/index.h"

#include <math.h>
#include <string.h>

enum : uint32_t {
    /* Columns profiled at once, and so the size of the arrays on the stack. A whole number of 8s saves the kernel its tail. */
    DENSITY_BATCH_COLUMNS = TERRAIN_LANDFORM_BATCH_COLUMNS,
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

/* Plan 007 phase 06: the land's rise inland is a fifth of what it was (3 to 22, was 3 to 100), and every knot but the ends
 * moves 0.12 towards the land. Continentalness has octaves down to 48 blocks, and 97 blocks of rise over it made every
 * lowland moderate ground (master section 4 wants 30 to 40% of land flat); the shift keeps the sea's share, which the
 * plateaus, now read from smooth broad continentalness, would otherwise have lifted out of the pockets they stand round. */
static const density_knot_t DENSITY_KNOTS_CONTINENTAL[] = {
    {-1.0f64, -110.0f64}, {-0.76f64, -90.0f64}, {-0.66f64, -64.0f64}, {-0.48f64, -36.0f64}, {-0.38f64, -10.0f64},
    {-0.33f64, 3.0f64},   {-0.22f64, 4.4f64},   {0.05f64, 7.6f64},    {0.55f64, 13.2f64},   {1.0f64, 22.4f64},
};
/* The soft shaping curve, m^1.6 at its knots: a range on rolling ground, gentle at its foot and only a little steeper at its
 * top. The knots at tenths beside master 3.6's table hold the spline's slope near the power curve's (README). */
static const density_knot_t DENSITY_KNOTS_SOFT[] = {
    {0.0f64, 0.0f64},   {0.1f64, 0.025f64}, {0.2f64, 0.076f64},  {0.3f64, 0.146f64},
    {0.4f64, 0.231f64}, {0.5f64, 0.33f64},  {0.6f64, 0.442f64},  {0.7f64, 0.565f64},
    {0.8f64, 0.7f64},   {0.9f64, 0.845f64}, {0.95f64, 0.921f64}, {1.0f64, 1.0f64},
};
/* The sharp one, m^3: an alpine range, a long gentle apron and a steep top. */
static const density_knot_t DENSITY_KNOTS_SHARP[] = {
    {0.0f64, 0.0f64},   {0.1f64, 0.001f64}, {0.2f64, 0.008f64},  {0.3f64, 0.027f64},
    {0.4f64, 0.064f64}, {0.5f64, 0.125f64}, {0.6f64, 0.216f64},  {0.7f64, 0.343f64},
    {0.8f64, 0.512f64}, {0.9f64, 0.729f64}, {0.95f64, 0.857f64}, {1.0f64, 1.0f64},
};
/* A koppie: steep sides and a flat top, like the Highveld's. Master 3.7. */
static const density_knot_t DENSITY_KNOTS_KOPPIE[] = {
    {0.0f64, 0.0f64}, {0.3f64, 0.15f64}, {0.5f64, 0.75f64}, {0.7f64, 0.95f64}, {1.0f64, 1.0f64},
};
/* How much range the land allows, from continentalness without its finest octaves. Those are what make a coast's bays and capes,
 * and a range taken down over them would stand in the sea as a wall. */
static const density_knot_t DENSITY_KNOTS_RANGE_LAND[] = {
    {-1.0f64, 0.0f64}, {-0.38f64, 0.0f64}, {-0.1f64, 0.5f64}, {0.2f64, 1.0f64}, {1.0f64, 1.0f64},
};
/* From how near a column is to a range's line, 1 on the line itself, to how much of the range is there. Wide and nearly
 * straight: plan 007 phase 04's slope shaping puts the concavity on it, so it is not baked in here twice. */
static const density_knot_t DENSITY_KNOTS_FLANK[] = {
    {0.0f64, 0.0f64},     {0.60f64, 0.0f64},    {0.70f64, 0.06f64}, {0.80f64, 0.20f64}, {0.88f64, 0.42f64},
    {0.9333f64, 0.64f64}, {0.9667f64, 0.82f64}, {0.99f64, 0.96f64}, {1.0f64, 1.0f64},
};
/* How tall a stretch of range stands, along its length: passes, and here and there the great heights. */
static const density_knot_t DENSITY_KNOTS_MASSIF[] = {
    {-1.0f64, 0.35f64}, {-0.2f64, 0.58f64}, {0.4f64, 0.82f64}, {0.75f64, 1.0f64}, {1.0f64, 1.0f64},
};
/* From how near a column is to one of a range's crests, 1 on the crest, to the share of the range's height it stands at. */
static const density_knot_t DENSITY_KNOTS_CREST[] = {
    {0.0f64, 0.5f64},
    {0.4f64, 0.66f64},
    {0.8f64, 0.94f64},
    {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_VALLEY[] = {
    {-1.0f64, 0.6f64},
    {-0.6f64, 0.85f64},
    {-0.2f64, 1.0f64},
    {1.0f64, 1.0f64},
};
/* How far worn ground's valley floors sink below what the land's height leaves them, as a share of DENSITY_WETLAND_BLOCKS. */
static const density_knot_t DENSITY_KNOTS_WETLAND[] = {
    {-1.0f64, 0.0f64},
    {0.35f64, 0.0f64},
    {0.6f64, 1.0f64},
    {1.0f64, 1.0f64},
};
/* Moved 0.12 with the coast (plan 007 phase 06), so a beach still has no hills or knobs. */
static const density_knot_t DENSITY_KNOTS_HILL_LAND[] = {
    {-1.0f64, 0.0f64},
    {-0.33f64, 0.0f64},
    {-0.08f64, 1.0f64},
    {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_HILL_SEA[] = {
    {-1.0f64, 1.0f64},
    {-0.46f64, 1.0f64},
    {-0.39f64, 0.0f64},
    {1.0f64, 0.0f64},
};
/* A dry basin's sinking, as a share of the most, against how far into the basin a column is: concave, a flat floor and steep
 * sides. Plan 007 phase 05. */
static const density_knot_t DENSITY_KNOTS_BASIN[] = {
    {0.0f64, 0.0f64}, {0.15f64, 0.35f64}, {0.35f64, 0.80f64}, {0.6f64, 0.97f64}, {1.0f64, 1.0f64},
};
static const density_knot_t DENSITY_KNOTS_HILL_RELIEF[] = {
    {-1.0f64, 1.0f64},
    {0.0f64, 0.5f64},
    {0.6f64, 0.12f64},
    {1.0f64, 0.0f64},
};

/* The height formula's sizes, in blocks. The limits in the header are their sum. A range rises to DENSITY_RANGE_TOP less the
 * plateau it stands on, so the two together never pass it. */
/* 1080 from plan 007 phase 06, which took the land's own rise from 100 to 22.4: the summits stay at 1100 to 1190 (master
 * section 4), and the sum stays under TERRAIN_DENSITY_HEIGHT_MAX. */
static const float64_t DENSITY_RANGE_TOP = 1080.0f64;
/* How much a crest's side valleys cut a range at its full height: half of what the crest spline leaves out. */
static const float64_t DENSITY_CREST_DEPTH = 0.5f64;
/* How much a plateau's face counts as rough ground, for softness, and how much a rugged range's height does. */
static const float64_t DENSITY_FACE_SOFTNESS = 0.5f64;
static const float64_t DENSITY_RUGGED_SOFTNESS = 0.3f64;
static const float64_t DENSITY_WETLAND_BLOCKS = 14.0f64;
/* The summit rock: DENSITY_PEAK_BLOCKS * w^3 * (1 - |peak|), w = (m - FROM) / (1 - FROM) above FROM, else none. */
static const float64_t DENSITY_PEAK_BLOCKS = 40.0f64;
static const float64_t DENSITY_PEAK_FROM = 0.6f64;
/* Knobs: where knobs passes DENSITY_KNOB_FROM, (MIN + EXTRA * size) * shape(t) blocks. */
static const float64_t DENSITY_KNOB_FROM = 0.7f64;
static const float64_t DENSITY_KNOB_BLOCKS_MIN = 20.0f64;
static const float64_t DENSITY_KNOB_BLOCKS_EXTRA = 70.0f64;
static const float64_t DENSITY_HILL_BLOCKS_MIN = 0.0f64;
static const float64_t DENSITY_HILL_BLOCKS_EXTRA = 10.0f64;
static const float64_t DENSITY_HILL_BLOCKS_SEA = 6.0f64;
static const float64_t DENSITY_SOFTNESS_MIN = 4.0f64;
static const float64_t DENSITY_SOFTNESS_EXTRA = 22.0f64;
/* Dry basins (plan 007 phase 05; README "Dry basins"). The ring field is B less DENSITY_BASIN_COAST_DEPTH where the land is
 * not inland (smoothstep of broad continentalness from INLAND_FROM to INLAND_TO); the basin field is that less
 * DENSITY_BASIN_PLATEAU_DEPTH times the plateau's share of DENSITY_BASIN_PLATEAU_SHARE (65 blocks), held to 1. A column
 * is dry where the basin field passes DENSITY_BASIN_FROM, and sinks by (base + DENSITY_BASIN_FLOOR) times the basin
 * spline of how far past it is, so no floor is below -DENSITY_BASIN_FLOOR. Where the ring field is at least FROM - RING
 * and the basin field at most FROM, the ground is raised to softness + DENSITY_BASIN_RIM_BLOCKS: the rim. Each edge of
 * that is blended over DENSITY_BASIN_BLEND of its field. Rivers fade out over the ring field from FROM - RIVERS_FROM to
 * FROM - RIVERS_TO, before the rim starts. */
static const float64_t DENSITY_BASIN_FROM = 0.86f64;
static const float64_t DENSITY_BASIN_RING = 0.08f64;
static const float64_t DENSITY_BASIN_BLEND = 0.02f64;
static const float64_t DENSITY_BASIN_RIVERS_FROM = 0.16f64;
static const float64_t DENSITY_BASIN_RIVERS_TO = 0.10f64;
static const float64_t DENSITY_BASIN_COAST_DEPTH = 0.4f64;
static const float64_t DENSITY_BASIN_INLAND_FROM = -0.1f64;
static const float64_t DENSITY_BASIN_INLAND_TO = 0.2f64;
static const float64_t DENSITY_BASIN_PLATEAU_DEPTH = 0.4f64;
static const float64_t DENSITY_BASIN_PLATEAU_SHARE = 0.1f64;
/* The ring field also loses DENSITY_BASIN_UPLIFT_DEPTH times smoothstep(U; UPLIFT_FROM, UPLIFT_TO): basins sink where the land
 * is least lifted, far from any plateau. It is what makes them few: B alone passes its threshold in a dozen or more places a
 * 64 km map, whatever the threshold, as its cell is 8192 (Issues found). */
static const float64_t DENSITY_BASIN_UPLIFT_DEPTH = 0.4f64;
static const float64_t DENSITY_BASIN_UPLIFT_FROM = -0.95f64;
static const float64_t DENSITY_BASIN_UPLIFT_TO = -0.85f64;
static const float64_t DENSITY_BASIN_FLOOR = 100.0f64;
/* A basin's floor is DENSITY_BASIN_FLOOR times the ruggedness G, but at least DENSITY_BASIN_FLOOR_LEAST of it: shallow pans
 * on rolling ground, deep troughs among alpine ranges (plan 007 phase 06; every floor was -99 or -100 before, as B is held
 * to 1 where basins sink). */
static const float64_t DENSITY_BASIN_FLOOR_LEAST = 0.4f64;
/* The inland gate reads broad continentalness at nodes this many blocks apart, blended between, not at the column: broad
 * continentalness's finest octave has a cell of 768, and read at the column it broke a basin into pieces round every sea
 * pocket and changed the ring field faster than the rim can follow. */
static const int64_t DENSITY_BASIN_INLAND_CELL = 2048;
static const float64_t DENSITY_BASIN_RIM_BLOCKS = 3.0f64;
/* How much of broad continentalness at the column the plateau reads; the rest is the inland gate's blend (plan 007 phase 06). */
static const float64_t DENSITY_PLATEAU_BROAD_SHARE = 0.25f64;

/* What each field looks like. */
static const terrain_noise_shape_t DENSITY_SHAPE_HILLS = {
    .frequency_x = 1.0f64 / 384.0f64,
    .frequency_y = 1.0f64 / 384.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};
/* Where it is 0 is a range's line. A line where a smooth field is 0 has no ends, and this field's size sets how far apart they lie. */
static const terrain_noise_shape_t DENSITY_SHAPE_RANGE = {
    .frequency_x = 1.0f64 / 12288.0f64,
    .frequency_y = 1.0f64 / 12288.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 3u,
};
static const terrain_noise_shape_t DENSITY_SHAPE_MASSIF = {
    .frequency_x = 1.0f64 / 4096.0f64,
    .frequency_y = 1.0f64 / 4096.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t DENSITY_SHAPE_CREST = {
    .frequency_x = 1.0f64 / 896.0f64,
    .frequency_y = 1.0f64 / 896.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
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
static const float64_t DENSITY_LIMIT_DETAIL = 0.35f64;
static const float64_t DENSITY_LIMIT_RANGE = 0.36f64;
static const float64_t DENSITY_LIMIT_MASSIF = 0.39f64;
static const float64_t DENSITY_LIMIT_CREST = 0.35f64;

/* Corners at least this far from 0 mean a whole cell has that sign, rounding and all. The largest density is about 370, at 4
 * blocks of softness, and a float32_t's rounding there is about 0.00003. */
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

void terrain_density_spline_build(terrain_density_spline_t *spline, const float64_t *xs, const float64_t *ys,
                                  uint32_t count) {
    density_knot_t knots[TERRAIN_DENSITY_SPLINE_KNOTS_MAX];
    ASSERT_ALWAYS(count >= 2u && count <= TERRAIN_DENSITY_SPLINE_KNOTS_MAX);
    for (uint32_t i = 0u; i < count; i++) {
        knots[i] = (density_knot_t){.x = xs[i], .y = ys[i]};
    }
    density_spline_build(spline, knots, count);
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

float64_t terrain_density_spline_slope_at(const terrain_density_spline_t *spline, float64_t x) {
    const uint32_t last = spline->count - 1u;
    if (x <= spline->x[0] || x >= spline->x[last]) {
        return 0.0f64;
    }
    uint32_t i = 0u;
    while (x >= spline->x[i + 1u]) {
        i++;
    }
    const float64_t width = spline->x[i + 1u] - spline->x[i];
    const float64_t t = (x - spline->x[i]) / width;
    const float64_t t2 = t * t;
    /* The derivative of terrain_density_spline_at's Hermite basis, term by term, over the piece's width. */
    return ((6.0f64 * t2 - 6.0f64 * t) * spline->y[i] + (3.0f64 * t2 - 4.0f64 * t + 1.0f64) * width * spline->slope[i] +
            (6.0f64 * t - 6.0f64 * t2) * spline->y[i + 1u] +
            (3.0f64 * t2 - 2.0f64 * t) * width * spline->slope[i + 1u]) /
           width;
}

static float64_t density_fold(float32_t value) {
    return 1.0f64 - fabs((float64_t)value);
}

/* How much of a range this column has, from 0 to 1, without the two fields that only matter where that is above 0. */
static float64_t density_range_reach(const terrain_density_t *density, float32_t broad, float32_t range) {
    return terrain_density_spline_at(&density->range_land, (float64_t)broad) *
           terrain_density_spline_at(&density->flank, density_fold(range));
}

/* How much of a range stands here, body, from 0 to 1. */
static float64_t density_body(const terrain_density_t *density, float32_t broad, float32_t range, float32_t massif) {
    return density_range_reach(density, broad, range) * terrain_density_spline_at(&density->massif, (float64_t)massif);
}

/* The range's envelope m, body less what a crest's side valleys cut, which cut deep only high up a range: their depth goes
 * with body, so the foothills stay smooth. */
static float64_t density_envelope(const terrain_density_t *density, float64_t body, float32_t crest) {
    return body * (1.0f64 - DENSITY_CREST_DEPTH * body *
                                (1.0f64 - terrain_density_spline_at(&density->crest, density_fold(crest))));
}

float64_t terrain_density_shape_at(const terrain_density_t *density, float64_t envelope, float64_t rugged) {
    return (1.0f64 - rugged) * terrain_density_spline_at(&density->soft, envelope) +
           rugged * terrain_density_spline_at(&density->sharp, envelope);
}

float64_t terrain_density_knob_shape_at(const terrain_density_t *density, float64_t t, float64_t rugged) {
    const float64_t below = 1.0f64 - t;
    return (1.0f64 - rugged) * (1.0f64 - below * below) + rugged * terrain_density_spline_at(&density->koppie, t);
}

/* 3t^2 - 2t^3 of t = (x - from) / (to - from), held to 0 and 1: exactly 0 and 1 at and past the ends. */
static float64_t density_smoothstep(float64_t x, float64_t from, float64_t to) {
    float64_t t = (x - from) / (to - from);
    t = t > 1.0f64 ? 1.0f64 : (t < 0.0f64 ? 0.0f64 : t);
    return t * t * (3.0f64 - 2.0f64 * t);
}

terrain_density_profile_t terrain_density_profile_from(const terrain_density_t *density,
                                                       const terrain_density_inputs_t *inputs) {
    const float64_t continentalness = (float64_t)inputs->continentalness;
    const float64_t erosion = (float64_t)inputs->erosion;
    const float64_t ridges = (float64_t)inputs->ridges;

    const float64_t valley = terrain_density_spline_at(&density->valley, ridges);
    float64_t base = terrain_density_spline_at(&density->continental, continentalness);
    if (base > 0.0f64) {
        /* What the valley spline leaves of the land, less a worn valley's sinking, so that its floor can hold water far from the sea. The
         * sinking goes with 1 - valley, so that it is gone by the time the ground is no valley, and the height has no step. */
        base = base * valley -
               DENSITY_WETLAND_BLOCKS * (1.0f64 - valley) * terrain_density_spline_at(&density->wetland, erosion);
    }
    const float64_t body = density_body(density, inputs->broad, inputs->range, inputs->massif);
    const float64_t envelope = density_envelope(density, body, inputs->crest);
    const float64_t lift = (float64_t)TERRAIN_LANDFORM_PLATEAU_BLOCKS * (float64_t)inputs->plateau;
    const float64_t rugged = (float64_t)inputs->rugged;
    /* Summit cliffs and aretes, only high up a range. */
    float64_t peak = 0.0f64;
    if (envelope > DENSITY_PEAK_FROM) {
        const float64_t w = (envelope - DENSITY_PEAK_FROM) / (1.0f64 - DENSITY_PEAK_FROM);
        peak = DENSITY_PEAK_BLOCKS * w * w * w * density_fold(inputs->peak);
    }
    const float64_t land = terrain_density_spline_at(&density->hill_land, continentalness);
    /* Knobs stand on any ground, low or lifted, but not on a range or in the sea. */
    float64_t knob = 0.0f64;
    if ((float64_t)inputs->knobs > DENSITY_KNOB_FROM) {
        const float64_t t = ((float64_t)inputs->knobs - DENSITY_KNOB_FROM) / (1.0f64 - DENSITY_KNOB_FROM);
        knob = (DENSITY_KNOB_BLOCKS_MIN + DENSITY_KNOB_BLOCKS_EXTRA * (float64_t)inputs->knob_size) *
               terrain_density_knob_shape_at(density, t, rugged) * (1.0f64 - body) * land;
    }
    /* A valley's floor sinks with its hills, so that a river can run on through the hills without a break. */
    const float64_t hill_blocks =
        land * valley *
            (DENSITY_HILL_BLOCKS_MIN + DENSITY_HILL_BLOCKS_EXTRA *
                                           terrain_density_spline_at(&density->hill_relief, erosion) *
                                           (0.5f64 + 0.5f64 * rugged)) +
        terrain_density_spline_at(&density->hill_sea, continentalness) * DENSITY_HILL_BLOCKS_SEA;

    /* A dry basin: inland, off the plateaus, where B is high. Its floor is flat, as the hills fade with its sinking. */
    const float64_t inland =
        density_smoothstep((float64_t)inputs->inland, DENSITY_BASIN_INLAND_FROM, DENSITY_BASIN_INLAND_TO);
    const float64_t ring =
        (float64_t)inputs->basin - DENSITY_BASIN_COAST_DEPTH * (1.0f64 - inland) -
        DENSITY_BASIN_UPLIFT_DEPTH *
            density_smoothstep((float64_t)inputs->uplift, DENSITY_BASIN_UPLIFT_FROM, DENSITY_BASIN_UPLIFT_TO);
    const float64_t plateau_share = (float64_t)inputs->plateau / DENSITY_BASIN_PLATEAU_SHARE;
    const float64_t field = ring - DENSITY_BASIN_PLATEAU_DEPTH * (plateau_share < 1.0f64 ? plateau_share : 1.0f64);
    float64_t sink = 0.0f64;
    if (field > DENSITY_BASIN_FROM) {
        sink = terrain_density_spline_at(&density->basin, (field - DENSITY_BASIN_FROM) / (1.0f64 - DENSITY_BASIN_FROM));
    }
    /* A sea pocket in a basin, its base below -100, is raised to the floor as the rest is lowered to it. */
    const float64_t floor_depth =
        DENSITY_BASIN_FLOOR * (rugged > DENSITY_BASIN_FLOOR_LEAST ? rugged : DENSITY_BASIN_FLOOR_LEAST);
    const float64_t depression = (base + floor_depth) * sink;

    /* A range stands on the plateau and rises at most to DENSITY_RANGE_TOP above the base, whatever the plateau. */
    float64_t height = base - depression + lift +
                       (DENSITY_RANGE_TOP - lift) * terrain_density_shape_at(density, envelope, rugged) + peak + knob +
                       hill_blocks * (float64_t)inputs->hills * (1.0f64 - sink);
    /* Faces and rugged high ground are rough for the 3D noise: its overhangs go to escarpments and ranges, not plains. */
    float64_t rough = DENSITY_FACE_SOFTNESS * (float64_t)inputs->face + DENSITY_RUGGED_SOFTNESS * rugged * envelope;
    rough = rough > 1.0f64 ? 1.0f64 : rough;
    const float32_t softness =
        (float32_t)(DENSITY_SOFTNESS_MIN + DENSITY_SOFTNESS_EXTRA * (rough + body - rough * body));
    /* The rim: every column round a basin, and any in it that the plateau keeps from sinking, stands at least
     * DENSITY_BASIN_RIM_BLOCKS over its softness, so its ground is whole from the sea's level up. A comparison, no libm. */
    const float64_t into = density_smoothstep(ring, DENSITY_BASIN_FROM - DENSITY_BASIN_RING - DENSITY_BASIN_BLEND,
                                              DENSITY_BASIN_FROM - DENSITY_BASIN_RING);
    const float64_t out =
        1.0f64 - density_smoothstep(field, DENSITY_BASIN_FROM, DENSITY_BASIN_FROM + DENSITY_BASIN_BLEND);
    const float64_t rim = into < out ? into : out;
    const float64_t rim_top = (float64_t)softness + DENSITY_BASIN_RIM_BLOCKS;
    if (rim > 0.0f64 && height < rim_top) {
        height = rim >= 1.0f64 ? rim_top : height + rim * (rim_top - height);
    }
    /* No floor below -100, blended in as the rim blends out, so there is no step. */
    const float64_t floor_in = 1.0f64 - out;
    if (floor_in > 0.0f64 && height < -floor_depth) {
        height = floor_in >= 1.0f64 ? -floor_depth : height + floor_in * (-floor_depth - height);
    }
    return (terrain_density_profile_t){
        .height = (float32_t)height,
        .softness = softness,
        .lift = (float32_t)(lift - depression),
        .rivers = (float32_t)(1.0f64 - density_smoothstep(ring, DENSITY_BASIN_FROM - DENSITY_BASIN_RIVERS_FROM,
                                                          DENSITY_BASIN_FROM - DENSITY_BASIN_RIVERS_TO)),
        .ring = (float32_t)ring,
        .knob = (float32_t)knob,
        .dry = field > DENSITY_BASIN_FROM ? 1u : 0u,
    };
}

/* ---- Init -------------------------------------------------------------------------------------------------------- */

bool terrain_density_init(terrain_density_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                          const terrain_fields_t *fields, const struct terrain_landform_t *landform) {
    *out = nullptr;
    terrain_density_t *const density = arena_alloc(arena, sizeof(*density), alignof(terrain_density_t));
    memset(density, 0, sizeof(*density));
    density->borrowed_fields = fields;
    density->borrowed_landform = landform;
#define DENSITY_BUILD(field, knots) density_spline_build(&density->field, knots, sizeof(knots) / sizeof(knots[0]))
    DENSITY_BUILD(continental, DENSITY_KNOTS_CONTINENTAL);
    DENSITY_BUILD(soft, DENSITY_KNOTS_SOFT);
    DENSITY_BUILD(sharp, DENSITY_KNOTS_SHARP);
    DENSITY_BUILD(koppie, DENSITY_KNOTS_KOPPIE);
    DENSITY_BUILD(valley, DENSITY_KNOTS_VALLEY);
    DENSITY_BUILD(hill_land, DENSITY_KNOTS_HILL_LAND);
    DENSITY_BUILD(hill_sea, DENSITY_KNOTS_HILL_SEA);
    DENSITY_BUILD(hill_relief, DENSITY_KNOTS_HILL_RELIEF);
    DENSITY_BUILD(wetland, DENSITY_KNOTS_WETLAND);
    DENSITY_BUILD(flank, DENSITY_KNOTS_FLANK);
    DENSITY_BUILD(massif, DENSITY_KNOTS_MASSIF);
    DENSITY_BUILD(crest, DENSITY_KNOTS_CREST);
    DENSITY_BUILD(range_land, DENSITY_KNOTS_RANGE_LAND);
    DENSITY_BUILD(basin, DENSITY_KNOTS_BASIN);
#undef DENSITY_BUILD
    /* One at a time, and all of them, so that the log names the field the bank refused. The order moves nothing: a field
     * comes from its name. */
    if (!terrain_noise_bank_add(bank, "density/hills", &DENSITY_SHAPE_HILLS, &density->borrowed_hills) ||
        !terrain_noise_bank_add(bank, "density/detail", &DENSITY_SHAPE_DETAIL, &density->borrowed_detail) ||
        !terrain_noise_bank_add(bank, "density/range", &DENSITY_SHAPE_RANGE, &density->borrowed_range) ||
        !terrain_noise_bank_add(bank, "density/massif", &DENSITY_SHAPE_MASSIF, &density->borrowed_massif) ||
        !terrain_noise_bank_add(bank, "density/crest", &DENSITY_SHAPE_CREST, &density->borrowed_crest)) {
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

/* The plateau and the ruggedness at each column, from the landform's lattice and broad continentalness. In a function of
 * its own, so its arrays are not on the batch's frame. */
[[gnu::noinline]]
static void density_plateau_batch(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                  uint32_t count, const float32_t *broad, float32_t *plateau, float32_t *face,
                                  float32_t *rugged, float32_t *basin, float32_t *uplift_out) {
    float64_t uplift[DENSITY_BATCH_COLUMNS];
    float64_t rugged_wide[DENSITY_BATCH_COLUMNS];
    float64_t basin_wide[DENSITY_BATCH_COLUMNS];
    terrain_landform_lattice_points(density->borrowed_landform, xs, ys, count, uplift, rugged_wide, basin_wide);
    for (uint32_t i = 0u; i < count; i++) {
        const terrain_landform_shape_t shape =
            terrain_landform_shape_from(density->borrowed_landform, uplift[i], (float64_t)broad[i]);
        plateau[i] = (float32_t)shape.plateau;
        face[i] = (float32_t)shape.face;
        rugged[i] = (float32_t)rugged_wide[i];
        uplift_out[i] = (float32_t)uplift[i];
        basin[i] = (float32_t)basin_wide[i];
    }
}

/* Broad continentalness at count columns, blended from the nodes DENSITY_BASIN_INLAND_CELL apart round each, x first and then
 * y, in float64, as the landform's lattice blends: a pure function of the column. Each node is sampled once a call. */
[[gnu::noinline]]
static void density_inland_batch(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                 uint32_t count, float32_t *inland) {
    enum : uint32_t { NODES_MAX = 4u * DENSITY_BATCH_COLUMNS };
    int32_t node_i[NODES_MAX];
    int32_t node_j[NODES_MAX];
    float32_t node_broad[NODES_MAX];
    float64_t piece_x[DENSITY_BATCH_COLUMNS];
    float64_t piece_y[DENSITY_BATCH_COLUMNS];
    uint16_t corners[DENSITY_BATCH_COLUMNS][4];
    uint32_t nodes = 0u;
    const float64_t cell = (float64_t)DENSITY_BASIN_INLAND_CELL;
    for (uint32_t c = 0u; c < count; c++) {
        const int32_t i = (int32_t)floor(xs[c] / cell);
        const int32_t j = (int32_t)floor(ys[c] / cell);
        for (uint32_t k = 0u; k < 4u; k++) {
            const int32_t ni = i + (int32_t)(k & 1u);
            const int32_t nj = j + (int32_t)(k >> 1u);
            uint32_t n = 0u;
            while (n < nodes && (node_i[n] != ni || node_j[n] != nj)) {
                n++;
            }
            if (n == nodes) {
                node_i[n] = ni;
                node_j[n] = nj;
                nodes++;
            }
            corners[c][k] = (uint16_t)n;
        }
    }
    for (uint32_t done = 0u; done < nodes; done += DENSITY_BATCH_COLUMNS) {
        const uint32_t left = nodes - done;
        const uint32_t piece = left < DENSITY_BATCH_COLUMNS ? left : DENSITY_BATCH_COLUMNS;
        for (uint32_t n = 0u; n < piece; n++) {
            piece_x[n] = (float64_t)((int64_t)node_i[done + n] * DENSITY_BASIN_INLAND_CELL);
            piece_y[n] = (float64_t)((int64_t)node_j[done + n] * DENSITY_BASIN_INLAND_CELL);
        }
        const terrain_fields_arrays_t part = {.broad = node_broad + done};
        terrain_fields_sample_points(density->borrowed_fields, piece_x, piece_y, piece, &part);
    }
    for (uint32_t c = 0u; c < count; c++) {
        const float64_t t_x = xs[c] / cell - floor(xs[c] / cell);
        const float64_t t_y = ys[c] / cell - floor(ys[c] / cell);
        const float64_t v00 = (float64_t)node_broad[corners[c][0]], v10 = (float64_t)node_broad[corners[c][1]];
        const float64_t v01 = (float64_t)node_broad[corners[c][2]], v11 = (float64_t)node_broad[corners[c][3]];
        const float64_t south = v00 + t_x * (v10 - v00);
        const float64_t north = v01 + t_x * (v11 - v01);
        inland[c] = (float32_t)(south + t_y * (north - south));
    }
}

/* The knob and summit fields, each only at the columns where it can add anything: knobs on land, the summit rock high on a
 * range. Anywhere else any value gives the same ground, so the fields keep 0 there and cost nothing. */
[[gnu::noinline]]
static void density_fine_batch(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                               uint32_t count, const float32_t *continentalness, const float32_t *broad,
                               const float32_t *range, const float32_t *massif, const float32_t *crest,
                               float32_t *knobs, float32_t *knob_size, float32_t *peak) {
    float64_t xs_knob[DENSITY_BATCH_COLUMNS];
    float64_t ys_knob[DENSITY_BATCH_COLUMNS];
    float64_t xs_peak[DENSITY_BATCH_COLUMNS];
    float64_t ys_peak[DENSITY_BATCH_COLUMNS];
    float32_t got[DENSITY_BATCH_COLUMNS];
    float32_t got_size[DENSITY_BATCH_COLUMNS];
    uint8_t which_knob[DENSITY_BATCH_COLUMNS];
    uint8_t which_peak[DENSITY_BATCH_COLUMNS];
    uint32_t on_land = 0u;
    uint32_t high = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        knobs[i] = 0.0f32;
        knob_size[i] = 0.0f32;
        peak[i] = 0.0f32;
        if (terrain_density_spline_at(&density->hill_land, (float64_t)continentalness[i]) > 0.0f64) {
            xs_knob[on_land] = xs[i];
            ys_knob[on_land] = ys[i];
            which_knob[on_land] = (uint8_t)i;
            on_land++;
        }
        const float64_t body = density_body(density, broad[i], range[i], massif[i]);
        if (body > DENSITY_PEAK_FROM && density_envelope(density, body, crest[i]) > DENSITY_PEAK_FROM) {
            xs_peak[high] = xs[i];
            ys_peak[high] = ys[i];
            which_peak[high] = (uint8_t)i;
            high++;
        }
    }
    if (on_land > 0u) {
        terrain_landform_knobs_points(density->borrowed_landform, xs_knob, ys_knob, on_land, got, got_size);
        for (uint32_t n = 0u; n < on_land; n++) {
            knobs[which_knob[n]] = got[n];
            knob_size[which_knob[n]] = got_size[n];
        }
    }
    if (high > 0u) {
        terrain_landform_peak_points(density->borrowed_landform, xs_peak, ys_peak, high, got);
        for (uint32_t n = 0u; n < high; n++) {
            peak[which_peak[n]] = got[n];
        }
    }
}

/* Up to DENSITY_BATCH_COLUMNS columns, into out's arrays from done on. */
static void density_profile_batch(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                  uint32_t count, const terrain_density_profile_arrays_t *out, uint64_t done) {
    float32_t continentalness[DENSITY_BATCH_COLUMNS];
    float32_t erosion[DENSITY_BATCH_COLUMNS];
    float32_t ridges[DENSITY_BATCH_COLUMNS];
    float32_t broad[DENSITY_BATCH_COLUMNS];
    float32_t hills[DENSITY_BATCH_COLUMNS];
    float32_t range[DENSITY_BATCH_COLUMNS];
    float32_t massif[DENSITY_BATCH_COLUMNS];
    float32_t crest[DENSITY_BATCH_COLUMNS];
    float32_t plateau[DENSITY_BATCH_COLUMNS];
    float32_t face[DENSITY_BATCH_COLUMNS];
    float32_t rugged[DENSITY_BATCH_COLUMNS];
    float32_t basin[DENSITY_BATCH_COLUMNS];
    float32_t inland[DENSITY_BATCH_COLUMNS];
    float32_t uplift[DENSITY_BATCH_COLUMNS];
    float32_t knobs[DENSITY_BATCH_COLUMNS];
    float32_t knob_size[DENSITY_BATCH_COLUMNS];
    float32_t peak[DENSITY_BATCH_COLUMNS];
    float64_t xs_in_range[DENSITY_BATCH_COLUMNS];
    float64_t ys_in_range[DENSITY_BATCH_COLUMNS];
    uint8_t which[DENSITY_BATCH_COLUMNS];
    static_assert(DENSITY_BATCH_COLUMNS <= UINT8_MAX);
    const float64_t zs[DENSITY_BATCH_COLUMNS] = {0.0f64}; /* no 2D field has a frequency on z */

    const terrain_fields_arrays_t fields = {
        .continentalness = continentalness,
        .erosion = erosion,
        .ridges = ridges,
        .broad = broad,
    };
    terrain_fields_sample_points(density->borrowed_fields, xs, ys, count, &fields);
    terrain_noise_sample_points(density->borrowed_hills, xs, ys, zs, count, hills);
    terrain_noise_sample_points(density->borrowed_range, xs, ys, zs, count, range);
    /* The plateau reads broad continentalness mostly as the basins' inland gate does, blended from nodes
     * DENSITY_BASIN_INLAND_CELL apart, with DENSITY_PLATEAU_BROAD_SHARE of it at the column (plan 007 phase 06): read wholly
     * at the column, its 768-block octave made the plateau input change up to five times faster than the faces are drawn
     * for, and 1.8 to 3.6% of the land was sheer; read wholly blended, the coast gate lifted the sea out of its pockets. */
    density_inland_batch(density, xs, ys, count, inland);
    float32_t plateau_broad[DENSITY_BATCH_COLUMNS];
    for (uint32_t i = 0u; i < count; i++) {
        plateau_broad[i] = (float32_t)((float64_t)inland[i] +
                                       DENSITY_PLATEAU_BROAD_SHARE * ((float64_t)broad[i] - (float64_t)inland[i]));
    }
    density_plateau_batch(density, xs, ys, count, plateau_broad, plateau, face, rugged, basin, uplift);

    /* Massif and crest only multiply a range, so outside one they are not worth their octaves, and any value gives the same ground. */
    uint32_t reached = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        range[i] = density_spread(range[i], DENSITY_LIMIT_RANGE);
        massif[i] = 0.0f32;
        crest[i] = 0.0f32;
        if (density_range_reach(density, broad[i], range[i]) > 0.0f64) {
            xs_in_range[reached] = xs[i];
            ys_in_range[reached] = ys[i];
            which[reached] = (uint8_t)i;
            reached++;
        }
    }
    if (reached > 0u) {
        float32_t raw[DENSITY_BATCH_COLUMNS];
        terrain_noise_sample_points(density->borrowed_massif, xs_in_range, ys_in_range, zs, reached, raw);
        for (uint32_t r = 0u; r < reached; r++) {
            massif[which[r]] = density_spread(raw[r], DENSITY_LIMIT_MASSIF);
        }
        terrain_noise_sample_points(density->borrowed_crest, xs_in_range, ys_in_range, zs, reached, raw);
        for (uint32_t r = 0u; r < reached; r++) {
            crest[which[r]] = density_spread(raw[r], DENSITY_LIMIT_CREST);
        }
    }
    density_fine_batch(density, xs, ys, count, continentalness, broad, range, massif, crest, knobs, knob_size, peak);

    for (uint32_t i = 0u; i < count; i++) {
        const terrain_density_inputs_t inputs = {
            .continentalness = continentalness[i],
            .erosion = erosion[i],
            .ridges = ridges[i],
            .hills = density_spread(hills[i], DENSITY_LIMIT_HILLS),
            .peak = peak[i],
            .range = range[i],
            .massif = massif[i],
            .crest = crest[i],
            .broad = broad[i],
            .plateau = plateau[i],
            .face = face[i],
            .rugged = rugged[i],
            .knobs = knobs[i],
            .knob_size = knob_size[i],
            .basin = basin[i],
            .inland = inland[i],
            .uplift = uplift[i],
        };
        const terrain_density_profile_t profile = terrain_density_profile_from(density, &inputs);
        out->heights[done + i] = profile.height;
        out->softness[done + i] = profile.softness;
        if (out->lift != nullptr) {
            out->lift[done + i] = profile.lift;
        }
        if (out->rivers != nullptr) {
            out->rivers[done + i] = profile.rivers;
        }
        if (out->dry != nullptr) {
            out->dry[done + i] = profile.dry;
        }
        if (out->ring != nullptr) {
            out->ring[done + i] = profile.ring;
        }
        if (out->knob != nullptr) {
            out->knob[done + i] = profile.knob;
        }
    }
}

void terrain_density_profile_area_arrays(const terrain_density_t *density, const terrain_fields_area_t *area,
                                         const terrain_density_profile_arrays_t *out) {
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
                density_profile_batch(density, xs, ys, held, out, done);
                done += held;
                held = 0u;
            }
        }
    }
    if (held > 0u) {
        density_profile_batch(density, xs, ys, held, out, done);
    }
}

void terrain_density_profile_area_lift(const terrain_density_t *density, const terrain_fields_area_t *area,
                                       float32_t *heights, float32_t *softness, float32_t *lift) {
    const terrain_density_profile_arrays_t out = {.heights = heights, .softness = softness, .lift = lift};
    terrain_density_profile_area_arrays(density, area, &out);
}

void terrain_density_profile_area(const terrain_density_t *density, const terrain_fields_area_t *area,
                                  float32_t *heights, float32_t *softness) {
    terrain_density_profile_area_lift(density, area, heights, softness, nullptr);
}

void terrain_density_profile_points_arrays(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                           uint64_t count, const terrain_density_profile_arrays_t *out) {
    for (uint64_t done = 0u; done < count; done += DENSITY_BATCH_COLUMNS) {
        const uint64_t left = count - done;
        const uint32_t batch = left < DENSITY_BATCH_COLUMNS ? (uint32_t)left : DENSITY_BATCH_COLUMNS;
        density_profile_batch(density, xs + done, ys + done, batch, out, done);
    }
}

void terrain_density_profile_points_lift(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                         uint64_t count, float32_t *heights, float32_t *softness, float32_t *lift) {
    const terrain_density_profile_arrays_t out = {.heights = heights, .softness = softness, .lift = lift};
    terrain_density_profile_points_arrays(density, xs, ys, count, &out);
}

void terrain_density_profile_points(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                    uint64_t count, float32_t *heights, float32_t *softness) {
    terrain_density_profile_points_lift(density, xs, ys, count, heights, softness, nullptr);
}

/* ---- The lattice ------------------------------------------------------------------------------------------------- */

/* The columns of one tile that a box needs, and their ground. */
typedef struct density_lattice_t {
    float32_t heights[DENSITY_TILE_LATTICE];  /* 0 */
    float32_t softness[DENSITY_TILE_LATTICE]; /* 324 */
    float64_t xs[DENSITY_TILE_LATTICE];       /* 648 */
    float64_t ys[DENSITY_TILE_LATTICE];       /* 1296 */
    uint32_t count;                           /* 1944 */
    uint8_t dry[DENSITY_TILE_LATTICE];        /* 1948: the profile's */
    uint8_t any_dry;                          /* 2029: any of dry is 1 */
    uint8_t _pad0[2];                         /* 2030 */
} density_lattice_t;
static_assert(sizeof(density_lattice_t) == 2032);

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

/* dry: the voxel is in a dry basin, so what is open at or below the sea's level is air. */
static uint16_t density_block_of(int32_t z, bool solid, bool dry) {
    if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
        return BLOCK_ID_AIR;
    }
    if (z == MC_WORLD_Z_MIN) {
        return BLOCK_ID_BEDROCK;
    }
    if (solid) {
        return BLOCK_ID_STONE;
    }
    return z <= MC_WORLD_Z_SEA && !dry ? BLOCK_ID_WATER : BLOCK_ID_AIR;
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
    const terrain_density_profile_arrays_t out = {
        .heights = lattice->heights, .softness = lattice->softness, .dry = lattice->dry};
    density_profile_batch(density, lattice->xs, lattice->ys, lattice->count, &out, 0u);
    lattice->any_dry = 0u;
    for (uint32_t i = 0u; i < lattice->count; i++) {
        lattice->any_dry |= lattice->dry[i];
    }
}

/* Whether the voxel column (x, y) of a tile is dry: any of the four lattice columns round it is. The same rule as
 * hydrology's, so the two agree on every voxel. */
static bool density_voxel_dry(const density_lattice_t *lattice, const density_span_t *span, int64_t x, int64_t y) {
    if (lattice->any_dry == 0u) {
        return false;
    }
    const uint32_t at = (uint32_t)((x >> DENSITY_CELL_XY_SHIFT) - span->cell_x) +
                        span->side_x * (uint32_t)((y >> DENSITY_CELL_XY_SHIFT) - span->cell_y);
    return (lattice->dry[at] | lattice->dry[at + 1u] | lattice->dry[at + span->side_x] |
            lattice->dry[at + span->side_x + 1u]) != 0u;
}

/* One z's voxels of a tile, when the whole of it is sure of its sign. */
static void density_write_sure(const density_output_t *output, const density_lattice_t *lattice,
                               const density_span_t *span, int64_t z, bool solid) {
    const terrain_density_box_t *const box = output->borrowed_box;
    const uint16_t block = density_block_of((int32_t)z, solid, false);
    const bool mixed = block == BLOCK_ID_WATER && lattice->any_dry != 0u;
    const uint64_t z_at = (uint64_t)(z - box->origin_z) * box->count_y;
    for (int64_t y = span->y_first; y <= span->y_last; y++) {
        uint16_t *const row = output->blocks + (z_at + (uint64_t)(y - box->origin_y)) * box->count_x;
        for (int64_t x = span->x_first; x <= span->x_last; x++) {
            row[x - box->origin_x] = mixed && density_voxel_dry(lattice, span, x, y) ? BLOCK_ID_AIR : block;
        }
    }
}

/* One z's voxels of a tile, each from the four lattice columns round it, already interpolated to this z in plane. Always x
 * first, then y, whichever box asked, so the bits cannot depend on the box. */
static void density_write_voxels(const density_output_t *output, const density_lattice_t *lattice,
                                 const density_span_t *span, int64_t z, const float32_t *plane) {
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
                const bool solid = value > 0.0f32;
                const bool dry = !solid && z <= (int64_t)MC_WORLD_Z_SEA && density_voxel_dry(lattice, span, x, y);
                output->blocks[row_at + (uint64_t)(x - box->origin_x)] = density_block_of((int32_t)z, solid, dry);
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
                density_write_sure(output, &lattice, span, z, sure_solid);
                continue;
            }
            const float32_t t_z = (float32_t)(z - level_z) * per_block;
            for (uint32_t i = 0u; i < lattice.count; i++) {
                interpolated[i] = below[i] + t_z * (above[i] - below[i]);
            }
            density_write_voxels(output, &lattice, span, z, interpolated);
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
