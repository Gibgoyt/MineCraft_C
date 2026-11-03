/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, and never frees. */
#include "mob/skeleton/index.h"

#include "assert/index.h"
#include "item/stack/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <string.h>

#define MOB_SKELETON_PI 3.14159265358979f64
/* Where a thing thrown out of a body starts, above its feet: clear of the floor, wherever the scatter takes it. */
#define MOB_SKELETON_DROP_RISE 0.5f64
/* How far ahead it looks before it steps back or aside. Its box is 0.3 each way, and it stops within a few tenths. */
#define MOB_SKELETON_LOOK_AHEAD 1.0f64
/* A box stopped against a face only touches what is past it. */
#define MOB_SKELETON_TOUCH 1.0e-6f64

void mob_skeleton_start(mob_skeleton_t *skeleton, arena_t *arena, uint64_t seed) {
    memset(skeleton, 0, sizeof(*skeleton));
    skeleton->borrowed_bodies = arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t),
                                                   alignof(mob_skeleton_body_t));
    skeleton->random = terrain_rng_start(seed);
}

void mob_skeleton_seed(mob_skeleton_t *skeleton, uint64_t seed) {
    skeleton->random = terrain_rng_start(seed);
}

uint32_t mob_skeleton_drops(mob_skeleton_t *skeleton, item_drop_store_t *drops, const mob_def_t *def,
                            const float64_t position[COLLISION_AXES]) {
    const float64_t at[COLLISION_AXES] = {position[0], position[1], position[2] + MOB_SKELETON_DROP_RISE};
    uint32_t total = 0u;
    for (uint32_t i = 0u; i < def->drop_count; i++) {
        const mob_drop_t *const row = &mob_drops[def->first_drop + i];
        /* Both rolls always, so that one row's outcome cannot change what the next rolls. */
        const bool lucky = terrain_rng_next_f32(&skeleton->random) < row->chance;
        const uint32_t count =
            (uint32_t)row->count_min +
            terrain_rng_next_below(&skeleton->random, (uint32_t)(row->count_max - row->count_min) + 1u);
        if (lucky && count != 0u) {
            item_drop_spawn(drops, item_stack_drop_row(row->item), count, at);
            total += count;
        }
    }
    skeleton->dropped_count += total;
    return total;
}

