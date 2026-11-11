/* See README.md. */
#include "terrain/ore/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "tables/ores/index.h"
#include "terrain/rng/index.h"

#include <string.h>

enum : uint32_t {
    ORE_CHUNK_SHIFT = 4u,
    ORE_PER_CHUNK_WHOLE_SHIFT = 8u, /* per_chunk_256's whole part */
    ORE_PER_CHUNK_PART = (1u << ORE_PER_CHUNK_WHOLE_SHIFT),
};
static_assert(MC_WORLD_CHUNK_SIZE == 1 << ORE_CHUNK_SHIFT);
static_assert(BLOCK_ID_AIR == 0u); /* ore_top_of_anything ors a layer together */

/* The table is config/ores/, which compile/src/game_tables/ores/ turns into tables/ores/. The README says where each number
 * comes from. Changing any of them changes every world. */
static_assert(TERRAIN_ORE_ROWS == ORE_ROW_COUNT);

/* The ball of the README's sum: the one radius, so 3 balls that overlap by about this much make the row's size. */
static const float64_t ORE_OVERLAP = 2.0f64;
static const float64_t ORE_FOUR_THIRDS_PI = 4.18879020478639f64;

/* The r for which r * r * r is the wanted volume. By halves, and not with libm's cbrt, since nothing that decides a block may
 * call what another libm could round differently. */
static float64_t ore_cube_root(float64_t value) {
    float64_t low = 0.0f64;
    float64_t high = value + 1.0f64;
    for (uint32_t i = 0u; i < 64u; i++) {
        const float64_t middle = 0.5f64 * (low + high);
        if (middle * middle * middle < value) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return high;
}

void terrain_ore_init(terrain_ore_t **out, arena_t *arena, uint64_t seed, const terrain_density_t *density) {
    terrain_ore_t *const ore = arena_alloc(arena, sizeof(*ore), alignof(terrain_ore_t));
    memset(ore, 0, sizeof(*ore));
    ore->borrowed_density = density;
    ore->strata_seed = terrain_rng_derive(seed, terrain_rng_salt("ore/strata"));
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        const terrain_ore_row_t *const row = &ore_rows[r];
        const float64_t radius = ore_cube_root((float64_t)row->size / (ORE_FOUR_THIRDS_PI * ORE_OVERLAP));
        ore->rows[r].seed = terrain_rng_derive(seed, terrain_rng_salt(row->borrowed_name));
        ore->rows[r].radius = (float32_t)radius;
        /* A ball's middle is at most half a block plus two steps of a radius from the start, and a voxel is at most a radius from
         * that. 1 more for the rounding of the two conversions. */
        ore->rows[r].reach = (int32_t)(3.0f64 * radius + 0.5f64) + 1;
        ASSERT(ore->rows[r].reach < MC_WORLD_CHUNK_SIZE);
    }
    *out = ore;
}

void terrain_ore_deinit(terrain_ore_t *ore) {
    memset(ore, 0, sizeof(*ore));
}

const terrain_ore_row_t *terrain_ore_table(void) {
    return ore_rows;
}

/* ---- Rock by depth ------------------------------------------------------------------------------------------------ */

/* Stone to deepslate, which is what makes the deep ores deep. Per voxel only in the band, where it is by chance. */
static void ore_strata(const terrain_ore_t *ore, const terrain_density_box_t *box, uint16_t *blocks) {
    const int64_t z_first = box->origin_z;
    const int64_t z_last = z_first + (int64_t)box->count_z - 1;
    const int64_t top =
        z_last < (int64_t)TERRAIN_ORE_DEEPSLATE_TOP - 1 ? z_last : (int64_t)TERRAIN_ORE_DEEPSLATE_TOP - 1;
    const uint32_t band = (uint32_t)(TERRAIN_ORE_DEEPSLATE_TOP - TERRAIN_ORE_DEEPSLATE_BOTTOM);
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    for (int64_t z = z_first; z <= top; z++) {
        uint16_t *const voxels = blocks + (uint64_t)(z - z_first) * layer;
        if (z <= (int64_t)TERRAIN_ORE_DEEPSLATE_BOTTOM) {
            for (uint64_t i = 0u; i < layer; i++) {
                voxels[i] = voxels[i] == BLOCK_ID_STONE ? (uint16_t)BLOCK_ID_DEEPSLATE : voxels[i];
            }
            continue;
        }
        const uint32_t share = (uint32_t)((int64_t)TERRAIN_ORE_DEEPSLATE_TOP - z);
        uint64_t i = 0u;
        for (uint32_t iy = 0u; iy < box->count_y; iy++) {
            for (uint32_t ix = 0u; ix < box->count_x; ix++) {
                if (voxels[i] == BLOCK_ID_STONE) {
                    const int32_t x = (int32_t)((int64_t)box->origin_x + (int64_t)ix);
                    const int32_t y = (int32_t)((int64_t)box->origin_y + (int64_t)iy);
                    if (terrain_rng_hash3(ore->strata_seed, x, y, (int32_t)z) % band < share) {
                        voxels[i] = BLOCK_ID_DEEPSLATE;
                    }
                }
                i++;
            }
        }
    }
}

