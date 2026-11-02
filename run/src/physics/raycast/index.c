/* See README.md. Amanatides & Woo's walk through the cells, on the sim's copy of the world. */
#include "physics/raycast/index.h"

#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>

/* Past this a cell's number does not fit in int32_t. */
#define RAYCAST_ORIGIN_MAX 2147483000.0f64

/* What cannot be stood on is aimed at by a box around what is drawn, in 32nds of a block: the numbers of cuda/include/traverse/,
 * and for what is thinner than anyone could aim at, somewhat more. */
typedef struct raycast_part_t {
    uint8_t low[COLLISION_AXES];  /* 0 */
    uint8_t high[COLLISION_AXES]; /* 3 */
} raycast_part_t;
static_assert(sizeof(raycast_part_t) == 6);

static const raycast_part_t RAYCAST_PART_WHOLE = {{0u, 0u, 0u}, {32u, 32u, 32u}};
static const raycast_part_t RAYCAST_PART_CROSS = {{4u, 4u, 0u}, {28u, 28u, 32u}};
static const raycast_part_t RAYCAST_PART_TORCH = {{12u, 12u, 0u}, {20u, 20u, 20u}};
static const raycast_part_t RAYCAST_PART_PLATE = {{0u, 30u, 0u}, {32u, 32u, 32u}};
static const raycast_part_t RAYCAST_PART_FLAT = {{0u, 0u, 0u}, {32u, 32u, 2u}};
#define RAYCAST_PART_UNIT (1.0f64 / 32.0f64)

static uint32_t raycast_part_box(const raycast_part_t *part, int32_t x, int32_t y, int32_t z, collision_box_t *out) {
    const int32_t cell[COLLISION_AXES] = {x, y, z};
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out->min[a] = (float64_t)cell[a] + (float64_t)part->low[a] * RAYCAST_PART_UNIT;
        out->max[a] = (float64_t)cell[a] + (float64_t)part->high[a] * RAYCAST_PART_UNIT;
    }
    return 1u;
}

uint32_t raycast_block_boxes(uint16_t block, int32_t x, int32_t y, int32_t z, collision_box_t *out) {
    /* A number no block has stays a solid thing here too, and collision_block_boxes knows what to do with it. */
    if (block >= BLOCK_COUNT || (block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u) {
        return collision_block_boxes(block, x, y, z, out);
    }
    switch ((block_shape_t)block_defs[block].shape) {
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_FLUID:
        return 0u;
    case BLOCK_SHAPE_CROSS:
        return raycast_part_box(&RAYCAST_PART_CROSS, x, y, z, out);
    case BLOCK_SHAPE_TORCH:
        return raycast_part_box(&RAYCAST_PART_TORCH, x, y, z, out);
    case BLOCK_SHAPE_THIN:
        /* The same two as render/frame's table: a lily pad lies on the floor, and the others stand against the +y side. */
        return raycast_part_box(block == BLOCK_ID_LILY_PAD ? &RAYCAST_PART_FLAT : &RAYCAST_PART_PLATE, x, y, z, out);
    case BLOCK_SHAPE_CROP:
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_SLAB:
    case BLOCK_SHAPE_STAIRS:
        break;
    }
    return raycast_part_box(&RAYCAST_PART_WHOLE, x, y, z, out);
}

/* The slab test. How far along it the ray comes into the box, and by which face. False if it never goes in. */
static bool raycast_box_enter(const collision_box_t *box, const float64_t origin[COLLISION_AXES],
                              const float64_t direction[COLLISION_AXES], float64_t *out_near, uint32_t *out_axis) {
    float64_t near = -HUGE_VAL, far = HUGE_VAL;
    uint32_t axis = RAYCAST_AXIS_NONE;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (direction[a] == 0.0f64) {
            /* Along a plane of its faces it is in the cell above that plane and not below, like floor(). */
            if (origin[a] < box->min[a] || origin[a] >= box->max[a]) {
                return false;
            }
            continue;
        }
        const float64_t t_min = (box->min[a] - origin[a]) / direction[a];
        const float64_t t_max = (box->max[a] - origin[a]) / direction[a];
        const float64_t t_in = t_min < t_max ? t_min : t_max;
        const float64_t t_out = t_min < t_max ? t_max : t_min;
        if (t_in > near) {
            near = t_in;
            axis = a;
        }
        far = t_out < far ? t_out : far;
    }
    if (!(near < far) || far <= 0.0f64) {
        return false;
    }
    *out_near = near;
    *out_axis = axis;
    return true;
}