/* Whether a body's box could reach into this cell: nothing solid at its feet or its head, no fluid, and ground within reach. */
static bool mob_skeleton_cell_is_safe(const world_map_t *map, int32_t x, int32_t y, int32_t feet) {
    for (int32_t z = feet; z <= feet + 1; z++) {
        uint16_t block = BLOCK_ID_AIR;
        if (!collision_cell_read(map, x, y, z, &block) || (block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u ||
            block_defs[block].shape == BLOCK_SHAPE_FLUID) {
            return false;
        }
    }
    for (uint32_t below = 1u; below <= MOB_SKELETON_STEP_DOWN_CELLS; below++) {
        uint16_t block = BLOCK_ID_AIR;
        if (!collision_cell_read(map, x, y, feet - (int32_t)below, &block) ||
            block_defs[block].shape == BLOCK_SHAPE_FLUID) {
            return false;
        }
        if ((block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u) {
            return true;
        }
    }
    return false;
}

/* Whether a body standing here could step this way: every cell under the corners of its box, as it would be a look ahead. Its middle
 * alone is not enough: going nearly along an edge, its side would be over it long before its middle was. */
static bool mob_skeleton_step_is_safe(const world_map_t *map, const entity_store_t *store, uint32_t index, float64_t x,
                                      float64_t y) {
    const float64_t half = (float64_t)store->borrowed_half_width[index] - MOB_SKELETON_TOUCH;
    const float64_t at_x = store->borrowed_position[0][index] + x * MOB_SKELETON_LOOK_AHEAD;
    const float64_t at_y = store->borrowed_position[1][index] + y * MOB_SKELETON_LOOK_AHEAD;
    const int32_t feet = (int32_t)floor(store->borrowed_position[2][index]);
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        const float64_t cx = (corner & 1u) != 0u ? at_x + half : at_x - half;
        const float64_t cy = (corner & 2u) != 0u ? at_y + half : at_y - half;
        if (!mob_skeleton_cell_is_safe(map, (int32_t)floor(cx), (int32_t)floor(cy), feet)) {
            return false;
        }
    }
    return true;
}

/* The way it most wants to go: along forward, towards the target if above 0, and aside by side, at most a whole speed. It takes the
 * first of that, the same with the other side, and either side alone, that is safe, and stands still if none is. */
static void mob_skeleton_keep_distance(mob_skeleton_body_t *body, entity_store_t *store, const world_map_t *map,
                                       uint32_t index, float64_t speed, float64_t to_x, float64_t to_y,
                                       float64_t forward) {
    for (uint32_t i = 0u; i < 4u; i++) {
        /* Even tries keep the way it was circling, and odd ones turn it round. */
        const float64_t turn = (i & 1u) != 0u ? -1.0f64 : 1.0f64;
        const float64_t along = i < 2u ? forward : 0.0f64;
        const float64_t aside =
            (body->circle_left != 0u ? MOB_SKELETON_CIRCLE_SPEED : -MOB_SKELETON_CIRCLE_SPEED) * turn;
        const float64_t x = to_x * along - to_y * aside;
        const float64_t y = to_y * along + to_x * aside;
        const float64_t length = sqrt(x * x + y * y);
        if (mob_skeleton_step_is_safe(map, store, index, x / length, y / length)) {
            const float64_t scale = (length > 1.0f64 ? 1.0f64 / length : 1.0f64) * speed;
            entity_store_wish_set(store, index, x * scale, y * scale);
            if ((i & 1u) != 0u) {
                body->circle_left = (uint8_t)(body->circle_left ^ 1u);
            }
            return;
        }
    }
    entity_store_wish_set(store, index, 0.0f64, 0.0f64);
}

static void mob_skeleton_shoot(mob_skeleton_t *skeleton, entity_t *entity, const mob_skeleton_view_t *view,
                               uint32_t index, const float64_t eye[COLLISION_AXES]) {
    const float64_t origin[COLLISION_AXES] = {eye[0], eye[1], eye[2] - MOB_SKELETON_MUZZLE_DROP};
    const float64_t dx = view->position[0] - origin[0];
    const float64_t dy = view->position[1] - origin[1];
    const float64_t dz = view->position[2] + view->height * MOB_SKELETON_AIM_AT - origin[2];
    const float64_t across = sqrt(dx * dx + dy * dy);
    const float64_t direction[COLLISION_AXES] = {dx, dy, dz + across * MOB_SKELETON_ARROW_LOB};
    projectile_fire(&entity->projectiles, entity->store.borrowed_handle[index], origin, direction,
                    MOB_SKELETON_ARROW_SPEED, MOB_SKELETON_ARROW_INACCURACY);
    skeleton->shot_count++;
}

/* With nothing to be after, it stands, and now and then sets off for somewhere near. */
static void mob_skeleton_calm(mob_skeleton_t *skeleton, mob_skeleton_body_t *body, mob_path_t *path,
                              entity_store_t *store, uint32_t index, float64_t speed) {
    const entity_handle_t handle = store->borrowed_handle[index];
    store->borrowed_head_yaw[index] = 0.0f32;
    store->borrowed_head_pitch[index] = 0.0f32;
    if (body->strolling == 0u) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        if (terrain_rng_next_below(&skeleton->random, MOB_SKELETON_STROLL_ONE_IN) == 0u) {
            const uint32_t across = 2u * MOB_SKELETON_STROLL_REACH + 1u;
            const float64_t off_x = (float64_t)terrain_rng_next_below(&skeleton->random, across);
            const float64_t off_y = (float64_t)terrain_rng_next_below(&skeleton->random, across);
            const float64_t goal[3] = {
                store->borrowed_position[0][index] + off_x - (float64_t)MOB_SKELETON_STROLL_REACH,
                store->borrowed_position[1][index] + off_y - (float64_t)MOB_SKELETON_STROLL_REACH,
                store->borrowed_position[2][index],
            };
            mob_path_ask_t ask;
            mob_path_ask_for_body(store, index, goal, MOB_SKELETON_STROLL_RADIUS, &ask);
            mob_path_request(path, handle, &ask);
            body->strolling = 1u;
            body->ask_wait = MOB_SKELETON_ASK_TICKS;
        }
        return;
    }
    const mob_path_follow_t follow = mob_path_follow(path, store, handle, speed);
    if (body->ask_wait != 0u) {
        /* Its answer may be on its way, and what it hears now may be the last one's. */
        body->ask_wait--;
        return;
    }
    if (follow != MOB_PATH_FOLLOW_WALKING) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        body->strolling = 0u;
    }
}

