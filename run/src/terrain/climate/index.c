/* See README.md. */
#include "terrain/climate/index.h"

#include <string.h>

enum : uint32_t {
    /* Columns done at once, and so the size of the arrays on the stack. A whole number of 8s saves the kernel its tail. */
    CLIMATE_BATCH_COLUMNS = 128u,
};

/* What each field looks like. The README says why these. Changing any number here changes every world. */
static const float32_t CLIMATE_WEIGHTS_CONTINENTALNESS[] = {0.5f32, 1.0f32, 2.0f32, 2.0f32, 2.0f32, 1.0f32, 1.0f32};
static const terrain_noise_shape_t CLIMATE_SHAPE_CONTINENTALNESS = {
    .frequency_x = 1.0f64 / 3072.0f64,
    .frequency_y = 1.0f64 / 3072.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = sizeof(CLIMATE_WEIGHTS_CONTINENTALNESS) / sizeof(CLIMATE_WEIGHTS_CONTINENTALNESS[0]),
    .borrowed_weights = CLIMATE_WEIGHTS_CONTINENTALNESS,
};
static const float32_t CLIMATE_WEIGHTS_EROSION[] = {1.0f32, 1.0f32, 2.0f32, 1.0f32, 1.0f32};
static const terrain_noise_shape_t CLIMATE_SHAPE_EROSION = {
    .frequency_x = 1.0f64 / 1536.0f64,
    .frequency_y = 1.0f64 / 1536.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = sizeof(CLIMATE_WEIGHTS_EROSION) / sizeof(CLIMATE_WEIGHTS_EROSION[0]),
    .borrowed_weights = CLIMATE_WEIGHTS_EROSION,
};
static const terrain_noise_shape_t CLIMATE_SHAPE_TEMPERATURE = {
    .frequency_x = 1.0f64 / 4096.0f64,
    .frequency_y = 1.0f64 / 4096.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t CLIMATE_SHAPE_HUMIDITY = {
    .frequency_x = 1.0f64 / 2048.0f64,
    .frequency_y = 1.0f64 / 2048.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.45f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t CLIMATE_SHAPE_WEIRDNESS = {
    .frequency_x = 1.0f64 / 512.0f64,
    .frequency_y = 1.0f64 / 512.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 4u,
};
static const terrain_noise_shape_t CLIMATE_SHAPE_WARP = {
    .frequency_x = 1.0f64 / 256.0f64,
    .frequency_y = 1.0f64 / 256.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 3u,
};

/* A raw value at this far from 0 becomes -1 or 1. Each is about twice what that field measures as its standard deviation:
 * the README has the numbers, and tests/run/terrain/climate/ holds the result to the spread the README promises. */
static const float64_t CLIMATE_LIMIT_CONTINENTALNESS = 0.259f64;
static const float64_t CLIMATE_LIMIT_EROSION = 0.31f64;
static const float64_t CLIMATE_LIMIT_TEMPERATURE = 0.35f64;
static const float64_t CLIMATE_LIMIT_HUMIDITY = 0.35f64;
static const float64_t CLIMATE_LIMIT_WEIRDNESS = 0.35f64;

/* Blocks a raw warp value of 1 moves a column. Raw values stay well inside 1, so a column moves a few tens of blocks. */
static const float64_t CLIMATE_WARP_BLOCKS = 96.0f64;

/* Spreads a raw value out over -1 to 1, so that a value says about how much of the world is below it. A cubic, and not an
 * error function from libm: this is the same bits on any machine. Slope 0 at both ends, so nothing shows where it stops.
 * Worked out in float64_t because that keeps the result from rounding past 1. */
static float32_t climate_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* Weirdness folded, so that -1 is the floor of a valley and 1 the top of a ridge. It crosses 1 at each |weirdness| of 2/3,
 * which is the point: a crease in this is what makes a ridge line. */
static float32_t climate_fold(float32_t weirdness) {
    const float32_t size = weirdness < 0.0f32 ? -weirdness : weirdness;
    const float32_t offset = 3.0f32 * size - 2.0f32;
    return 1.0f32 - (offset < 0.0f32 ? -offset : offset);
}

bool terrain_climate_init(terrain_climate_t **out, arena_t *arena, terrain_noise_bank_t *bank) {
    *out = nullptr;
    terrain_climate_t *const climate = arena_alloc(arena, sizeof(*climate), alignof(terrain_climate_t));
    memset(climate, 0, sizeof(*climate));
    /* One at a time, and all of them, so that the log names the field the bank refused. The order moves nothing: a field
     * comes from its name. */
    if (!terrain_noise_bank_add(bank, "climate/continentalness", &CLIMATE_SHAPE_CONTINENTALNESS,
                                &climate->borrowed_continentalness) ||
        !terrain_noise_bank_add(bank, "climate/erosion", &CLIMATE_SHAPE_EROSION, &climate->borrowed_erosion) ||
        !terrain_noise_bank_add(bank, "climate/temperature", &CLIMATE_SHAPE_TEMPERATURE,
                                &climate->borrowed_temperature) ||
        !terrain_noise_bank_add(bank, "climate/humidity", &CLIMATE_SHAPE_HUMIDITY, &climate->borrowed_humidity) ||
        !terrain_noise_bank_add(bank, "climate/weirdness", &CLIMATE_SHAPE_WEIRDNESS, &climate->borrowed_weirdness) ||
        !terrain_noise_bank_add(bank, "climate/warp_x", &CLIMATE_SHAPE_WARP, &climate->borrowed_warp_x) ||
        !terrain_noise_bank_add(bank, "climate/warp_y", &CLIMATE_SHAPE_WARP, &climate->borrowed_warp_y)) {
        return false;
    }
    *out = climate;
    return true;
}

void terrain_climate_deinit(terrain_climate_t *climate) {
    memset(climate, 0, sizeof(*climate));
}

uint64_t terrain_climate_area_columns(const terrain_climate_area_t *area) {
    return (uint64_t)area->count_x * area->count_y;
}

/* One field at the columns already warped, spread out into out. */
static void climate_fill(const terrain_noise_field_t *field, float64_t limit, const float64_t *xs, const float64_t *ys,
                         const float64_t *zs, uint32_t count, float32_t *out) {
    terrain_noise_sample_points(field, xs, ys, zs, count, out);
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = climate_spread(out[i], limit);
    }
}

/* Up to CLIMATE_BATCH_COLUMNS columns, written at index at of each array. */
static void climate_sample_batch(const terrain_climate_t *climate, const float64_t *xs, const float64_t *ys,
                                 uint32_t count, const terrain_climate_arrays_t *out, uint64_t at) {
    float64_t warped_x[CLIMATE_BATCH_COLUMNS];
    float64_t warped_y[CLIMATE_BATCH_COLUMNS];
    const float64_t zs[CLIMATE_BATCH_COLUMNS] = {0.0f64}; /* no field has a frequency on z, so any height would do */
    float32_t warp[CLIMATE_BATCH_COLUMNS];
    float32_t weirdness_held[CLIMATE_BATCH_COLUMNS];

    /* Both warps are read at the column itself, so that neither depends on the other. */
    terrain_noise_sample_points(climate->borrowed_warp_x, xs, ys, zs, count, warp);
    for (uint32_t i = 0u; i < count; i++) {
        warped_x[i] = xs[i] + (float64_t)warp[i] * CLIMATE_WARP_BLOCKS;
    }
    terrain_noise_sample_points(climate->borrowed_warp_y, xs, ys, zs, count, warp);
    for (uint32_t i = 0u; i < count; i++) {
        warped_y[i] = ys[i] + (float64_t)warp[i] * CLIMATE_WARP_BLOCKS;
    }

    if (out->continentalness != nullptr) {
        climate_fill(climate->borrowed_continentalness, CLIMATE_LIMIT_CONTINENTALNESS, warped_x, warped_y, zs, count,
                     out->continentalness + at);
    }
    if (out->erosion != nullptr) {
        climate_fill(climate->borrowed_erosion, CLIMATE_LIMIT_EROSION, warped_x, warped_y, zs, count,
                     out->erosion + at);
    }
    if (out->temperature != nullptr) {
        climate_fill(climate->borrowed_temperature, CLIMATE_LIMIT_TEMPERATURE, warped_x, warped_y, zs, count,
                     out->temperature + at);
    }
    if (out->humidity != nullptr) {
        climate_fill(climate->borrowed_humidity, CLIMATE_LIMIT_HUMIDITY, warped_x, warped_y, zs, count,
                     out->humidity + at);
    }
    if (out->weirdness != nullptr || out->ridges != nullptr) {
        float32_t *const weirdness = out->weirdness != nullptr ? out->weirdness + at : weirdness_held;
        climate_fill(climate->borrowed_weirdness, CLIMATE_LIMIT_WEIRDNESS, warped_x, warped_y, zs, count, weirdness);
        if (out->ridges != nullptr) {
            for (uint32_t i = 0u; i < count; i++) {
                out->ridges[at + i] = climate_fold(weirdness[i]);
            }
        }
    }
}

void terrain_climate_sample_points(const terrain_climate_t *climate, const float64_t *xs, const float64_t *ys,
                                   uint64_t count, const terrain_climate_arrays_t *out) {
    for (uint64_t done = 0u; done < count;) {
        const uint64_t left = count - done;
        const uint32_t batch = left < CLIMATE_BATCH_COLUMNS ? (uint32_t)left : CLIMATE_BATCH_COLUMNS;
        climate_sample_batch(climate, xs + done, ys + done, batch, out, done);
        done += batch;
    }
}

/* One axis of an area, as a block coordinate. In 64 bits, since origin + i * step can pass an int32_t, and a float64_t
 * holds the result exactly for any area that fits in memory. */
static float64_t climate_area_axis(int32_t origin, int32_t step, uint32_t index) {
    return (float64_t)((int64_t)origin + (int64_t)step * (int64_t)index);
}

void terrain_climate_sample_area(const terrain_climate_t *climate, const terrain_climate_area_t *area,
                                 const terrain_climate_arrays_t *out) {
    float64_t xs[CLIMATE_BATCH_COLUMNS];
    float64_t ys[CLIMATE_BATCH_COLUMNS];
    uint32_t held = 0u;
    uint64_t done = 0u;
    for (uint32_t iy = 0u; iy < area->count_y; iy++) {
        const float64_t y = climate_area_axis(area->origin_y, area->step_y, iy);
        for (uint32_t ix = 0u; ix < area->count_x; ix++) {
            xs[held] = climate_area_axis(area->origin_x, area->step_x, ix);
            ys[held] = y;
            held++;
            if (held == CLIMATE_BATCH_COLUMNS) {
                climate_sample_batch(climate, xs, ys, held, out, done);
                done += held;
                held = 0u;
            }
        }
    }
    if (held > 0u) {
        climate_sample_batch(climate, xs, ys, held, out, done);
    }
}

void terrain_climate_at(const terrain_climate_t *climate, int32_t x, int32_t y, terrain_climate_point_t *out) {
    const float64_t column_x = (float64_t)x;
    const float64_t column_y = (float64_t)y;
    const terrain_climate_arrays_t arrays = {
        .continentalness = &out->continentalness,
        .erosion = &out->erosion,
        .ridges = &out->ridges,
        .temperature = &out->temperature,
        .humidity = &out->humidity,
        .weirdness = &out->weirdness,
    };
    climate_sample_batch(climate, &column_x, &column_y, 1u, &arrays, 0u);
}
