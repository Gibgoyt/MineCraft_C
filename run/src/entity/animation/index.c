/* See README.md. The Arena tier by its depth, and it allocates nothing: its rigs are constants, from config/mobs/, and what moves is in the store. */
#include "entity/animation/index.h"

#include <math.h>

#define ENTITY_ANIMATION_PI 3.14159265358979f32
/* An arm held straight out: what aiming raises it to. */
#define ENTITY_ANIMATION_LEVEL (ENTITY_ANIMATION_PI * 0.5f32)
/* The published numbers. A stride's rate and size, and how fast a body takes up its pace. */
#define ENTITY_ANIMATION_STRIDE_RATE 0.6662f32
#define ENTITY_ANIMATION_STRIDE_PERIOD (2.0f32 * ENTITY_ANIMATION_PI / ENTITY_ANIMATION_STRIDE_RATE)
#define ENTITY_ANIMATION_PACE_PER_SPEED 4.0f32
#define ENTITY_ANIMATION_PACE_EASE 0.4f32
/* How far an attack lifts an arm at its height, and how much a burst swells a body. */
#define ENTITY_ANIMATION_SWING_LIFT 1.2f32
#define ENTITY_ANIMATION_SWELL_GROWTH 0.2f32

static const entity_animation_rig_t ENTITY_ANIMATION_RIGS[ENTITY_ANIMATION_RIG_COUNT] = {
    [ENTITY_ANIMATION_RIG_ZOMBIE] = {&mob_parts[MOB_ZOMBIE_FIRST_PART], MOB_ZOMBIE_PART_COUNT, MOB_ZOMBIE_TEXTURE_WIDTH,
                                     MOB_ZOMBIE_TEXTURE_HEIGHT},
    [ENTITY_ANIMATION_RIG_SKELETON] = {&mob_parts[MOB_SKELETON_FIRST_PART], MOB_SKELETON_PART_COUNT,
                                       MOB_SKELETON_TEXTURE_WIDTH, MOB_SKELETON_TEXTURE_HEIGHT},
    [ENTITY_ANIMATION_RIG_CREEPER] = {&mob_parts[MOB_CREEPER_FIRST_PART], MOB_CREEPER_PART_COUNT,
                                      MOB_CREEPER_TEXTURE_WIDTH, MOB_CREEPER_TEXTURE_HEIGHT},
};

const entity_animation_rig_t *entity_animation_rig(entity_animation_rig_id_t rig) {
    return &ENTITY_ANIMATION_RIGS[rig];
}

void entity_animation_tick(entity_store_t *store) {
    for (uint32_t i = 0u; i < store->count; i++) {
        store->borrowed_swing_ticks[i] =
            store->borrowed_swing_ticks[i] != 0u ? (uint8_t)(store->borrowed_swing_ticks[i] - 1u) : 0u;
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        const float64_t x = store->borrowed_position[0][i] - store->borrowed_previous[0][i];
        const float64_t y = store->borrowed_position[1][i] - store->borrowed_previous[1][i];
        const float32_t wanted = (float32_t)sqrt(x * x + y * y) * ENTITY_ANIMATION_PACE_PER_SPEED;
        const float32_t pace = wanted > 1.0f32 ? 1.0f32 : wanted;
        const float32_t amount =
            store->borrowed_limb_amount[i] + (pace - store->borrowed_limb_amount[i]) * ENTITY_ANIMATION_PACE_EASE;
        store->borrowed_limb_amount[i] = amount;
        const float32_t phase = store->borrowed_limb_phase[i] + amount;
        /* A whole stride later it looks the same, and a small number keeps its exactness all night. */
        store->borrowed_limb_phase[i] =
            phase >= ENTITY_ANIMATION_STRIDE_PERIOD ? phase - ENTITY_ANIMATION_STRIDE_PERIOD : phase;
    }
}

void entity_animation_swing(entity_store_t *store, uint32_t index) {
    store->borrowed_swing_ticks[index] = ENTITY_ANIMATION_SWING_TICKS;
}