/* It has seen what it is after, and has not forgotten. */
static void mob_skeleton_fight(mob_skeleton_t *skeleton, mob_skeleton_body_t *body, mob_path_t *path, entity_t *entity,
                               const world_map_t *map, const mob_skeleton_view_t *view, uint32_t index,
                               const float64_t eye[COLLISION_AXES], float64_t speed, float64_t reach, bool sees) {
    entity_store_t *const store = &entity->store;
    const entity_handle_t handle = store->borrowed_handle[index];
    const float64_t dx = view->position[0] - store->borrowed_position[0][index];
    const float64_t dy = view->position[1] - store->borrowed_position[1][index];
    const float64_t across = sqrt(dx * dx + dy * dy);
    /* Right on top of it, any way out will do. */
    const float64_t to_x = across > 0.0f64 ? dx / across : 1.0f64;
    const float64_t to_y = across > 0.0f64 ? dy / across : 0.0f64;
    const float64_t to_yaw = atan2(to_y, to_x);
    const bool in_range = sees && reach <= MOB_SKELETON_RANGE;

    body->circle++;
    if (body->circle >= MOB_SKELETON_CIRCLE_TICKS) {
        body->circle = 0u;
        if (terrain_rng_next_below(&skeleton->random, 100u) < MOB_SKELETON_CIRCLE_FLIP_PERCENT) {
            body->circle_left = (uint8_t)(body->circle_left ^ 1u);
        }
    }

    if (in_range) {
        const float64_t forward =
            reach < MOB_SKELETON_NEAR ? -1.0f64 : (reach > MOB_SKELETON_FAR ? MOB_SKELETON_COME_ON_SPEED : 0.0f64);
        mob_skeleton_keep_distance(body, store, map, index, speed, to_x, to_y, forward);
        store->borrowed_yaw[index] = (float32_t)to_yaw;
    } else {
        if (body->ask_wait == 0u) {
            mob_path_ask_t ask;
            mob_path_ask_for_body(store, index, view->position, MOB_SKELETON_CHASE_RADIUS, &ask);
            mob_path_request(path, handle, &ask);
            body->ask_wait = MOB_SKELETON_ASK_TICKS;
        } else {
            body->ask_wait--;
        }
        if (mob_path_follow(path, store, handle, speed) == MOB_PATH_FOLLOW_NONE) {
            entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        }
    }
    /* The head looks at it, whichever way the feet are going. */
    const float64_t turn = remainder(to_yaw - (float64_t)store->borrowed_yaw[index], 2.0f64 * MOB_SKELETON_PI);
    store->borrowed_head_yaw[index] = (float32_t)turn;
    store->borrowed_head_pitch[index] = (float32_t)atan2(view->position[2] + view->eye_height - eye[2], across);

    if (body->rest != 0u) {
        body->rest--;
        return;
    }
    if (body->draw < MOB_SKELETON_DRAW_TICKS) {
        body->draw++;
    }
    if (body->draw >= MOB_SKELETON_DRAW_TICKS && in_range) {
        mob_skeleton_shoot(skeleton, entity, view, index, eye);
        body->draw = 0u;
        body->rest = MOB_SKELETON_REST_TICKS;
    }
}