/* The top of what is in the box and is not air. A vein replaces nothing that is air, so a row wholly over this cannot change the box, and leaving it
 * out moves nothing. Most of a tall column is air, so this is the cost of rows only mountains can use. */
static int64_t ore_top_of_anything(const terrain_density_box_t *box, const uint16_t *blocks) {
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    for (uint32_t iz = box->count_z; iz-- > 0u;) {
        const uint16_t *const voxels = blocks + (uint64_t)iz * layer;
        uint16_t any = BLOCK_ID_AIR;
        for (uint64_t i = 0u; i < layer; i++) {
            any |= voxels[i];
        }
        if (any != BLOCK_ID_AIR) {
            return (int64_t)box->origin_z + (int64_t)iz;
        }
    }
    return (int64_t)box->origin_z - 1;
}

/* ---- Veins -------------------------------------------------------------------------------------------------------- */

/* What a row makes of a voxel holding this block. */
static uint16_t ore_replace(const terrain_ore_row_t *row, uint16_t host) {
    if (row->block == row->deep_block) { /* a rock */
        return (host == BLOCK_ID_STONE || host == BLOCK_ID_DEEPSLATE) ? row->block : host;
    }
    if (host == BLOCK_ID_STONE || host == BLOCK_ID_GRANITE || host == BLOCK_ID_DIORITE || host == BLOCK_ID_ANDESITE) {
        return row->block;
    }
    return (host == BLOCK_ID_DEEPSLATE || host == BLOCK_ID_TUFF) ? row->deep_block : host;
}

/* One ball of a vein. origin is where the vein started, and the ball's middle is relative to it, so that the sums stay small
 * and exact however far out the world goes. */
static void ore_ball(const terrain_density_box_t *box, const terrain_ore_row_t *row, uint16_t *blocks,
                     const int64_t *origin, const float32_t *middle, float32_t radius) {
    const float32_t radius_squared = radius * radius;
    int64_t from[3];
    int64_t to[3];
    const int64_t box_from[3] = {box->origin_x, box->origin_y, box->origin_z};
    const int64_t box_to[3] = {(int64_t)box->origin_x + (int64_t)box->count_x - 1,
                               (int64_t)box->origin_y + (int64_t)box->count_y - 1,
                               (int64_t)box->origin_z + (int64_t)box->count_z - 1};
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        const int64_t low = origin[axis] + (int64_t)(middle[axis] - radius) - 1;
        const int64_t high = origin[axis] + (int64_t)(middle[axis] + radius) + 1;
        from[axis] = low > box_from[axis] ? low : box_from[axis];
        to[axis] = high < box_to[axis] ? high : box_to[axis];
        if (from[axis] > to[axis]) {
            return;
        }
    }
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    for (int64_t z = from[2]; z <= to[2]; z++) {
        const float32_t dz = (float32_t)(z - origin[2]) - middle[2];
        for (int64_t y = from[1]; y <= to[1]; y++) {
            const float32_t dy = (float32_t)(y - origin[1]) - middle[1];
            const float32_t dyz = dz * dz + dy * dy;
            if (dyz > radius_squared) {
                continue;
            }
            uint16_t *const row_of_voxels =
                blocks + (uint64_t)(z - box->origin_z) * layer + (uint64_t)(y - box->origin_y) * box->count_x;
            for (int64_t x = from[0]; x <= to[0]; x++) {
                const float32_t dx = (float32_t)(x - origin[0]) - middle[0];
                if (dyz + dx * dx <= radius_squared) {
                    uint16_t *const voxel = &row_of_voxels[x - box->origin_x];
                    *voxel = ore_replace(row, *voxel);
                }
            }
        }
    }
}

