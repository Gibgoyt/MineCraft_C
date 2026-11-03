/* See README.md. The Arena tier: its array comes once from the arena its Owner gives it, and a tick takes nothing. */
#include "entity/projectile/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <string.h>

/* Past the end of any segment, which runs from 0 to 1. */
#define PROJECTILE_NEVER 2.0f64

typedef enum projectile_cast_t : uint8_t {
    PROJECTILE_CAST_CLEAR = 0,
    PROJECTILE_CAST_BLOCK = 1,
    PROJECTILE_CAST_NOT_LOADED = 2,
} projectile_cast_t;

void projectile_store_start(projectile_store_t *store, arena_t *arena, uint64_t seed) {
    memset(store, 0, sizeof(*store));
    store->borrowed_items = arena_alloc_array(arena, PROJECTILE_CAPACITY, sizeof(projectile_t), alignof(projectile_t));
    store->random = terrain_rng_start(seed);
}

/* The last one takes its place, so a walk that removes any must go from the end. */
static void projectile_remove(projectile_store_t *store, uint32_t index) {
    store->count--;
    store->borrowed_items[index] = store->borrowed_items[store->count];
}

/* What is left lying is worth the least, and of those what has lain longest. */
static void projectile_evict(projectile_store_t *store) {
    uint32_t worst = 0u;
    for (uint32_t i = 1u; i < store->count; i++) {
        const projectile_t *const item = &store->borrowed_items[i];
        const projectile_t *const best = &store->borrowed_items[worst];
        if (item->stuck > best->stuck || (item->stuck == best->stuck && item->age > best->age)) {
            worst = i;
        }
    }
    store->lost_count++;
    projectile_remove(store, worst);
}

/* The published shape: most shots go near where they were aimed. */
static float64_t projectile_astray(terrain_rng_t *random, float64_t reach) {
    return (terrain_rng_next_f64(random) - terrain_rng_next_f64(random)) * reach;
}

void projectile_fire(projectile_store_t *store, entity_handle_t shooter, const float64_t origin[COLLISION_AXES],
                     const float64_t direction[COLLISION_AXES], float64_t speed, float64_t inaccuracy) {
    const float64_t length =
        sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    ASSERT_ALWAYS(length > 0.0f64 && isfinite(length));
    if (store->count == PROJECTILE_CAPACITY) {
        projectile_evict(store);
    }
    projectile_t *const arrow = &store->borrowed_items[store->count];
    store->count++;
    store->fired_count++;
    memset(arrow, 0, sizeof(*arrow));
    const float64_t reach = inaccuracy * PROJECTILE_SPREAD;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        ASSERT_ALWAYS(isfinite(origin[a]));
        arrow->position[a] = origin[a];
        arrow->velocity[a] = (direction[a] / length + projectile_astray(&store->random, reach)) * speed;
    }
    arrow->shooter = shooter;
}

/* ---- Segments ------------------------------------------------------------------------------------------------------ */

/* Where along from + t * delta, with t from 0 to 1, a segment comes into a box, 0 if it starts inside. */
static bool projectile_segment_box(const float64_t from[COLLISION_AXES], const float64_t delta[COLLISION_AXES],
                                   const collision_box_t *box, float64_t *out_t) {
    float64_t enter = 0.0f64, leave = 1.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        if (delta[a] == 0.0f64) {
            if (from[a] < box->min[a] || from[a] > box->max[a]) {
                return false;
            }
            continue;
        }
        const float64_t to_min = (box->min[a] - from[a]) / delta[a];
        const float64_t to_max = (box->max[a] - from[a]) / delta[a];
        const float64_t low = to_min < to_max ? to_min : to_max;
        const float64_t high = to_min < to_max ? to_max : to_min;
        enter = low > enter ? low : enter;
        leave = high < leave ? high : leave;
        if (enter > leave) {
            return false;
        }
    }
    *out_t = enter;
    return true;
}

/* The first box of a solid block along a segment, by walking its cells in order. Plants and fluids have none. */
static projectile_cast_t projectile_cast_blocks(const world_map_t *map, const float64_t from[COLLISION_AXES],
                                                const float64_t delta[COLLISION_AXES], float64_t *out_t,
                                                int32_t out_cell[COLLISION_AXES]) {
    int32_t cell[COLLISION_AXES], step[COLLISION_AXES];
    float64_t next[COLLISION_AXES], each[COLLISION_AXES];
    float64_t cells = 1.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        const float64_t at = floor(from[a]);
        cell[a] = (int32_t)at;
        step[a] = delta[a] > 0.0f64 ? 1 : -1;
        cells += fabs(floor(from[a] + delta[a]) - at);
        if (delta[a] == 0.0f64) {
            next[a] = PROJECTILE_NEVER;
            each[a] = 0.0f64;
        } else {
            const float64_t edge = delta[a] > 0.0f64 ? at + 1.0f64 : at;
            next[a] = (edge - from[a]) / delta[a];
            each[a] = fabs(1.0f64 / delta[a]);
        }
    }
    for (uint32_t i = 0u; i < (uint32_t)cells; i++) {
        uint16_t block = BLOCK_ID_AIR;
        if (!collision_cell_read(map, cell[0], cell[1], cell[2], &block)) {
            return PROJECTILE_CAST_NOT_LOADED;
        }
        collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
        const uint32_t count = collision_block_boxes(block, cell[0], cell[1], cell[2], boxes);
        float64_t best = PROJECTILE_NEVER;
        for (uint32_t b = 0u; b < count; b++) {
            float64_t t = 0.0f64;
            if (projectile_segment_box(from, delta, &boxes[b], &t) && t < best) {
                best = t;
            }
        }
        if (best <= 1.0f64) {
            *out_t = best;
            memcpy(out_cell, cell, sizeof(cell));
            return PROJECTILE_CAST_BLOCK;
        }
        const uint32_t axis = next[0] <= next[1] ? (next[0] <= next[2] ? 0u : 2u) : (next[1] <= next[2] ? 1u : 2u);
        cell[axis] += step[axis];
        next[axis] += each[axis];
    }
    return PROJECTILE_CAST_CLEAR;
}