static void mob_skeleton_tick_one(mob_skeleton_t *skeleton, mob_path_t *path, entity_t *entity, const world_map_t *map,
                                  const mob_skeleton_view_t *view, uint32_t index) {
    entity_store_t *const store = &entity->store;
    const entity_handle_t handle = store->borrowed_handle[index];
    mob_skeleton_body_t *const body = &skeleton->borrowed_bodies[entity_handle_slot(handle)];
    if (body->owner != handle) {
        memset(body, 0, sizeof(*body));
        body->owner = handle;
    }
    const mob_def_t *const def = &mob_defs[MOB_ID_SKELETON];
    const float64_t position[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                                store->borrowed_position[2][index]};
    if (!(store->borrowed_health[index] > 0.0f32)) {
        (void)mob_skeleton_drops(skeleton, &entity->item_drops, def, position);
        ASSERT_ALWAYS(entity_store_remove(store, handle));
        skeleton->died_count++;
        return;
    }
    const uint8_t flags = store->borrowed_flags[index];
    if ((flags & ENTITY_STORE_FLAG_WAITING) != 0u) {
        return;
    }
    const float64_t eye[COLLISION_AXES] = {position[0], position[1],
                                           position[2] + (float64_t)store->borrowed_height[index] * MOB_SKELETON_EYE};
    float64_t reach = 0.0f64;
    bool sees = false;
    if (view->alive != 0u) {
        const float64_t dx = view->position[0] - position[0];
        const float64_t dy = view->position[1] - position[1];
        const float64_t dz = view->position[2] - position[2];
        reach = sqrt(dx * dx + dy * dy + dz * dz);
        if (reach <= MOB_SKELETON_SIGHT) {
            const float64_t target_eye[COLLISION_AXES] = {view->position[0], view->position[1],
                                                          view->position[2] + view->eye_height};
            sees = projectile_line_is_clear(map, eye, target_eye);
        }
    }
    if (sees) {
        body->unseen = 0u;
        body->engaged = 1u;
        body->strolling = 0u;
    } else if (body->engaged != 0u) {
        body->unseen++;
        if (view->alive == 0u || reach > MOB_SKELETON_SIGHT || body->unseen >= MOB_SKELETON_FORGET_TICKS) {
            body->engaged = 0u;
            body->draw = 0u;
            body->ask_wait = 0u;
        }
    }
    /* It keeps its head above water, or it would walk the bottom shooting at the surface. */
    if ((flags & ENTITY_STORE_FLAG_IN_WATER) != 0u) {
        entity_store_jump(store, index);
    }
    if (body->engaged != 0u) {
        mob_skeleton_fight(skeleton, body, path, entity, map, view, index, eye, (float64_t)def->speed, reach, sees);
    } else {
        mob_skeleton_calm(skeleton, body, path, store, index, (float64_t)def->speed);
    }
    const float32_t aim_wanted = body->engaged != 0u ? 1.0f32 : 0.0f32;
    store->borrowed_aim[index] += (aim_wanted - store->borrowed_aim[index]) * MOB_SKELETON_AIM_EASE;
}

void mob_skeleton_tick(mob_skeleton_t *skeleton, mob_path_t *path, entity_t *entity, const world_map_t *map,
                       const mob_skeleton_view_t *view) {
    entity_store_t *const store = &entity->store;
    /* From the end, since a removal moves the last body into the gap. */
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_kind[i] == (uint16_t)MOB_ID_SKELETON) {
            mob_skeleton_tick_one(skeleton, path, entity, map, view, i);
        }
    }
}
