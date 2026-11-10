/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, and never frees. */
#include "mob/zombie/index.h"

#include "entity/animation/index.h"
#include "item/stack/index.h"
#include "physics/collision/index.h"
#include "tables/mobs/index.h"

#include <math.h>
#include <string.h>

#define MOB_ZOMBIE_TWO_PI 6.28318530717959f64
/* A ray's step that never comes: further than any line asked about, in that line's own length. */
#define MOB_ZOMBIE_NEVER 1.0e30f64
/* What it leaves starts this far above its feet, and clear of the floor they stand on. */
#define MOB_ZOMBIE_DROP_ABOVE 0.5f64

void mob_zombie_start(mob_zombie_t *zombie, arena_t *arena, uint64_t seed) {
    memset(zombie, 0, sizeof(*zombie));
    zombie->borrowed_bodies =
        arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t), 64u);
    zombie->random = terrain_rng_start(seed);
}

/* Whether the line, from + d * t for t from 0 to 1, goes into any box of this cell's block. */
static bool mz_cell_is_clear(const world_map_t *map, const int32_t cell[COLLISION_AXES],
                             const float64_t from[COLLISION_AXES], const float64_t d[COLLISION_AXES]) {
    uint16_t block = 0u;
    if (!collision_cell_read_voxel(map, cell[0], cell[1], cell[2], &block)) {
        return false;
    }
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    const uint32_t count = collision_block_boxes(block, cell[0], cell[1], cell[2], boxes);
    for (uint32_t b = 0u; b < count; b++) {
        float64_t enter = 0.0f64, leave = 1.0f64;
        for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
            if (d[axis] == 0.0f64) {
                if (from[axis] <= boxes[b].min[axis] || from[axis] >= boxes[b].max[axis]) {
                    leave = -1.0f64;
                }
                continue;
            }
            const float64_t a = (boxes[b].min[axis] - from[axis]) / d[axis];
            const float64_t c = (boxes[b].max[axis] - from[axis]) / d[axis];
            const float64_t near = a < c ? a : c;
            const float64_t far = a < c ? c : a;
            enter = near > enter ? near : enter;
            leave = far < leave ? far : leave;
        }
        if (enter < leave) {
            return false;
        }
    }
    return true;
}

bool mob_zombie_line_is_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                              const float64_t to[COLLISION_AXES]) {
    float64_t d[COLLISION_AXES], next[COLLISION_AXES], each[COLLISION_AXES];
    int32_t cell[COLLISION_AXES], step[COLLISION_AXES];
    uint64_t cells = 1u;
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        d[axis] = to[axis] - from[axis];
        cell[axis] = (int32_t)floor(from[axis]);
        const int32_t last = (int32_t)floor(to[axis]);
        cells += (uint64_t)(last > cell[axis] ? last - cell[axis] : cell[axis] - last);
        step[axis] = last > cell[axis] ? 1 : -1;
        if (d[axis] == 0.0f64) {
            next[axis] = MOB_ZOMBIE_NEVER;
            each[axis] = MOB_ZOMBIE_NEVER;
        } else if (d[axis] > 0.0f64) {
            next[axis] = (floor(from[axis]) + 1.0f64 - from[axis]) / d[axis];
            each[axis] = 1.0f64 / d[axis];
        } else {
            next[axis] = (floor(from[axis]) - from[axis]) / d[axis];
            each[axis] = -1.0f64 / d[axis];
        }
    }
    /* Exactly the cells between its two ends, each crossing once: rounding cannot make it wander past the last. */
    for (uint64_t walked = 0u; walked < cells; walked++) {
        if (!mz_cell_is_clear(map, cell, from, d)) {
            return false;
        }
        uint32_t axis = next[0] < next[1] ? 0u : 1u;
        axis = next[2] < next[axis] ? 2u : axis;
        cell[axis] += step[axis];
        next[axis] += each[axis];
    }
    return true;
}

static bool mz_sees(const world_map_t *map, const entity_store_t *store, uint32_t index,
                    const mob_zombie_view_t *view) {
    const float64_t eye[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                           store->borrowed_position[2][index] + MOB_ZOMBIE_EYE};
    const float64_t target[COLLISION_AXES] = {view->position[0], view->position[1], view->position[2] + view->eye};
    return mob_zombie_line_is_clear(map, eye, target);
}

bool mob_zombie_in_reach(const entity_store_t *store, uint32_t index, const mob_zombie_view_t *view) {
    collision_box_t box;
    entity_store_box(store, index, &box);
    return box.min[0] - MOB_ZOMBIE_ATTACK_REACH < view->position[0] + view->half_width &&
           box.max[0] + MOB_ZOMBIE_ATTACK_REACH > view->position[0] - view->half_width &&
           box.min[1] - MOB_ZOMBIE_ATTACK_REACH < view->position[1] + view->half_width &&
           box.max[1] + MOB_ZOMBIE_ATTACK_REACH > view->position[1] - view->half_width &&
           box.min[2] < view->position[2] + view->height && box.max[2] > view->position[2];
}

