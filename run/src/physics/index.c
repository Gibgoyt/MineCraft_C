/* See README.md. The Owner: it holds the region the part's boxes come from, and decides when they die. */
#include "physics/index.h"

#include "assert/index.h"

#include <math.h>
#include <string.h>

enum : uint64_t {
    /* The cells a sweep can touch: the largest box, plus the largest step, plus a cell at each end, on each axis. */
    PHYSICS_CELLS_ACROSS = 8ull + 8ull + 2ull,
    PHYSICS_BOXES_MAX = PHYSICS_CELLS_ACROSS * PHYSICS_CELLS_ACROSS * PHYSICS_CELLS_ACROSS * COLLISION_BOXES_PER_BLOCK,
    PHYSICS_REGION_BYTES = 2ull << 20,
};
static_assert(PHYSICS_BOXES_MAX * sizeof(collision_box_t) + alignof(collision_box_t) <= PHYSICS_REGION_BYTES);

bool physics_init(physics_t *physics) {
    memset(physics, 0, sizeof(*physics));
    if (!heap_reserve(PHYSICS_REGION_BYTES, &physics->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&physics->arena_boxes, "physics_boxes", physics->owned_region.owned_base, PHYSICS_REGION_BYTES);
    return true;
}

void physics_deinit(physics_t *physics) {
    arena_deinit(&physics->arena_boxes);
    heap_release(&physics->owned_region);
    memset(physics, 0, sizeof(*physics));
}

static bool physics_box_fits(const collision_box_t *box) {
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (!(box->max[a] - box->min[a] <= PHYSICS_BOX_SIZE_MAX)) {
            return false;
        }
    }
    return true;
}

/* One sweep short enough for the region to hold every box it could meet. */
static void physics_step(physics_t *physics, const world_map_t *map, const collision_box_t *box,
                         const float64_t delta[COLLISION_AXES], collision_move_t *out) {
    collision_box_t swept = *box;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        swept.min[a] += delta[a] < 0.0f64 ? delta[a] : 0.0f64;
        swept.max[a] += delta[a] > 0.0f64 ? delta[a] : 0.0f64;
    }
    arena_reset(&physics->arena_boxes);
    uint32_t count = 0u;
    const collision_box_t *const boxes = collision_gather(&physics->arena_boxes, map, &swept, &count);
    collision_sweep(boxes, count, box, delta, out);
}

void physics_move(physics_t *physics, const world_map_t *map, const collision_box_t *box,
                  const float64_t delta[COLLISION_AXES], collision_move_t *out) {
    ASSERT_ALWAYS(physics_box_fits(box));
    float64_t longest = 0.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        const float64_t length = fabs(delta[a]);
        ASSERT_ALWAYS(isfinite(length) && length <= PHYSICS_MOVE_BLOCKS_MAX);
        longest = length > longest ? length : longest;
    }
    const uint32_t steps = longest <= PHYSICS_STEP_BLOCKS ? 1u : (uint32_t)ceil(longest / PHYSICS_STEP_BLOCKS);
    *out = (collision_move_t){.moved = {0.0f64, 0.0f64, 0.0f64}};
    collision_box_t at = *box;
    float64_t piece[COLLISION_AXES] = {delta[0] / (float64_t)steps, delta[1] / (float64_t)steps,
                                       delta[2] / (float64_t)steps};
    for (uint32_t s = 0u; s < steps; s++) {
        collision_move_t step;
        physics_step(physics, map, &at, piece, &step);
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            at.min[a] += step.moved[a];
            at.max[a] += step.moved[a];
        }
        out->hit_x |= step.hit_x;
        out->hit_y |= step.hit_y;
        out->hit_z_down |= step.hit_z_down;
        out->hit_z_up |= step.hit_z_up;
        /* A later piece may not go on where this one was stopped. */
        piece[COLLISION_AXIS_X] = step.hit_x != 0u ? 0.0f64 : piece[COLLISION_AXIS_X];
        piece[COLLISION_AXIS_Y] = step.hit_y != 0u ? 0.0f64 : piece[COLLISION_AXIS_Y];
        piece[COLLISION_AXIS_Z] = step.hit_z_down != 0u || step.hit_z_up != 0u ? 0.0f64 : piece[COLLISION_AXIS_Z];
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        out->moved[a] = at.min[a] - box->min[a];
    }
}

bool physics_box_is_clear(physics_t *physics, const world_map_t *map, const collision_box_t *box) {
    ASSERT_ALWAYS(physics_box_fits(box));
    arena_reset(&physics->arena_boxes);
    uint32_t count = 0u;
    const collision_box_t *const boxes = collision_gather(&physics->arena_boxes, map, box, &count);
    return !collision_overlaps_any(boxes, count, box);
}
