/* See README.md. */
#include "terrain/noise/index.h"

#include "log/index.h"
#include "simd/noise/index.h"
#include "terrain/rng/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint32_t {
    /* Points done at once, and so the size of the arrays on the stack. The kernel takes 8 at a time, and a whole number of
     * 8s saves it from its slower tail. */
    NOISE_BATCH_POINTS = 256u,
    NOISE_AXES = 3u,
};

/* How far an octave's lattice is moved, in cells. Enough that one octave's whole-number points, where noise is 0, do not
 * line up with the next one's. Small, since each doubling of a coordinate costs a float32_t a bit of its fraction. */
static const float64_t NOISE_OFFSET_CELLS = 256.0f64;
/* A lattice finer than a block only aliases: it is a mistake in the shape, not a look anyone asked for. */
static const float64_t NOISE_FREQUENCY_MAX = 1.0f64;
static const float32_t NOISE_LACUNARITY_MAX = 16.0f32;

void terrain_noise_bank_init(terrain_noise_bank_t *bank, arena_t *arena, uint64_t seed, uint32_t capacity) {
    memset(bank, 0, sizeof(*bank));
    bank->borrowed_fields =
        arena_alloc_array(arena, capacity, sizeof(terrain_noise_field_t), alignof(terrain_noise_field_t));
    bank->seed = seed;
    bank->capacity = capacity;
}

void terrain_noise_bank_deinit(terrain_noise_bank_t *bank) {
    memset(bank, 0, sizeof(*bank));
}

/* Written so that NaN fails each test. */
static bool noise_shape_is_good(const terrain_noise_shape_t *shape) {
    if (shape->octaves == 0u || shape->octaves > TERRAIN_NOISE_OCTAVES_MAX) {
        return false;
    }
    if (!(shape->lacunarity >= 1.0f32 && shape->lacunarity <= NOISE_LACUNARITY_MAX)) {
        return false;
    }
    if (!(shape->persistence > 0.0f32 && shape->persistence <= 1.0f32)) {
        return false;
    }
    float64_t top_scale = 1.0f64;
    for (uint32_t i = 1u; i < shape->octaves; i++) {
        top_scale *= (float64_t)shape->lacunarity;
    }
    const float64_t frequencies[NOISE_AXES] = {shape->frequency_x, shape->frequency_y, shape->frequency_z};
    bool any_axis = false;
    for (uint32_t axis = 0u; axis < NOISE_AXES; axis++) {
        if (!(frequencies[axis] >= 0.0f64 && frequencies[axis] * top_scale <= NOISE_FREQUENCY_MAX)) {
            return false;
        }
        any_axis = any_axis || frequencies[axis] > 0.0f64;
    }
    return any_axis;
}

bool terrain_noise_bank_add(terrain_noise_bank_t *bank, const char *name, const terrain_noise_shape_t *shape,
                            const terrain_noise_field_t **out) {
    *out = nullptr;
    if (!noise_shape_is_good(shape)) {
        LOG_ERROR("terrain noise: field %s has a shape outside the limits in terrain/noise/index.h", name);
        return false;
    }
    if (bank->count == bank->capacity) {
        LOG_ERROR("terrain noise: no room for field %s: the bank holds %" PRIu32, name, bank->capacity);
        return false;
    }
    const uint64_t salt = terrain_rng_salt(name);
    for (uint32_t i = 0u; i < bank->count; i++) {
        if (bank->borrowed_fields[i].salt == salt) {
            LOG_ERROR("terrain noise: field %s is already in the bank", name);
            return false;
        }
    }

    terrain_noise_field_t *const field = &bank->borrowed_fields[bank->count];
    memset(field, 0, sizeof(*field));
    field->borrowed_name = name;
    field->salt = salt;
    field->octave_count = shape->octaves;

    /* By repeated multiplication, and not pow: the same bits on any libm. */
    const uint64_t field_seed = terrain_rng_derive(bank->seed, salt);
    float64_t scale = 1.0f64;
    float64_t amplitude = 1.0f64;
    float64_t amplitude_total = 0.0f64;
    for (uint32_t i = 0u; i < shape->octaves; i++) {
        terrain_noise_octave_t *const octave = &field->octaves[i];
        terrain_rng_t rng = terrain_rng_start(terrain_rng_derive(field_seed, i));
        octave->seed = terrain_rng_next_u32(&rng);
        octave->offset_x = terrain_rng_next_f64(&rng) * NOISE_OFFSET_CELLS;
        octave->offset_y = terrain_rng_next_f64(&rng) * NOISE_OFFSET_CELLS;
        octave->offset_z = terrain_rng_next_f64(&rng) * NOISE_OFFSET_CELLS;
        octave->frequency_x = shape->frequency_x * scale;
        octave->frequency_y = shape->frequency_y * scale;
        octave->frequency_z = shape->frequency_z * scale;
        amplitude_total += amplitude;
        scale *= (float64_t)shape->lacunarity;
        amplitude *= (float64_t)shape->persistence;
    }
    amplitude = 1.0f64;
    for (uint32_t i = 0u; i < shape->octaves; i++) {
        field->octaves[i].amplitude = (float32_t)(amplitude / amplitude_total);
        amplitude *= (float64_t)shape->persistence;
    }
    bank->count++;
    *out = field;
    return true;
}

