/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, once, and never frees. */
#include "mob/creeper/index.h"

#include "item/stack/index.h"
#include "tables/mobs/index.h"

#include <math.h>
#include <string.h>

#define MOB_CREEPER_PI 3.14159265358979f64

void mob_creeper_start(mob_creeper_t *creeper, arena_t *arena, uint64_t seed) {
    memset(creeper, 0, sizeof(*creeper));
    creeper->borrowed_bodies =
        arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t), 64u);
    creeper->random = terrain_rng_start(seed);
}

void mob_creeper_seed_set(mob_creeper_t *creeper, uint64_t seed) {
    creeper->random = terrain_rng_start(seed);
}

/* What it leaves when it dies some other way than bursting. */
static void mob_creeper_drops_spawn(mob_creeper_t *creeper, item_drop_store_t *drops,
                                    const float64_t position[COLLISION_AXES]) {
    const mob_def_t *const def = &mob_defs[MOB_ID_CREEPER];
    for (uint32_t row = 0u; row < def->drop_count; row++) {
        const mob_drop_t *const drop = &mob_drops[def->first_drop + row];
        /* Both numbers are taken whatever comes of them, so that one row's luck does not shift the next's. */
        const float64_t roll = terrain_rng_next_f64(&creeper->random);
        const uint32_t spread = terrain_rng_next_u32(&creeper->random);
        if (roll >= (float64_t)drop->chance) {
            continue;
        }
        const uint32_t choices = (uint32_t)drop->count_max - (uint32_t)drop->count_min + 1u;
        const uint32_t count = (uint32_t)drop->count_min + spread % choices;
        if (count != 0u) {
            item_drop_spawn(drops, item_stack_drop_row(drop->item), count, position);
            creeper->dropped_count += count;
        }
    }
}

/* Whether anyone is there to chase: a body that has died or is waiting to come back is not. */
static bool mob_creeper_has_target(const mob_creeper_scene_t *scene) {
    return scene->borrowed_movement != nullptr && scene->borrowed_survival->state != PLAYER_SURVIVAL_DEAD &&
           scene->borrowed_survival->placing == 0u;
}

/* What its burst does to the player, who is not in the store. */
static void mob_creeper_hit_player(const mob_creeper_scene_t *scene, const float64_t center[COLLISION_AXES]) {
    if (!mob_creeper_has_target(scene)) {
        return;
    }
    player_movement_t *const movement = scene->borrowed_movement;
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    collision_box_t box;
    player_movement_box(movement->position, height, &box);
    explosion_blow_t blow;
    if (explosion_blow(scene->borrowed_edit->borrowed_map, center, MOB_CREEPER_POWER, movement->position, &box,
                       movement->eye_height, &blow)) {
        (void)player_survival_hurt(scene->borrowed_survival, blow.damage, PLAYER_SURVIVAL_CAUSE_ATTACK);
        for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
            movement->velocity[axis] += blow.push[axis];
        }
    }
}

/* The body goes before the blast, so that it neither shields nor is hit, and it leaves nothing. */
static void mob_creeper_burst(mob_creeper_t *creeper, const mob_creeper_scene_t *scene, entity_handle_t handle,
                              uint32_t index) {
    entity_t *const entity = scene->borrowed_entity;
    entity_store_t *const store = &entity->store;
    const float64_t center[COLLISION_AXES] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                              store->borrowed_position[2][index]};
    (void)entity_store_remove(store, handle);
    explosion_begin(&entity->explosion, scene->borrowed_edit->borrowed_map, store, &entity->item_drops, center,
                    MOB_CREEPER_POWER);
    mob_creeper_hit_player(scene, center);
    (void)explosion_finish(&entity->explosion, scene->borrowed_edit, &entity->item_drops, MOB_CREEPER_POWER);
    creeper->burst_count++;
}

/* Turns its head to the place, as far as a head turns. */
static void mob_creeper_look(entity_store_t *store, uint32_t index, float64_t dx, float64_t dy, float64_t dz) {
    float64_t turn = atan2(dy, dx) - (float64_t)store->borrowed_yaw[index];
    turn = remainder(turn, 2.0f64 * MOB_CREEPER_PI);
    const float64_t most = (float64_t)MOB_CREEPER_HEAD_TURN_MAX;
    turn = turn > most ? most : (turn < -most ? -most : turn);
    store->borrowed_head_yaw[index] = (float32_t)turn;
    store->borrowed_head_pitch[index] = (float32_t)atan2(dz, sqrt(dx * dx + dy * dy));
}

