/* See README.md. */
#include "terrain/fields/index.h"

#include <string.h>

enum : uint32_t {
    /* Columns done at once, and so the size of the arrays on the stack. A whole number of 8s saves the kernel its tail. */
    FIELDS_BATCH_COLUMNS = 128u,
};

/* What each field looks like. The README says why these. Changing any number here changes every world. */
static const float32_t FIELDS_WEIGHTS_CONTINENTALNESS[] = {0.3f32, 1.0f32, 2.0f32, 2.0f32, 2.0f32, 1.0f32, 1.0f32};
static const terrain_noise_shape_t FIELDS_SHAPE_CONTINENTALNESS = {
    .frequency_x = 1.0f64 / 3072.0f64,
    .frequency_y = 1.0f64 / 3072.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = sizeof(FIELDS_WEIGHTS_CONTINENTALNESS) / sizeof(FIELDS_WEIGHTS_CONTINENTALNESS[0]),
    .borrowed_weights = FIELDS_WEIGHTS_CONTINENTALNESS,
};
static const float32_t FIELDS_WEIGHTS_EROSION[] = {1.0f32, 1.0f32, 2.0f32, 1.0f32, 1.0f32};
static const terrain_noise_shape_t FIELDS_SHAPE_EROSION = {
    .frequency_x = 1.0f64 / 1536.0f64,
    .frequency_y = 1.0f64 / 1536.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = sizeof(FIELDS_WEIGHTS_EROSION) / sizeof(FIELDS_WEIGHTS_EROSION[0]),
    .borrowed_weights = FIELDS_WEIGHTS_EROSION,
};
static const terrain_noise_shape_t FIELDS_SHAPE_TEMPERATURE = {
    .frequency_x = 1.0f64 / 4096.0f64,
    .frequency_y = 1.0f64 / 4096.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t FIELDS_SHAPE_HUMIDITY = {
    .frequency_x = 1.0f64 / 2048.0f64,
    .frequency_y = 1.0f64 / 2048.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
/* Plan 006 phase 03: what picks among one climate family's biomes, in place of temperature and humidity, which no biome reads
 * since. The shape of humidity, seeded by its own name. */
static const terrain_noise_shape_t FIELDS_SHAPE_VARIETY = {
    .frequency_x = 1.0f64 / 2048.0f64,
    .frequency_y = 1.0f64 / 2048.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t FIELDS_SHAPE_WETNESS = {
    .frequency_x = 1.0f64 / 2048.0f64,
    .frequency_y = 1.0f64 / 2048.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t FIELDS_SHAPE_WEIRDNESS = {
    .frequency_x = 1.0f64 / 512.0f64,
    .frequency_y = 1.0f64 / 512.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t FIELDS_SHAPE_WARP = {
    .frequency_x = 1.0f64 / 256.0f64,
    .frequency_y = 1.0f64 / 256.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};

/* How many of continentalness's octaves make its broad form. Down to a cell of 768 blocks. */
enum : uint32_t {
    FIELDS_BROAD_OCTAVES = 3u,
};

/* A raw value at this far from 0 becomes -1 or 1. Each is about twice what that field measures as its standard deviation:
 * the README has the numbers, and tests/run/terrain/fields/ holds the result to the spread the README promises. */
static const float64_t FIELDS_LIMIT_CONTINENTALNESS = 0.259f64;
static const float64_t FIELDS_LIMIT_EROSION = 0.31f64;
static const float64_t FIELDS_LIMIT_TEMPERATURE = 0.35f64;
static const float64_t FIELDS_LIMIT_HUMIDITY = 0.35f64;
static const float64_t FIELDS_LIMIT_WEIRDNESS = 0.35f64;
static const float64_t FIELDS_LIMIT_VARIETY = 0.35f64;
static const float64_t FIELDS_LIMIT_WETNESS = 0.35f64;

/* Blocks a raw warp value of 1 moves a column. Raw values stay well inside 1, so a column moves a few tens of blocks. */
static const float64_t FIELDS_WARP_BLOCKS = 96.0f64;

/* Spreads a raw value out over -1 to 1, so that a value says about how much of the world is below it. A cubic, and not an
 * error function from libm: this is the same bits on any machine. Slope 0 at both ends, so nothing shows where it stops.
 * Worked out in float64_t because that keeps the result from rounding past 1. */
static float32_t fields_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* Weirdness folded, so that -1 is the floor of a valley and 1 the top of a ridge. It crosses 1 at each |weirdness| of 2/3,
 * which is the point: a crease in this is what makes a ridge line. */
static float32_t fields_fold(float32_t weirdness) {
    const float32_t size = weirdness < 0.0f32 ? -weirdness : weirdness;
    const float32_t offset = 3.0f32 * size - 2.0f32;
    return 1.0f32 - (offset < 0.0f32 ? -offset : offset);
}

bool terrain_fields_init(terrain_fields_t **out, arena_t *arena, terrain_noise_bank_t *bank) {
    *out = nullptr;
    terrain_fields_t *const fields = arena_alloc(arena, sizeof(*fields), alignof(terrain_fields_t));
    memset(fields, 0, sizeof(*fields));
    /* One at a time, and all of them, so that the log names the field the bank refused. The order moves nothing: a field
     * comes from its name. */
    if (!terrain_noise_bank_add(bank, "climate/continentalness", &FIELDS_SHAPE_CONTINENTALNESS,
                                &fields->borrowed_continentalness) ||
        !terrain_noise_bank_add(bank, "climate/erosion", &FIELDS_SHAPE_EROSION, &fields->borrowed_erosion) ||
        !terrain_noise_bank_add(bank, "climate/temperature", &FIELDS_SHAPE_TEMPERATURE,
                                &fields->borrowed_temperature) ||
        !terrain_noise_bank_add(bank, "climate/humidity", &FIELDS_SHAPE_HUMIDITY, &fields->borrowed_humidity) ||
        !terrain_noise_bank_add(bank, "climate/weirdness", &FIELDS_SHAPE_WEIRDNESS, &fields->borrowed_weirdness) ||
        !terrain_noise_bank_add(bank, "climate/warp_x", &FIELDS_SHAPE_WARP, &fields->borrowed_warp_x) ||
        !terrain_noise_bank_add(bank, "climate/warp_y", &FIELDS_SHAPE_WARP, &fields->borrowed_warp_y) ||
        !terrain_noise_bank_add(bank, "climate/variety", &FIELDS_SHAPE_VARIETY, &fields->borrowed_variety) ||
        !terrain_noise_bank_add(bank, "climate/wetness", &FIELDS_SHAPE_WETNESS, &fields->borrowed_wetness)) {
        return false;
    }
    *out = fields;
    return true;
}

void terrain_fields_deinit(terrain_fields_t *fields) {
    memset(fields, 0, sizeof(*fields));
}

uint64_t terrain_fields_area_columns(const terrain_fields_area_t *area) {
    return (uint64_t)area->count_x * area->count_y;
}

/* One field at the columns already warped, spread out into out. */
static void fields_fill(const terrain_noise_field_t *field, float64_t limit, const float64_t *xs, const float64_t *ys,
                        const float64_t *zs, uint32_t count, float32_t *out) {
    terrain_noise_sample_points(field, xs, ys, zs, count, out);
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = fields_spread(out[i], limit);
    }
}

/* Up to FIELDS_BATCH_COLUMNS columns, written at index at of each array. */
static void fields_sample_batch(const terrain_fields_t *fields, const float64_t *xs, const float64_t *ys,
                                uint32_t count, const terrain_fields_arrays_t *out, uint64_t at) {
    float64_t warped_x[FIELDS_BATCH_COLUMNS];
    float64_t warped_y[FIELDS_BATCH_COLUMNS];
    const float64_t zs[FIELDS_BATCH_COLUMNS] = {0.0f64}; /* no field has a frequency on z, so any height would do */
    float32_t warp[FIELDS_BATCH_COLUMNS];
    float32_t weirdness_held[FIELDS_BATCH_COLUMNS];

    /* Both warps are read at the column itself, so that neither depends on the other. */
    terrain_noise_sample_points(fields->borrowed_warp_x, xs, ys, zs, count, warp);
    for (uint32_t i = 0u; i < count; i++) {
        warped_x[i] = xs[i] + (float64_t)warp[i] * FIELDS_WARP_BLOCKS;
    }
    terrain_noise_sample_points(fields->borrowed_warp_y, xs, ys, zs, count, warp);
    for (uint32_t i = 0u; i < count; i++) {
        warped_y[i] = ys[i] + (float64_t)warp[i] * FIELDS_WARP_BLOCKS;
    }

    if (out->broad != nullptr) {
        float32_t continentalness_held[FIELDS_BATCH_COLUMNS];
        float32_t *const whole = out->continentalness != nullptr ? out->continentalness + at : continentalness_held;
        float32_t *const broad = out->broad + at;
        terrain_noise_sample_points_split(fields->borrowed_continentalness, warped_x, warped_y, zs, count,
                                          FIELDS_BROAD_OCTAVES, broad, whole);
        for (uint32_t i = 0u; i < count; i++) {
            whole[i] = fields_spread(whole[i], FIELDS_LIMIT_CONTINENTALNESS);
            broad[i] = fields_spread(broad[i], FIELDS_LIMIT_CONTINENTALNESS);
        }
    } else if (out->continentalness != nullptr) {
        fields_fill(fields->borrowed_continentalness, FIELDS_LIMIT_CONTINENTALNESS, warped_x, warped_y, zs, count,
                    out->continentalness + at);
    }
    if (out->erosion != nullptr) {
        fields_fill(fields->borrowed_erosion, FIELDS_LIMIT_EROSION, warped_x, warped_y, zs, count, out->erosion + at);
    }
    if (out->temperature != nullptr) {
        fields_fill(fields->borrowed_temperature, FIELDS_LIMIT_TEMPERATURE, warped_x, warped_y, zs, count,
                    out->temperature + at);
    }
    if (out->humidity != nullptr) {
        fields_fill(fields->borrowed_humidity, FIELDS_LIMIT_HUMIDITY, warped_x, warped_y, zs, count,
                    out->humidity + at);
    }
    if (out->variety != nullptr) {
        fields_fill(fields->borrowed_variety, FIELDS_LIMIT_VARIETY, warped_x, warped_y, zs, count, out->variety + at);
    }
    if (out->wetness != nullptr) {
        fields_fill(fields->borrowed_wetness, FIELDS_LIMIT_WETNESS, warped_x, warped_y, zs, count, out->wetness + at);
    }
    if (out->weirdness != nullptr || out->ridges != nullptr) {
        float32_t *const weirdness = out->weirdness != nullptr ? out->weirdness + at : weirdness_held;
        fields_fill(fields->borrowed_weirdness, FIELDS_LIMIT_WEIRDNESS, warped_x, warped_y, zs, count, weirdness);
        if (out->ridges != nullptr) {
            for (uint32_t i = 0u; i < count; i++) {
                out->ridges[at + i] = fields_fold(weirdness[i]);
            }
        }
    }
}

void terrain_fields_sample_points(const terrain_fields_t *fields, const float64_t *xs, const float64_t *ys,
                                  uint64_t count, const terrain_fields_arrays_t *out) {
    for (uint64_t done = 0u; done < count;) {
        const uint64_t left = count - done;
        const uint32_t batch = left < FIELDS_BATCH_COLUMNS ? (uint32_t)left : FIELDS_BATCH_COLUMNS;
        fields_sample_batch(fields, xs + done, ys + done, batch, out, done);
        done += batch;
    }
}

/* One axis of an area, as a block coordinate. In 64 bits, since origin + i * step can pass an int32_t, and a float64_t
 * holds the result exactly for any area that fits in memory. */
static float64_t fields_area_axis(int32_t origin, int32_t step, uint32_t index) {
    return (float64_t)((int64_t)origin + (int64_t)step * (int64_t)index);
}

void terrain_fields_sample_area(const terrain_fields_t *fields, const terrain_fields_area_t *area,
                                const terrain_fields_arrays_t *out) {
    float64_t xs[FIELDS_BATCH_COLUMNS];
    float64_t ys[FIELDS_BATCH_COLUMNS];
    uint32_t held = 0u;
    uint64_t done = 0u;
    for (uint32_t iy = 0u; iy < area->count_y; iy++) {
        const float64_t y = fields_area_axis(area->origin_y, area->step_y, iy);
        for (uint32_t ix = 0u; ix < area->count_x; ix++) {
            xs[held] = fields_area_axis(area->origin_x, area->step_x, ix);
            ys[held] = y;
            held++;
            if (held == FIELDS_BATCH_COLUMNS) {
                fields_sample_batch(fields, xs, ys, held, out, done);
                done += held;
                held = 0u;
            }
        }
    }
    if (held > 0u) {
        fields_sample_batch(fields, xs, ys, held, out, done);
    }
}

void terrain_fields_at(const terrain_fields_t *fields, int32_t x, int32_t y, terrain_fields_point_t *out) {
    const float64_t column_x = (float64_t)x;
    const float64_t column_y = (float64_t)y;
    const terrain_fields_arrays_t arrays = {
        .continentalness = &out->continentalness,
        .erosion = &out->erosion,
        .ridges = &out->ridges,
        .temperature = &out->temperature,
        .humidity = &out->humidity,
        .weirdness = &out->weirdness,
        .broad = &out->broad,
        .variety = &out->variety,
        .wetness = &out->wetness,
    };
    fields_sample_batch(fields, &column_x, &column_y, 1u, &arrays, 0u);
}