/* Each row rolled on its own, as config/mobs/README.md says. Every row draws the same numbers, whatever came of those before. */
static void mz_die(mob_zombie_t *zombie, entity_store_t *store, item_drop_store_t *drops, uint32_t index) {
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    const float64_t at[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                          store->borrowed_position[2][index] + MOB_ZOMBIE_DROP_ABOVE};
    for (uint32_t row = 0u; row < def->drop_count; row++) {
        const mob_drop_t *const drop = &mob_drops[def->first_drop + row];
        const bool lucky = terrain_rng_next_f32(&zombie->random) < drop->chance;
        const uint32_t count =
            drop->count_min + terrain_rng_next_below(&zombie->random, (uint32_t)drop->count_max - drop->count_min + 1u);
        if (lucky && count != 0u) {
            item_drop_spawn(drops, item_stack_drop_row(drop->item), count, at);
            zombie->dropped_count += count;
        }
    }
    zombie->died_count++;
    (void)entity_store_remove(store, store->borrowed_handle[index]);
}

static float32_t mz_clamp(float64_t value, float64_t most) {
    return (float32_t)(value < -most ? -most : (value > most ? most : value));
}

/* Its head turns to what it chases, as far as a neck goes. */
static void mz_look(entity_store_t *store, uint32_t index, const mob_zombie_view_t *view) {
    const float64_t dx = view->position[0] - store->borrowed_position[0][index];
    const float64_t dy = view->position[1] - store->borrowed_position[1][index];
    const float64_t dz = view->position[2] + view->eye - (store->borrowed_position[2][index] + MOB_ZOMBIE_EYE);
    const float64_t turn = remainder(atan2(dy, dx) - (float64_t)store->borrowed_yaw[index], MOB_ZOMBIE_TWO_PI);
    store->borrowed_head_yaw[index] = mz_clamp(turn, MOB_ZOMBIE_HEAD_YAW_MAX);
    store->borrowed_head_pitch[index] = mz_clamp(atan2(dz, sqrt(dx * dx + dy * dy)), MOB_ZOMBIE_HEAD_PITCH_MAX);
}

static void mz_give_up(mob_zombie_body_t *body, entity_store_t *store, uint32_t index) {
    body->chasing = 0u;
    body->stroll = 0u;
    entity_store_wish_set(store, index, 0.0f64, 0.0f64);
    store->borrowed_head_yaw[index] = 0.0f32;
    store->borrowed_head_pitch[index] = 0.0f32;
}

/* Walks the way it last got, and asks for a new one when that has gone stale and its turn has come. */
static void mz_walk(mob_zombie_t *zombie, mob_path_t *path, entity_store_t *store, uint32_t index,
                    mob_zombie_body_t *body, const mob_zombie_view_t *view, float64_t distance_squared) {
    const entity_handle_t handle = store->borrowed_handle[index];
    const mob_path_follow_t follow = mob_path_follow(path, store, handle, (float64_t)mob_defs[MOB_ID_ZOMBIE].speed);
    if (follow == MOB_PATH_FOLLOW_NONE) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
    }
    if (body->repath != 0u) {
        return;
    }
    const float64_t mx = view->position[0] - body->asked[0];
    const float64_t my = view->position[1] - body->asked[1];
    const float64_t mz = view->position[2] - body->asked[2];
    if (mx * mx + my * my + mz * mz < 1.0f64 && follow == MOB_PATH_FOLLOW_WALKING) {
        return;
    }
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, view->position, 0u, &ask);
    mob_path_request(path, handle, &ask);
    memcpy(body->asked, view->position, sizeof(body->asked));
    uint32_t wait = MOB_ZOMBIE_REPATH_MIN + terrain_rng_next_below(&zombie->random, MOB_ZOMBIE_REPATH_SPREAD);
    wait += distance_squared > 1024.0f64  ? MOB_ZOMBIE_REPATH_FARTHER
            : distance_squared > 256.0f64 ? MOB_ZOMBIE_REPATH_FAR
                                          : 0u;
    /* A place it cannot get to costs a whole search, and it would be asked for again and again. */
    wait += follow == MOB_PATH_FOLLOW_ENDED || follow == MOB_PATH_FOLLOW_STUCK ? MOB_ZOMBIE_REPATH_FAILED : 0u;
    body->repath = (uint16_t)wait;
}