uint64_t terrain_noise_grid_points(const terrain_noise_grid_t *grid) {
    return (uint64_t)grid->count_x * grid->count_y * grid->count_z;
}

/* Up to NOISE_BATCH_POINTS points. Each coordinate is worked out in float64_t and only then narrowed, so that it comes out
 * the same from any caller, and the narrowing is the only rounding the kernel's input has seen. */
static void noise_sample_batch(const terrain_noise_field_t *field, const float64_t *xs, const float64_t *ys,
                               const float64_t *zs, uint32_t count, float32_t *out) {
    float32_t cell_x[NOISE_BATCH_POINTS];
    float32_t cell_y[NOISE_BATCH_POINTS];
    float32_t cell_z[NOISE_BATCH_POINTS];
    float32_t value[NOISE_BATCH_POINTS];
    for (uint32_t i = 0u; i < count; i++) {
        out[i] = 0.0f32;
    }
    for (uint32_t o = 0u; o < field->octave_count; o++) {
        const terrain_noise_octave_t *const octave = &field->octaves[o];
        for (uint32_t i = 0u; i < count; i++) {
            cell_x[i] = (float32_t)(xs[i] * octave->frequency_x + octave->offset_x);
            cell_y[i] = (float32_t)(ys[i] * octave->frequency_y + octave->offset_y);
            cell_z[i] = (float32_t)(zs[i] * octave->frequency_z + octave->offset_z);
        }
        noise3_batch8(value, cell_x, cell_y, cell_z, count, octave->seed);
        for (uint32_t i = 0u; i < count; i++) {
            out[i] += value[i] * octave->amplitude;
        }
    }
}

void terrain_noise_sample_points(const terrain_noise_field_t *field, const float64_t *xs, const float64_t *ys,
                                 const float64_t *zs, uint64_t count, float32_t *out) {
    for (uint64_t done = 0u; done < count;) {
        const uint64_t left = count - done;
        const uint32_t batch = left < NOISE_BATCH_POINTS ? (uint32_t)left : NOISE_BATCH_POINTS;
        noise_sample_batch(field, xs + done, ys + done, zs + done, batch, out + done);
        done += batch;
    }
}

/* One axis of a grid, as a block coordinate. In 64 bits, since origin + i * step can pass an int32_t, and a float64_t holds
 * the result exactly for any grid that fits in memory. */
static float64_t noise_grid_axis(int32_t origin, int32_t step, uint32_t index) {
    return (float64_t)((int64_t)origin + (int64_t)step * (int64_t)index);
}

void terrain_noise_sample_grid(const terrain_noise_field_t *field, const terrain_noise_grid_t *grid, float32_t *out) {
    float64_t xs[NOISE_BATCH_POINTS];
    float64_t ys[NOISE_BATCH_POINTS];
    float64_t zs[NOISE_BATCH_POINTS];
    uint32_t held = 0u;
    uint64_t done = 0u;
    for (uint32_t iz = 0u; iz < grid->count_z; iz++) {
        const float64_t z = noise_grid_axis(grid->origin_z, grid->step_z, iz);
        for (uint32_t iy = 0u; iy < grid->count_y; iy++) {
            const float64_t y = noise_grid_axis(grid->origin_y, grid->step_y, iy);
            for (uint32_t ix = 0u; ix < grid->count_x; ix++) {
                xs[held] = noise_grid_axis(grid->origin_x, grid->step_x, ix);
                ys[held] = y;
                zs[held] = z;
                held++;
                if (held == NOISE_BATCH_POINTS) {
                    noise_sample_batch(field, xs, ys, zs, held, out + done);
                    done += held;
                    held = 0u;
                }
            }
        }
    }
    noise_sample_batch(field, xs, ys, zs, held, out + done);
}
