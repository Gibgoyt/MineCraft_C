/* See README.md. The Arena tier: the one allocation here is the list of boxes, from the arena its Owner gives it. */
#include "physics/collision/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>

/* Three stops, one per axis, and a last look that finds nothing in the way. */
enum : uint32_t { COLLISION_SWEEP_ROUNDS = COLLISION_AXES + 1u };

bool collision_cell_read(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint16_t *out) {
    const column_t *const column = world_map_column_at(map, column_chunk_of(x), column_chunk_of(y));
    if (column == nullptr) {
        return false;
    }
    if (z < COLUMN_Z_GAMEPLAY_MIN) {
        *out = BLOCK_ID_BEDROCK;
    } else if (z > COLUMN_Z_GAMEPLAY_MAX) {
        *out = BLOCK_ID_AIR;
    } else {
        const bool in_range = column_block_get(column, column_local_of(x), column_local_of(y), z, out);
        ASSERT(in_range);
        (void)in_range;
    }
    return true;
}

static collision_box_t collision_cell_part(int32_t x, int32_t y, int32_t z, float64_t from_y, float64_t from_z,
                                           float64_t to_z) {
    const float64_t cell_x = (float64_t)x, cell_y = (float64_t)y, cell_z = (float64_t)z;
    return (collision_box_t){
        .min = {cell_x, cell_y + from_y, cell_z + from_z},
        .max = {cell_x + 1.0f64, cell_y + 1.0f64, cell_z + to_z},
    };
}