/* The balls of one vein. A ball steps at most a radius on each axis from the one before. */
static void ore_vein(const terrain_density_box_t *box, const terrain_ore_row_t *row, float32_t radius,
                     uint64_t vein_seed, const int64_t *origin, uint16_t *blocks) {
    terrain_rng_t stream = terrain_rng_start(vein_seed);
    float32_t middle[3];
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        middle[axis] = terrain_rng_next_f32(&stream) - 0.5f32;
    }
    ore_ball(box, row, blocks, origin, middle, radius);
    for (uint32_t ball = 1u; ball < TERRAIN_ORE_BALLS; ball++) {
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            middle[axis] += (terrain_rng_next_f32(&stream) * 2.0f32 - 1.0f32) * radius;
        }
        ore_ball(box, row, blocks, origin, middle, radius);
    }
}

/* A row's z, drawn the same way whether or not the box wants it. */
static int32_t ore_draw_z(terrain_rng_t *stream, const terrain_ore_row_t *row) {
    const uint32_t range = (uint32_t)((int32_t)row->z_high - (int32_t)row->z_low);
    if (row->spread == TERRAIN_ORE_TRIANGLE) {
        const uint32_t half = range / 2u;
        const uint32_t a = terrain_rng_next_below(stream, half + 1u);
        const uint32_t b = terrain_rng_next_below(stream, range - half + 1u);
        return (int32_t)row->z_low + (int32_t)(a + b);
    }
    return (int32_t)row->z_low + (int32_t)terrain_rng_next_below(stream, range + 1u);
}

/* The veins one row starts in one chunk that reach the box. Every draw before the box is looked at is made for every attempt,
 * so that what a vein is cannot depend on which box asked. */
static void ore_chunk(const terrain_ore_row_t *row, const terrain_ore_world_row_t *world_row,
                      const terrain_density_box_t *box, int32_t chunk_x, int32_t chunk_y, uint16_t *blocks) {
    terrain_rng_t stream = terrain_rng_start(terrain_rng_hash2(world_row->seed, chunk_x, chunk_y));
    uint32_t attempts = row->per_chunk_256 >> ORE_PER_CHUNK_WHOLE_SHIFT;
    if (terrain_rng_next_below(&stream, ORE_PER_CHUNK_PART) < (row->per_chunk_256 & (ORE_PER_CHUNK_PART - 1u))) {
        attempts++;
    }
    const int64_t reach = (int64_t)world_row->reach;
    const int64_t x_low = (int64_t)box->origin_x - reach;
    const int64_t y_low = (int64_t)box->origin_y - reach;
    const int64_t z_low = (int64_t)box->origin_z - reach;
    const int64_t x_high = (int64_t)box->origin_x + (int64_t)box->count_x - 1 + reach;
    const int64_t y_high = (int64_t)box->origin_y + (int64_t)box->count_y - 1 + reach;
    const int64_t z_high = (int64_t)box->origin_z + (int64_t)box->count_z - 1 + reach;
    for (uint32_t attempt = 0u; attempt < attempts; attempt++) {
        const uint32_t where = terrain_rng_next_u32(&stream);
        const int32_t z = ore_draw_z(&stream, row);
        const uint64_t vein_seed = terrain_rng_next_u64(&stream);
        const int64_t origin[3] = {
            (int64_t)chunk_x * MC_WORLD_CHUNK_SIZE + (int64_t)(where & 15u),
            (int64_t)chunk_y * MC_WORLD_CHUNK_SIZE + (int64_t)((where >> 4u) & 15u),
            (int64_t)z,
        };
        if (z < MC_WORLD_Z_MIN || z > MC_WORLD_Z_MAX) {
            continue;
        }
        if (origin[0] < x_low || origin[0] > x_high || origin[1] < y_low || origin[1] > y_high || origin[2] < z_low ||
            origin[2] > z_high) {
            continue;
        }
        ore_vein(box, row, world_row->radius, vein_seed, origin, blocks);
    }
}