/* The nearest box of the block in this cell the ray goes into within limit, if any. */
static bool raycast_cell_test(uint16_t block, const int32_t cell[COLLISION_AXES],
                              const float64_t origin[COLLISION_AXES], const float64_t direction[COLLISION_AXES],
                              float64_t limit, raycast_hit_t *out) {
    collision_box_t boxes[RAYCAST_BOXES_PER_BLOCK];
    const uint32_t count = raycast_block_boxes(block, cell[0], cell[1], cell[2], boxes);
    float64_t best = HUGE_VAL;
    uint32_t best_axis = RAYCAST_AXIS_NONE;
    for (uint32_t i = 0u; i < count; i++) {
        float64_t near = 0.0f64;
        uint32_t axis = RAYCAST_AXIS_NONE;
        if (raycast_box_enter(&boxes[i], origin, direction, &near, &axis) && near < best) {
            best = near;
            best_axis = axis;
        }
    }
    if (best > limit) {
        return false;
    }
    collision_box_t bounds = boxes[0];
    for (uint32_t i = 1u; i < count; i++) {
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            bounds.min[a] = boxes[i].min[a] < bounds.min[a] ? boxes[i].min[a] : bounds.min[a];
            bounds.max[a] = boxes[i].max[a] > bounds.max[a] ? boxes[i].max[a] : bounds.max[a];
        }
    }
    const bool inside = best < 0.0f64;
    out->bounds = bounds;
    out->distance = inside ? 0.0f64 : best;
    out->block = block;
    out->axis = inside ? (uint8_t)RAYCAST_AXIS_NONE : (uint8_t)best_axis;
    out->normal_sign = inside ? (int8_t)0 : (direction[best_axis] > 0.0f64 ? (int8_t)-1 : (int8_t)1);
    return true;
}

static raycast_result_t raycast_end(raycast_hit_t *out, raycast_result_t result, const int32_t cell[COLLISION_AXES]) {
    out->result = (uint8_t)result;
    if (cell != nullptr) {
        out->cell[0] = cell[0];
        out->cell[1] = cell[1];
        out->cell[2] = cell[2];
    }
    return result;
}

/* Where a ray next comes to a wall on this axis, worked out from the cell's own number so that no error adds up along it. */
static float64_t raycast_next_wall(int32_t cell, int32_t step, float64_t origin, float64_t unit) {
    if (step == 0) {
        return HUGE_VAL;
    }
    return ((float64_t)cell + (step > 0 ? 1.0f64 : 0.0f64) - origin) / unit;
}

raycast_result_t raycast_cast(const world_map_t *map, const float64_t origin[COLLISION_AXES],
                              const float64_t direction[COLLISION_AXES], float64_t max_distance, raycast_hit_t *out) {
    *out = (raycast_hit_t){.axis = RAYCAST_AXIS_NONE};
    const float64_t length =
        sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    /* Written so that a NaN in any of them comes out as a miss. */
    if (!(length > 0.0f64) || !isfinite(length) || !(max_distance > 0.0f64) ||
        !(max_distance <= RAYCAST_DISTANCE_MAX)) {
        return raycast_end(out, RAYCAST_RESULT_MISS, nullptr);
    }
    int32_t cell[COLLISION_AXES];
    int32_t step[COLLISION_AXES];
    float64_t unit[COLLISION_AXES];
    float64_t t_next[COLLISION_AXES];
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (!(fabs(origin[a]) < RAYCAST_ORIGIN_MAX)) {
            return raycast_end(out, RAYCAST_RESULT_MISS, nullptr);
        }
        unit[a] = direction[a] / length;
        cell[a] = (int32_t)floor(origin[a]);
        step[a] = unit[a] > 0.0f64 ? 1 : (unit[a] < 0.0f64 ? -1 : 0);
        t_next[a] = raycast_next_wall(cell[a], step[a], origin[a], unit[a]);
    }
    float64_t t_cell = 0.0f64;
    while (t_cell <= max_distance) {
        if (cell[COLLISION_AXIS_Z] >= COLUMN_Z_GAMEPLAY_MIN && cell[COLLISION_AXIS_Z] <= COLUMN_Z_GAMEPLAY_MAX) {
            uint16_t block = BLOCK_ID_AIR;
            if (!collision_cell_read(map, cell[0], cell[1], cell[2], &block)) {
                out->distance = t_cell;
                return raycast_end(out, RAYCAST_RESULT_NOT_LOADED, cell);
            }
            if (raycast_cell_test(block, cell, origin, unit, max_distance, out)) {
                return raycast_end(out, RAYCAST_RESULT_HIT, cell);
            }
        }
        /* The next wall it comes to, the lowest axis first where two are met at once. */
        uint32_t axis = COLLISION_AXIS_X;
        axis = t_next[COLLISION_AXIS_Y] < t_next[axis] ? COLLISION_AXIS_Y : axis;
        axis = t_next[COLLISION_AXIS_Z] < t_next[axis] ? COLLISION_AXIS_Z : axis;
        t_cell = t_next[axis];
        cell[axis] += step[axis];
        t_next[axis] = raycast_next_wall(cell[axis], step[axis], origin[axis], unit[axis]);
    }
    return raycast_end(out, RAYCAST_RESULT_MISS, nullptr);
}

void raycast_hit_in_front(const raycast_hit_t *hit, int32_t out[COLLISION_AXES]) {
    out[0] = hit->cell[0];
    out[1] = hit->cell[1];
    out[2] = hit->cell[2];
    if (hit->axis < COLLISION_AXES) {
        out[hit->axis] += hit->normal_sign;
    }
}