uint32_t collision_block_boxes(uint16_t block, int32_t x, int32_t y, int32_t z, collision_box_t *out) {
    /* A number no block has is what a broken world would hold: better to stand on it than to fall through. */
    const bool known = block < BLOCK_COUNT;
    if (known && (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u) {
        return 0u;
    }
    const uint8_t shape = known ? block_defs[block].shape : (uint8_t)BLOCK_SHAPE_CUBE;
    if (shape == BLOCK_SHAPE_SLAB) {
        out[0] = collision_cell_part(x, y, z, 0.0f64, 0.0f64, 0.5f64);
        return 1u;
    }
    if (shape == BLOCK_SHAPE_STAIRS) {
        out[0] = collision_cell_part(x, y, z, 0.0f64, 0.0f64, 0.5f64);
        /* A block is one state for now and has no facing, so all stairs climb to the north. */
        out[1] = collision_cell_part(x, y, z, 0.5f64, 0.5f64, 1.0f64);
        return 2u;
    }
    out[0] = collision_cell_part(x, y, z, 0.0f64, 0.0f64, 1.0f64);
    return 1u;
}

collision_box_t *collision_gather(arena_t *arena, const world_map_t *map, const collision_box_t *around,
                                  uint32_t *out_count) {
    const int32_t x_from = (int32_t)floor(around->min[COLLISION_AXIS_X] - COLLISION_EPSILON);
    const int32_t y_from = (int32_t)floor(around->min[COLLISION_AXIS_Y] - COLLISION_EPSILON);
    const int32_t z_from = (int32_t)floor(around->min[COLLISION_AXIS_Z] - COLLISION_EPSILON);
    const int32_t x_to = (int32_t)floor(around->max[COLLISION_AXIS_X] + COLLISION_EPSILON);
    const int32_t y_to = (int32_t)floor(around->max[COLLISION_AXIS_Y] + COLLISION_EPSILON);
    const int32_t z_to = (int32_t)floor(around->max[COLLISION_AXIS_Z] + COLLISION_EPSILON);
    const uint64_t cells =
        (uint64_t)(x_to - x_from + 1) * (uint64_t)(y_to - y_from + 1) * (uint64_t)(z_to - z_from + 1);
    collision_box_t *const boxes =
        arena_alloc_array(arena, cells * COLLISION_BOXES_PER_BLOCK, sizeof(collision_box_t), alignof(collision_box_t));
    uint32_t count = 0u;
    for (int32_t x = x_from; x <= x_to; x++) {
        for (int32_t y = y_from; y <= y_to; y++) {
            for (int32_t z = z_from; z <= z_to; z++) {
                uint16_t block = BLOCK_ID_AIR;
                /* Not loaded is a wall: the number no block has. */
                const uint16_t seen = collision_cell_read(map, x, y, z, &block) ? block : (uint16_t)BLOCK_COUNT;
                count += collision_block_boxes(seen, x, y, z, &boxes[count]);
            }
        }
    }
    *out_count = count;
    return boxes;
}

/* The first moment from now that box runs into another, or 1 if none does in the time left. Out says on which axis and at what
 * coordinate, untouched if none. Time here is a fraction of delta. */
static float64_t collision_first_hit(const collision_box_t *boxes, uint32_t count, const collision_box_t *box,
                                     const float64_t delta[COLLISION_AXES], uint32_t *out_axis, float64_t *out_face) {
    /* A box reaching two of its faces at the same moment stops on the first in this order. So walking across a seam while coming
     * down on it stops on the next block's top, and not at its side, which is flush with the floor. */
    static const uint32_t order[COLLISION_AXES] = {COLLISION_AXIS_Z, COLLISION_AXIS_X, COLLISION_AXIS_Y};
    float64_t first = 1.0f64;
    uint32_t first_axis = COLLISION_AXES;
    for (uint32_t b = 0u; b < count; b++) {
        const collision_box_t *const other = &boxes[b];
        float64_t entry = -INFINITY, exit = INFINITY;
        uint32_t entry_axis = COLLISION_AXES;
        bool apart = false;
        for (uint32_t i = 0u; i < COLLISION_AXES && !apart; i++) {
            const uint32_t axis = order[i];
            const float64_t d = delta[axis];
            if (d == 0.0f64) {
                apart = box->max[axis] <= other->min[axis] + COLLISION_EPSILON ||
                        box->min[axis] >= other->max[axis] - COLLISION_EPSILON;
                continue;
            }
            /* How far until the faces meet, and until they have gone past by more than a touch. */
            const float64_t speed = fabs(d);
            const float64_t gap = d > 0.0f64 ? other->min[axis] - box->max[axis] : box->min[axis] - other->max[axis];
            const float64_t through = d > 0.0f64 ? other->max[axis] - COLLISION_EPSILON - box->min[axis]
                                                 : box->max[axis] - (other->min[axis] + COLLISION_EPSILON);
            /* Touching already is meeting now. */
            const float64_t axis_entry = (gap < 0.0f64 && gap >= -COLLISION_EPSILON ? 0.0f64 : gap) / speed;
            const float64_t axis_exit = through / speed;
            if (axis_entry > entry) {
                entry = axis_entry;
                entry_axis = axis;
            }
            exit = axis_exit < exit ? axis_exit : exit;
        }
        /* Below 0: it overlaps already, or has gone by. */
        if (apart || entry_axis == COLLISION_AXES || entry < 0.0f64 || entry >= exit || entry >= first) {
            continue;
        }
        first = entry;
        first_axis = entry_axis;
        *out_face = delta[entry_axis] > 0.0f64 ? other->min[entry_axis] : other->max[entry_axis];
    }
    *out_axis = first_axis;
    return first;
}

void collision_sweep(const collision_box_t *boxes, uint32_t count, const collision_box_t *box,
                     const float64_t delta[COLLISION_AXES], collision_move_t *out) {
    collision_box_t at = *box;
    float64_t left[COLLISION_AXES] = {delta[0], delta[1], delta[2]};
    *out = (collision_move_t){.moved = {0.0f64, 0.0f64, 0.0f64}};
    for (uint32_t round = 0u; round < COLLISION_SWEEP_ROUNDS; round++) {
        uint32_t axis = COLLISION_AXES;
        float64_t face = 0.0f64;
        const float64_t time = collision_first_hit(boxes, count, &at, left, &axis, &face);
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            at.min[a] += left[a] * time;
            at.max[a] += left[a] * time;
            left[a] -= left[a] * time;
        }
        if (axis == COLLISION_AXES) {
            break;
        }
        /* Exactly on the face, and not wherever the rounding put it, or the small errors would add up tick after tick. */
        const float64_t size = box->max[axis] - box->min[axis];
        const bool forward = delta[axis] > 0.0f64;
        at.min[axis] = forward ? face - size : face;
        at.max[axis] = forward ? face : face + size;
        left[axis] = 0.0f64;
        if (axis == COLLISION_AXIS_X) {
            out->hit_x = 1u;
        } else if (axis == COLLISION_AXIS_Y) {
            out->hit_y = 1u;
        } else if (forward) {
            out->hit_z_up = 1u;
        } else {
            out->hit_z_down = 1u;
        }
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out->moved[a] = at.min[a] - box->min[a];
    }
}

bool collision_overlaps_any(const collision_box_t *boxes, uint32_t count, const collision_box_t *box) {
    for (uint32_t b = 0u; b < count; b++) {
        bool overlap = true;
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            overlap = overlap && box->max[a] > boxes[b].min[a] + COLLISION_EPSILON &&
                      box->min[a] < boxes[b].max[a] - COLLISION_EPSILON;
        }
        if (overlap) {
            return true;
        }
    }
    return false;
}
