/* See README.md. The Arena tier by its depth, and it takes nothing: all it needs is in player_combat_t. */
#include "player/combat/index.h"

#include "machine/index.h"
#include "physics/raycast/index.h"
#include "player/camera/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"

#include <math.h>
#include <string.h>

void player_combat_start(player_combat_t *combat) {
    memset(combat, 0, sizeof(*combat));
}

float32_t player_combat_attack_speed(uint16_t item) {
    const item_def_t *const def = &item_defs[item];
    switch (def->tool) {
    case BLOCK_TOOL_SWORD:
        return 1.6f32;
    case BLOCK_TOOL_SHOVEL:
        return 1.0f32;
    case BLOCK_TOOL_PICKAXE:
        return 1.2f32;
    case BLOCK_TOOL_AXE:
        /* An axe's and a hoe's go by what they are made of: config/tiers/ (plan 005 phase 6). */
        return item_tiers[def->tier].axe_attack_speed;
    case BLOCK_TOOL_HOE:
        return item_tiers[def->tier].hoe_attack_speed;
    default:
        return PLAYER_COMBAT_HAND_SPEED;
    }
}

float32_t player_combat_strength(uint32_t since_swing, float32_t speed) {
    const float32_t delay = (float32_t)MC_GAME_TICK_HZ / speed;
    const float32_t strength = ((float32_t)since_swing + 0.5f32) / delay;
    return strength > 1.0f32 ? 1.0f32 : strength;
}

/* How far along the line it comes into this box, or a negative number if it does not. Inside it is 0. */
static float64_t player_combat_enter(const collision_box_t *box, const float64_t eye[COLLISION_AXES],
                                     const float64_t direction[COLLISION_AXES]) {
    float64_t enter = 0.0f64, leave = PLAYER_COMBAT_REACH;
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        if (direction[axis] == 0.0f64) {
            if (eye[axis] < box->min[axis] || eye[axis] > box->max[axis]) {
                return -1.0f64;
            }
            continue;
        }
        const float64_t a = (box->min[axis] - eye[axis]) / direction[axis];
        const float64_t b = (box->max[axis] - eye[axis]) / direction[axis];
        const float64_t near = a < b ? a : b;
        const float64_t far = a < b ? b : a;
        enter = near > enter ? near : enter;
        leave = far < leave ? far : leave;
    }
    return enter <= leave ? enter : -1.0f64;
}

void player_combat_aim(const world_map_t *map, const entity_store_t *store, const float64_t eye[COLLISION_AXES],
                       const float64_t direction[COLLISION_AXES], player_combat_aim_t *out) {
    *out = (player_combat_aim_t){.distance = PLAYER_COMBAT_REACH, .handle = ENTITY_HANDLE_NONE};
    raycast_hit_t hit;
    float64_t limit = PLAYER_COMBAT_REACH;
    if (raycast_cast(map, eye, direction, PLAYER_COMBAT_REACH, &hit) != RAYCAST_RESULT_MISS) {
        out->blocked = 1u;
        limit = hit.distance;
    }
    for (uint32_t i = 0u; i < store->count; i++) {
        if (!(store->borrowed_health[i] > 0.0f32) || (store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        collision_box_t box;
        entity_store_box(store, i, &box);
        const float64_t distance = player_combat_enter(&box, eye, direction);
        /* A tie goes to whichever is first in the store, which means nothing, but is the same every time. */
        if (distance >= 0.0f64 && distance <= limit &&
            (out->handle == ENTITY_HANDLE_NONE || distance < out->distance)) {
            out->handle = store->borrowed_handle[i];
            out->distance = distance;
        }
    }
}

bool player_combat_tick(player_combat_t *combat, const player_combat_input_t *input, player_combat_swing_t *out) {
    const bool press = input->attack != 0u && combat->attack_before == 0u;
    combat->attack_before = input->attack;
    combat->aiming_body = 0u;
    if (combat->held_item != input->weapon.item) {
        combat->held_item = input->weapon.item;
        combat->since_swing = 0u;
    } else if (combat->since_swing != UINT16_MAX) {
        combat->since_swing++;
    }
    if (input->attack == 0u || input->may_hit == 0u) {
        return false;
    }
    const player_movement_t *const movement = input->borrowed_movement;
    const float64_t eye[COLLISION_AXES] = {movement->position[0], movement->position[1],
                                           movement->position[2] + movement->eye_height};
    float64_t direction[COLLISION_AXES];
    player_camera_direction(input->yaw, input->pitch, direction);
    player_combat_aim_t aim;
    player_combat_aim(input->borrowed_map, input->borrowed_store, eye, direction, &aim);
    combat->aiming_body = aim.handle != ENTITY_HANDLE_NONE ? 1u : 0u;
    if (!press) {
        return false;
    }
    if (aim.handle == ENTITY_HANDLE_NONE) {
        /* At the air it costs the swing. At a block it is mining, and costs nothing. */
        combat->since_swing = aim.blocked != 0u ? combat->since_swing : 0u;
        return false;
    }
    const float32_t strength = player_combat_strength(combat->since_swing, input->weapon.speed);
    const bool full = strength > PLAYER_COMBAT_FULL_OVER;
    const bool sprinting = movement->sprinting != 0u;
    const bool critical = full && !sprinting && movement->on_ground == 0u && movement->fall_distance > 0.0f64 &&
                          movement->mode != PLAYER_MOVEMENT_MODE_CLIMB && movement->mode != PLAYER_MOVEMENT_MODE_WATER;
    const float32_t share = PLAYER_COMBAT_WEAKEST + strength * strength * (1.0f32 - PLAYER_COMBAT_WEAKEST);
    *out = (player_combat_swing_t){
        .facing = {cos((float64_t)input->yaw), sin((float64_t)input->yaw)},
        .handle = aim.handle,
        .damage = input->weapon.damage * share * (critical ? PLAYER_COMBAT_CRITICAL : 1.0f32),
        .knockback = full && sprinting ? PLAYER_COMBAT_SPRINT_KNOCKBACK : 0.0f32,
        .strength = strength,
        .critical = critical ? 1u : 0u,
    };
    combat->since_swing = 0u;
    return true;
}

void player_combat_landed(player_movement_t *movement, const player_combat_swing_t *swing) {
    if (swing->knockback > 0.0f32) {
        movement->velocity[0] *= PLAYER_COMBAT_SPRINT_SLOWS;
        movement->velocity[1] *= PLAYER_COMBAT_SPRINT_SLOWS;
        movement->sprinting = 0u;
    }
}