bool projectile_line_is_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                              const float64_t to[COLLISION_AXES]) {
    const float64_t delta[COLLISION_AXES] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    float64_t t = 0.0f64;
    int32_t cell[COLLISION_AXES];
    return projectile_cast_blocks(map, from, delta, &t, cell) == PROJECTILE_CAST_CLEAR;
}

/* ---- One arrow's tick ---------------------------------------------------------------------------------------------- */

static void projectile_inflate(collision_box_t *box, float64_t by) {
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        box->min[a] -= by;
        box->max[a] += by;
    }
}

void projectile_knockback(float64_t velocity[COLLISION_AXES], const float64_t direction[COLLISION_AXES],
                          bool on_ground) {
    velocity[COLLISION_AXIS_X] =
        velocity[COLLISION_AXIS_X] / 2.0f64 + direction[COLLISION_AXIS_X] * PROJECTILE_KNOCKBACK;
    velocity[COLLISION_AXIS_Y] =
        velocity[COLLISION_AXIS_Y] / 2.0f64 + direction[COLLISION_AXIS_Y] * PROJECTILE_KNOCKBACK;
    if (on_ground) {
        const float64_t up = velocity[COLLISION_AXIS_Z] / 2.0f64 + PROJECTILE_KNOCKBACK;
        velocity[COLLISION_AXIS_Z] = up < PROJECTILE_KNOCKBACK ? up : PROJECTILE_KNOCKBACK;
    }
}

/* The first living body on a segment that comes before best_t, and how far along it. Its shooter is never one. */
static entity_handle_t projectile_first_body(const projectile_t *arrow, entity_store_t *bodies, entity_grid_t *grid,
                                             float64_t *best_t) {
    collision_box_t around;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        const float64_t end = arrow->position[a] + arrow->velocity[a];
        around.min[a] = (arrow->position[a] < end ? arrow->position[a] : end) - PROJECTILE_BODY_MARGIN;
        around.max[a] = (arrow->position[a] < end ? end : arrow->position[a]) + PROJECTILE_BODY_MARGIN;
    }
    entity_handle_t found[PROJECTILE_BODIES_MAX];
    const uint32_t total = entity_grid_query(grid, bodies, &around, found, PROJECTILE_BODIES_MAX);
    const uint32_t count = total < PROJECTILE_BODIES_MAX ? total : PROJECTILE_BODIES_MAX;
    entity_handle_t first = ENTITY_HANDLE_NONE;
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t index = 0u;
        if (found[i] == arrow->shooter || !entity_store_index_of(bodies, found[i], &index) ||
            !(bodies->borrowed_health[index] > 0.0f32)) {
            continue;
        }
        collision_box_t box;
        entity_store_box(bodies, index, &box);
        projectile_inflate(&box, PROJECTILE_BODY_MARGIN);
        float64_t t = 0.0f64;
        if (projectile_segment_box(arrow->position, arrow->velocity, &box, &t) && t < *best_t) {
            *best_t = t;
            first = found[i];
        }
    }
    return first;
}

/* What a hit comes to at this speed, and which way it was going, along x and y. */
static float32_t projectile_hit_size(const float64_t velocity[COLLISION_AXES], float64_t out[COLLISION_AXES]) {
    const float64_t across = velocity[0] * velocity[0] + velocity[1] * velocity[1];
    const float64_t speed = sqrt(across + velocity[2] * velocity[2]);
    const float64_t side = sqrt(across);
    out[0] = side > 0.0f64 ? velocity[0] / side : 0.0f64;
    out[1] = side > 0.0f64 ? velocity[1] / side : 0.0f64;
    out[2] = 0.0f64;
    return (float32_t)ceil(speed * PROJECTILE_DAMAGE);
}