void entity_animation_state_read(const entity_store_t *store, uint32_t index, entity_animation_state_t *out) {
    const uint32_t ticks = store->borrowed_swing_ticks[index];
    *out = (entity_animation_state_t){
        .yaw = store->borrowed_yaw[index],
        .limb_phase = store->borrowed_limb_phase[index],
        .limb_amount = store->borrowed_limb_amount[index],
        .head_yaw = store->borrowed_head_yaw[index],
        .head_pitch = store->borrowed_head_pitch[index],
        .swing = ticks == 0u ? 0.0f32 : 1.0f32 - (float32_t)ticks / (float32_t)ENTITY_ANIMATION_SWING_TICKS,
        .aim = store->borrowed_aim[index],
        .swell = store->borrowed_swell[index],
    };
}

/* The pitch of a part: its rest, what walking does, and, for an arm, what aiming and an attack do. */
static float32_t entity_animation_part_pitch(const entity_animation_part_t *part,
                                             const entity_animation_state_t *state) {
    const float32_t stride = cosf(state->limb_phase * ENTITY_ANIMATION_STRIDE_RATE);
    float32_t pitch = part->rest_pitch + part->walk_swing * stride * state->limb_amount;
    if ((part->flags & ENTITY_ANIMATION_PART_ARM) != 0u) {
        /* Aiming takes over from walking, up to level. */
        pitch += (ENTITY_ANIMATION_LEVEL - pitch) * state->aim;
        pitch += sinf(state->swing * ENTITY_ANIMATION_PI) * ENTITY_ANIMATION_SWING_LIFT;
    }
    if ((part->flags & ENTITY_ANIMATION_PART_HEAD) != 0u) {
        pitch += state->head_pitch;
    }
    return pitch;
}

uint32_t entity_animation_pose(const entity_animation_rig_t *rig, const entity_animation_state_t *state,
                               const float64_t position[COLLISION_AXES], entity_animation_pose_t *out) {
    const float32_t to_blocks = 1.0f32 / ENTITY_ANIMATION_PIXELS_PER_BLOCK;
    const float32_t growth = 1.0f32 + state->swell * ENTITY_ANIMATION_SWELL_GROWTH;
    const float32_t body_cos = cosf(state->yaw), body_sin = sinf(state->yaw);
    for (uint32_t p = 0u; p < rig->part_count; p++) {
        const entity_animation_part_t *const part = &rig->borrowed_parts[p];
        const bool head = (part->flags & ENTITY_ANIMATION_PART_HEAD) != 0u;
        const float32_t pitch = entity_animation_part_pitch(part, state);
        const float32_t yaw = head ? state->yaw + state->head_yaw : state->yaw;
        const float32_t pitch_cos = cosf(pitch), pitch_sin = sinf(pitch);
        const float32_t yaw_cos = cosf(yaw), yaw_sin = sinf(yaw);
        /* Its middle from its pivot, tilted about its left axis. */
        const float32_t forward = part->offset[0] * pitch_cos - part->offset[2] * pitch_sin;
        const float32_t left = part->offset[1];
        const float32_t up = part->offset[0] * pitch_sin + part->offset[2] * pitch_cos;
        /* The pivot turns with the body, and the part about the pivot by its own yaw. */
        const float32_t x = part->pivot[0] * body_cos - part->pivot[1] * body_sin + forward * yaw_cos - left * yaw_sin;
        const float32_t y = part->pivot[0] * body_sin + part->pivot[1] * body_cos + forward * yaw_sin + left * yaw_cos;
        const float32_t z = part->pivot[2] + up;
        entity_animation_pose_t *const pose = &out[p];
        pose->center[0] = position[0] + (float64_t)(x * to_blocks);
        pose->center[1] = position[1] + (float64_t)(y * to_blocks);
        pose->center[2] = position[2] + (float64_t)(z * to_blocks);
        for (uint32_t a = 0u; a < 3u; a++) {
            pose->half[a] = part->size[a] * 0.5f32 * to_blocks * growth;
        }
        const float32_t reach_cos = fabsf(pitch_cos), reach_sin = fabsf(pitch_sin);
        pose->reach[ENTITY_ANIMATION_FORWARD] =
            reach_cos * pose->half[ENTITY_ANIMATION_FORWARD] + reach_sin * pose->half[ENTITY_ANIMATION_UP];
        pose->reach[ENTITY_ANIMATION_LEFT] = pose->half[ENTITY_ANIMATION_LEFT];
        pose->reach[ENTITY_ANIMATION_UP] =
            reach_sin * pose->half[ENTITY_ANIMATION_FORWARD] + reach_cos * pose->half[ENTITY_ANIMATION_UP];
        pose->yaw = yaw;
        pose->pitch = pitch;
    }
    return rig->part_count;
}