/* One tick of walking at the player: it asks again when its way has run out or the player's cell has changed, though not too often. */
static void mob_creeper_chase(mob_creeper_t *creeper, const mob_creeper_scene_t *scene, mob_creeper_body_t *body,
                              entity_handle_t handle, uint32_t index) {
    entity_store_t *const store = &scene->borrowed_entity->store;
    const float64_t *const goal = scene->borrowed_movement->position;
    const int32_t cell[3] = {(int32_t)floor(goal[0]), (int32_t)floor(goal[1]), (int32_t)floor(goal[2])};
    const mob_path_follow_t follow =
        mob_path_follow(scene->borrowed_path, store, handle, (float64_t)mob_defs[MOB_ID_CREEPER].speed);
    const bool moved =
        body->asked == 0u || cell[0] != body->goal[0] || cell[1] != body->goal[1] || cell[2] != body->goal[2];
    if (body->ask_wait != 0u) {
        body->ask_wait--;
    } else if (moved || follow != MOB_PATH_FOLLOW_WALKING) {
        mob_path_ask_t ask;
        mob_path_ask_for_body(store, index, goal, 1u, &ask);
        mob_path_request(scene->borrowed_path, handle, &ask);
        memcpy(body->goal, cell, sizeof(body->goal));
        body->asked = 1u;
        body->ask_wait = (uint16_t)(MOB_CREEPER_ASK_MIN_TICKS +
                                    terrain_rng_next_below(&creeper->random, MOB_CREEPER_ASK_SPREAD_TICKS));
    }
}

static int32_t mob_creeper_either_way(mob_creeper_t *creeper, uint32_t most) {
    return (int32_t)terrain_rng_next_below(&creeper->random, 2u * most + 1u) - (int32_t)most;
}

/* Nobody to chase: it stands, and now and then walks somewhere near, a little slower. */
static void mob_creeper_stroll(mob_creeper_t *creeper, const mob_creeper_scene_t *scene, mob_creeper_body_t *body,
                               entity_handle_t handle, uint32_t index) {
    entity_store_t *const store = &scene->borrowed_entity->store;
    if (body->state == MOB_CREEPER_STATE_IDLE) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        if (terrain_rng_next_below(&creeper->random, MOB_CREEPER_STROLL_ONE_IN) != 0u) {
            return;
        }
        const float64_t goal[3] = {
            store->borrowed_position[0][index] + (float64_t)mob_creeper_either_way(creeper, MOB_CREEPER_STROLL_XY),
            store->borrowed_position[1][index] + (float64_t)mob_creeper_either_way(creeper, MOB_CREEPER_STROLL_XY),
            store->borrowed_position[2][index] + (float64_t)mob_creeper_either_way(creeper, MOB_CREEPER_STROLL_Z),
        };
        mob_path_ask_t ask;
        mob_path_ask_for_body(store, index, goal, 1u, &ask);
        mob_path_request(scene->borrowed_path, handle, &ask);
        const mob_path_found_t *const before = mob_path_found(scene->borrowed_path, handle);
        body->asked_serial = before != nullptr ? (uint32_t)before->serial : 0u;
        body->ask_wait = (uint16_t)MOB_CREEPER_STROLL_PATIENCE_TICKS;
        body->state = MOB_CREEPER_STATE_STROLLING;
        return;
    }
    /* The answer it had is to some older question. */
    const mob_path_found_t *const found = mob_path_found(scene->borrowed_path, handle);
    if (found == nullptr || (uint32_t)found->serial == body->asked_serial) {
        body->ask_wait--;
        if (body->ask_wait == 0u) {
            body->state = MOB_CREEPER_STATE_IDLE;
        }
        return;
    }
    const float64_t speed = (float64_t)mob_defs[MOB_ID_CREEPER].speed * MOB_CREEPER_STROLL_PACE;
    if (mob_path_follow(scene->borrowed_path, store, handle, speed) != MOB_PATH_FOLLOW_WALKING) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        body->state = MOB_CREEPER_STATE_IDLE;
    }
}

/* Who it is after, as of this tick. It must see the player to notice, and it does not at once; it gives up past its reach, or when it
 * has not seen the player for a while. */
static void mob_creeper_think(mob_creeper_t *creeper, mob_creeper_body_t *body, bool there, float64_t distance,
                              bool sees) {
    if (body->state == MOB_CREEPER_STATE_CHASING) {
        body->unseen = sees ? 0u : (uint16_t)(body->unseen + 1u);
        if (!there || distance > MOB_CREEPER_NOTICE_REACH || body->unseen > MOB_CREEPER_UNSEEN_TICKS) {
            body->state = MOB_CREEPER_STATE_IDLE;
        }
    } else if (there && sees && distance <= MOB_CREEPER_NOTICE_REACH &&
               terrain_rng_next_below(&creeper->random, MOB_CREEPER_NOTICE_ONE_IN) == 0u) {
        body->state = MOB_CREEPER_STATE_CHASING;
        body->unseen = 0u;
        body->asked = 0u;
        body->ask_wait = 0u;
    }
}