static void projectile_hurt_body(projectile_store_t *store, entity_store_t *bodies, const projectile_t *arrow,
                                 entity_handle_t handle) {
    uint32_t index = 0u;
    ASSERT_ALWAYS(entity_store_index_of(bodies, handle, &index));
    float64_t direction[COLLISION_AXES];
    bodies->borrowed_health[index] -= projectile_hit_size(arrow->velocity, direction);
    float64_t velocity[COLLISION_AXES];
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        velocity[a] = bodies->borrowed_velocity[a][index];
    }
    projectile_knockback(velocity, direction, (bodies->borrowed_flags[index] & ENTITY_STORE_FLAG_ON_GROUND) != 0u);
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        bodies->borrowed_velocity[a][index] = velocity[a];
    }
    store->body_hits++;
}

/* In the block it hit, just short of the face. */
static void projectile_stick(projectile_store_t *store, projectile_t *arrow, float64_t t,
                             const int32_t cell[COLLISION_AXES]) {
    const float64_t *const velocity = arrow->velocity;
    const float64_t length = sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    const float64_t travelled = t * length;
    const float64_t kept = travelled > PROJECTILE_BACK_OFF ? (travelled - PROJECTILE_BACK_OFF) / length : 0.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        arrow->position[a] += arrow->velocity[a] * kept;
        arrow->velocity[a] = 0.0f64;
    }
    memcpy(arrow->stuck_cell, cell, sizeof(arrow->stuck_cell));
    arrow->stuck = 1u;
    arrow->age = 0u;
    store->stuck_count++;
}

static void projectile_tick_stuck(projectile_store_t *store, const world_map_t *map, uint32_t index) {
    projectile_t *const arrow = &store->borrowed_items[index];
    uint16_t block = BLOCK_ID_AIR;
    if (!collision_cell_read(map, arrow->stuck_cell[0], arrow->stuck_cell[1], arrow->stuck_cell[2], &block)) {
        return;
    }
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    if (collision_block_boxes(block, arrow->stuck_cell[0], arrow->stuck_cell[1], arrow->stuck_cell[2], boxes) == 0u) {
        /* What it was in has gone, so it falls. */
        arrow->stuck = 0u;
        arrow->age = 0u;
        return;
    }
    arrow->age++;
    if (arrow->age >= PROJECTILE_STUCK_TICKS) {
        store->expired_count++;
        projectile_remove(store, index);
    }
}

static void projectile_tick_flying(projectile_store_t *store, entity_store_t *bodies, entity_grid_t *grid,
                                   const world_map_t *map, const collision_box_t *target, projectile_hurt_t *hurt,
                                   void *borrowed_context, uint32_t index) {
    projectile_t *const arrow = &store->borrowed_items[index];
    uint16_t here = BLOCK_ID_AIR;
    if (!collision_cell_read(map, (int32_t)floor(arrow->position[0]), (int32_t)floor(arrow->position[1]),
                             (int32_t)floor(arrow->position[2]), &here)) {
        return;
    }
    if (here == BLOCK_ID_LAVA) {
        store->burned_count++;
        projectile_remove(store, index);
        return;
    }
    float64_t block_t = 0.0f64;
    int32_t block_cell[COLLISION_AXES];
    const projectile_cast_t cast = projectile_cast_blocks(map, arrow->position, arrow->velocity, &block_t, block_cell);
    if (cast == PROJECTILE_CAST_NOT_LOADED) {
        return;
    }
    const bool hit_block = cast == PROJECTILE_CAST_BLOCK;
    float64_t best = hit_block ? block_t : PROJECTILE_NEVER;
    const entity_handle_t body = projectile_first_body(arrow, bodies, grid, &best);
    if (target != nullptr) {
        collision_box_t box = *target;
        projectile_inflate(&box, PROJECTILE_BODY_MARGIN);
        float64_t t = 0.0f64;
        if (projectile_segment_box(arrow->position, arrow->velocity, &box, &t) && t < best) {
            float64_t direction[COLLISION_AXES];
            const float32_t amount = projectile_hit_size(arrow->velocity, direction);
            store->target_hits++;
            projectile_remove(store, index);
            hurt(borrowed_context, amount, direction);
            return;
        }
    }
    if (body != ENTITY_HANDLE_NONE) {
        projectile_hurt_body(store, bodies, arrow, body);
        projectile_remove(store, index);
        return;
    }
    if (hit_block) {
        projectile_stick(store, arrow, block_t, block_cell);
        return;
    }
    arrow->age++;
    if (arrow->age >= PROJECTILE_FLIGHT_TICKS) {
        store->expired_count++;
        projectile_remove(store, index);
        return;
    }
    const float64_t drag = here == BLOCK_ID_WATER ? PROJECTILE_WATER_DRAG : PROJECTILE_DRAG;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        arrow->position[a] += arrow->velocity[a];
        arrow->velocity[a] *= drag;
    }
    arrow->velocity[COLLISION_AXIS_Z] -= PROJECTILE_GRAVITY;
}

void projectile_tick(projectile_store_t *store, entity_store_t *bodies, entity_grid_t *grid, const world_map_t *map,
                     const collision_box_t *target, projectile_hurt_t *hurt, void *borrowed_context) {
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_items[i].stuck != 0u) {
            projectile_tick_stuck(store, map, i);
        } else {
            projectile_tick_flying(store, bodies, grid, map, target, hurt, borrowed_context, i);
        }
    }
}