/* With nothing to chase: it stands, and now and then sets off for somewhere near. Its own way, by the same paths, and never a long search. */
static void mz_stroll(mob_zombie_t *zombie, mob_path_t *path, entity_store_t *store, uint32_t index,
                      mob_zombie_body_t *body) {
    if (body->stroll != 0u) {
        body->stroll--;
        const mob_path_follow_t follow =
            mob_path_follow(path, store, store->borrowed_handle[index], (float64_t)mob_defs[MOB_ID_ZOMBIE].speed);
        /* None is an answer on its way. Any other but walking is the end of this one, and the wish is already 0. */
        if (follow == MOB_PATH_FOLLOW_NONE) {
            entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        } else if (follow != MOB_PATH_FOLLOW_WALKING) {
            body->stroll = 0u;
        }
        if (body->stroll == 0u) {
            entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        }
        return;
    }
    if (terrain_rng_next_below(&zombie->random, MOB_ZOMBIE_STROLL_ONE_IN) != 0u) {
        return;
    }
    const uint32_t across = 2u * MOB_ZOMBIE_STROLL_ACROSS + 1u, up = 2u * MOB_ZOMBIE_STROLL_UP + 1u;
    const float64_t goal[COLLISION_AXES] = {
        store->borrowed_position[0][index] + (float64_t)terrain_rng_next_below(&zombie->random, across) -
            (float64_t)MOB_ZOMBIE_STROLL_ACROSS,
        store->borrowed_position[1][index] + (float64_t)terrain_rng_next_below(&zombie->random, across) -
            (float64_t)MOB_ZOMBIE_STROLL_ACROSS,
        store->borrowed_position[2][index] + (float64_t)terrain_rng_next_below(&zombie->random, up) -
            (float64_t)MOB_ZOMBIE_STROLL_UP,
    };
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, goal, 0u, &ask);
    ask.nodes = (uint16_t)MOB_ZOMBIE_STROLL_NODES;
    mob_path_request(path, store->borrowed_handle[index], &ask);
    body->stroll = (uint16_t)MOB_ZOMBIE_STROLL_TICKS;
}

/* One tick of one zombie, which is alive and has its column. True if it struck. */
static bool mz_think(mob_zombie_t *zombie, mob_path_t *path, entity_store_t *store, const world_map_t *map,
                     uint32_t index, mob_zombie_body_t *body, const mob_zombie_view_t *view) {
    body->cooldown = body->cooldown != 0u ? (uint8_t)(body->cooldown - 1u) : 0u;
    body->repath = body->repath != 0u ? (uint16_t)(body->repath - 1u) : 0u;

    const float64_t dx = view->position[0] - store->borrowed_position[0][index];
    const float64_t dy = view->position[1] - store->borrowed_position[1][index];
    const float64_t dz = view->position[2] - store->borrowed_position[2][index];
    const float64_t distance_squared = dx * dx + dy * dy + dz * dz;
    const bool in_range = view->alive != 0u && distance_squared <= MOB_ZOMBIE_FOLLOW_RANGE * MOB_ZOMBIE_FOLLOW_RANGE;

    if (body->chasing == 0u) {
        if (!in_range || fabs(dz) > MOB_ZOMBIE_NOTICE_UP ||
            terrain_rng_next_below(&zombie->random, MOB_ZOMBIE_NOTICE_ONE_IN) != 0u ||
            !mz_sees(map, store, index, view)) {
            mz_stroll(zombie, path, store, index, body);
            return false;
        }
        body->chasing = 1u;
        body->stroll = 0u;
        body->unseen = 0u;
        body->repath = 0u;
        body->cooldown = 0u;
    } else if (!in_range) {
        mz_give_up(body, store, index);
        return false;
    } else if ((store->borrowed_age[index] + entity_handle_slot(store->borrowed_handle[index])) %
                   MOB_ZOMBIE_SIGHT_EVERY ==
               0u) {
        body->unseen = mz_sees(map, store, index, view) ? 0u : (uint16_t)(body->unseen + MOB_ZOMBIE_SIGHT_EVERY);
        if (body->unseen > MOB_ZOMBIE_UNSEEN_TICKS) {
            mz_give_up(body, store, index);
            return false;
        }
    }

    const bool reach = mob_zombie_in_reach(store, index, view);
    if (reach && body->unseen == 0u) {
        /* Close enough: it stands there, and does not walk into what it hits. */
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        store->borrowed_yaw[index] = (float32_t)atan2(dy, dx);
    } else {
        mz_walk(zombie, path, store, index, body, view, distance_squared);
    }
    mz_look(store, index, view);
    /* Sight looked at again, as what it has in mind may be some ticks old and something may have been built since. */
    if (!reach || body->cooldown != 0u || !mz_sees(map, store, index, view)) {
        return false;
    }
    body->cooldown = MOB_ZOMBIE_ATTACK_TICKS;
    entity_animation_swing(store, index);
    zombie->struck_count++;
    return true;
}

float32_t mob_zombie_tick(mob_zombie_t *zombie, mob_path_t *path, entity_store_t *store, item_drop_store_t *drops,
                          const world_map_t *map, const mob_zombie_view_t *view) {
    float32_t blow = 0.0f32;
    /* From the end, as a death moves the last body into its place, and that one has had its turn. */
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_kind[i] != (uint16_t)MOB_ID_ZOMBIE) {
            continue;
        }
        const entity_handle_t handle = store->borrowed_handle[i];
        mob_zombie_body_t *const body = &zombie->borrowed_bodies[entity_handle_slot(handle)];
        if (body->owner != handle) {
            /* A slot used again starts over: this one has never struck or seen anything. */
            memset(body, 0, sizeof(*body));
            body->owner = handle;
        }
        if (store->borrowed_health[i] <= 0.0f32) {
            mz_die(zombie, store, drops, i);
            continue;
        }
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        if (mz_think(zombie, path, store, map, i, body, view)) {
            blow = MOB_ZOMBIE_DAMAGE;
        }
    }
    return blow;
}