/* The fuse: lit when near enough, and out again when too far or out of sight, running back as it ran on. */
static void mob_creeper_fuse(mob_creeper_body_t *body, float64_t distance, bool sees) {
    const bool chasing = body->state == MOB_CREEPER_STATE_CHASING;
    if (chasing && sees &&
        (distance < MOB_CREEPER_LIGHT_REACH || (body->lit != 0u && distance <= MOB_CREEPER_ABORT_REACH))) {
        body->lit = 1u;
    } else {
        body->lit = 0u;
    }
    if (body->lit != 0u) {
        body->fuse = body->fuse < MOB_CREEPER_FUSE_TICKS ? (uint8_t)(body->fuse + 1u) : body->fuse;
    } else if (body->fuse != 0u) {
        body->fuse--;
    }
}

/* One body with health left, in a column loaded. If it bursts it is gone, and the index with it. */
static void mob_creeper_body_tick(mob_creeper_t *creeper, const mob_creeper_scene_t *scene, mob_creeper_body_t *body,
                                  entity_handle_t handle, uint32_t index, uint32_t *bursts) {
    entity_store_t *const store = &scene->borrowed_entity->store;
    const bool there = mob_creeper_has_target(scene);
    float64_t distance = 0.0f64, dx = 0.0f64, dy = 0.0f64, dz = 0.0f64;
    bool sees = false;
    if (there) {
        const player_movement_t *const movement = scene->borrowed_movement;
        const float64_t eye = (float64_t)store->borrowed_height[index] * EXPLOSION_EYE_SHARE;
        dx = movement->position[0] - store->borrowed_position[0][index];
        dy = movement->position[1] - store->borrowed_position[1][index];
        const float64_t feet = movement->position[2] - store->borrowed_position[2][index];
        distance = sqrt(dx * dx + dy * dy + feet * feet);
        dz = feet + movement->eye_height - eye;
        if (distance <= MOB_CREEPER_NOTICE_REACH) {
            const float64_t from[3] = {store->borrowed_position[0][index], store->borrowed_position[1][index],
                                       store->borrowed_position[2][index] + eye};
            const float64_t to[3] = {movement->position[0], movement->position[1],
                                     movement->position[2] + movement->eye_height};
            sees = explosion_line_clear(scene->borrowed_edit->borrowed_map, from, to);
        }
    }
    mob_creeper_think(creeper, body, there, distance, sees);
    mob_creeper_fuse(body, distance, sees);
    store->borrowed_swell[index] = (float32_t)body->fuse / (float32_t)MOB_CREEPER_FUSE_TICKS;
    if (body->fuse >= MOB_CREEPER_FUSE_TICKS && *bursts < MOB_CREEPER_BURSTS_PER_TICK) {
        (*bursts)++;
        mob_creeper_burst(creeper, scene, handle, index);
        return;
    }
    if (body->lit != 0u) {
        /* A lit fuse holds it where it stands. */
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        mob_creeper_look(store, index, dx, dy, dz);
    } else if (body->state == MOB_CREEPER_STATE_CHASING) {
        mob_creeper_chase(creeper, scene, body, handle, index);
        mob_creeper_look(store, index, dx, dy, dz);
    } else {
        mob_creeper_stroll(creeper, scene, body, handle, index);
        store->borrowed_head_yaw[index] = 0.0f32;
        store->borrowed_head_pitch[index] = 0.0f32;
    }
}

void mob_creeper_tick(mob_creeper_t *creeper, const mob_creeper_scene_t *scene) {
    entity_t *const entity = scene->borrowed_entity;
    entity_store_t *const store = &entity->store;
    uint32_t bursts = 0u;
    /* From the end: a removal moves the last body into the gap, and that one has had its turn. */
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_kind[i] != (uint16_t)MOB_ID_CREEPER) {
            continue;
        }
        const entity_handle_t handle = store->borrowed_handle[i];
        mob_creeper_body_t *const body = &creeper->borrowed_bodies[entity_handle_slot(handle)];
        if (body->owner != handle) {
            *body = (mob_creeper_body_t){.owner = handle};
        }
        if (store->borrowed_health[i] <= 0.0f32) {
            const float64_t position[COLLISION_AXES] = {store->borrowed_position[0][i], store->borrowed_position[1][i],
                                                        store->borrowed_position[2][i]};
            mob_creeper_drops_spawn(creeper, &entity->item_drops, position);
            (void)entity_store_remove(store, handle);
            creeper->died_count++;
            continue;
        }
        /* A body whose column is not loaded is not there: it holds, fuse and all. */
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) == 0u) {
            mob_creeper_body_tick(creeper, scene, body, handle, i, &bursts);
        }
    }
}