/* Whether a row may go under this chunk at all. */
static bool ore_chunk_allowed(const terrain_ore_t *ore, const terrain_ore_row_t *row, int64_t chunk_x,
                              int64_t chunk_y) {
    if (row->min_column_height == TERRAIN_ORE_ANY_COLUMN) {
        return true;
    }
    const int64_t x = chunk_x * MC_WORLD_CHUNK_SIZE + MC_WORLD_CHUNK_SIZE / 2;
    const int64_t y = chunk_y * MC_WORLD_CHUNK_SIZE + MC_WORLD_CHUNK_SIZE / 2;
    if (x > INT32_MAX || y > INT32_MAX || x < INT32_MIN || y < INT32_MIN) {
        return false; /* a chunk's middle off the end of the coordinates: never a mountain */
    }
    const terrain_fields_area_t area = {
        .origin_x = (int32_t)x,
        .origin_y = (int32_t)y,
        .step_x = 1,
        .step_y = 1,
        .count_x = 1u,
        .count_y = 1u,
    };
    float32_t height = 0.0f32;
    float32_t softness = 0.0f32;
    terrain_density_profile_area(ore->borrowed_density, &area, &height, &softness);
    return height >= (float32_t)row->min_column_height;
}

uint32_t terrain_ore_chunk_veins(const terrain_ore_t *ore, uint32_t row_index, int32_t chunk_x, int32_t chunk_y) {
    ASSERT(row_index < TERRAIN_ORE_ROWS);
    const terrain_ore_row_t *const row = &ore_rows[row_index];
    if (!ore_chunk_allowed(ore, row, chunk_x, chunk_y)) {
        return 0u;
    }
    /* ore_chunk's draws, in its order, with no box. */
    terrain_rng_t stream = terrain_rng_start(terrain_rng_hash2(ore->rows[row_index].seed, chunk_x, chunk_y));
    uint32_t attempts = row->per_chunk_256 >> ORE_PER_CHUNK_WHOLE_SHIFT;
    if (terrain_rng_next_below(&stream, ORE_PER_CHUNK_PART) < (row->per_chunk_256 & (ORE_PER_CHUNK_PART - 1u))) {
        attempts++;
    }
    uint32_t landed = 0u;
    for (uint32_t attempt = 0u; attempt < attempts; attempt++) {
        (void)terrain_rng_next_u32(&stream);
        const int32_t z = ore_draw_z(&stream, row);
        (void)terrain_rng_next_u64(&stream);
        landed += (z >= MC_WORLD_Z_MIN && z <= MC_WORLD_Z_MAX) ? 1u : 0u;
    }
    return landed;
}

void terrain_ore_fill_box(const terrain_ore_t *ore, const terrain_density_box_t *box, uint16_t *blocks) {
    if (box->count_x == 0u || box->count_y == 0u || box->count_z == 0u) {
        return;
    }
    ore_strata(ore, box, blocks);
    const int64_t z_first = box->origin_z;
    const int64_t z_last = ore_top_of_anything(box, blocks);
    /* Row by row, then chunk by chunk from the south-west, as the README says: leaving out a vein that misses a voxel cannot move
     * what the others do to it, so any box of it gets the same answer. */
    for (uint32_t r = 0u; r < TERRAIN_ORE_ROWS; r++) {
        const terrain_ore_row_t *const row = &ore_rows[r];
        const terrain_ore_world_row_t *const world_row = &ore->rows[r];
        const int64_t reach = (int64_t)world_row->reach;
        if ((int64_t)row->z_high + reach < z_first || (int64_t)row->z_low - reach > z_last) {
            continue;
        }
        const int64_t chunk_x_first = ((int64_t)box->origin_x - reach) >> ORE_CHUNK_SHIFT;
        const int64_t chunk_y_first = ((int64_t)box->origin_y - reach) >> ORE_CHUNK_SHIFT;
        const int64_t chunk_x_last = ((int64_t)box->origin_x + (int64_t)box->count_x - 1 + reach) >> ORE_CHUNK_SHIFT;
        const int64_t chunk_y_last = ((int64_t)box->origin_y + (int64_t)box->count_y - 1 + reach) >> ORE_CHUNK_SHIFT;
        for (int64_t chunk_y = chunk_y_first; chunk_y <= chunk_y_last; chunk_y++) {
            for (int64_t chunk_x = chunk_x_first; chunk_x <= chunk_x_last; chunk_x++) {
                if (ore_chunk_allowed(ore, row, chunk_x, chunk_y)) {
                    ore_chunk(row, world_row, box, (int32_t)chunk_x, (int32_t)chunk_y, blocks);
                }
            }
        }
    }
}
