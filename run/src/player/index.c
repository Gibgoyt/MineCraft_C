/* See README.md. The Owner. Nothing in it needs memory, so it holds no region: what crosses threads is here, and how a body
 * moves is in movement/. */
#include "player/index.h"

#include <string.h>

enum : uint32_t {
    PLAYER_INTENT_SHIFT_FORWARD = 32u,
    PLAYER_INTENT_SHIFT_RIGHT = 40u,
    PLAYER_INTENT_SHIFT_JUMP = 48u,
    PLAYER_INTENT_SHIFT_SNEAK = 49u,
    PLAYER_INTENT_SHIFT_SPRINT = 50u,
};

/* Where the random numbers of a body's drops start. */
#define PLAYER_DROP_SEED 0x4D494E45u

/* In one word, so that the sim never sees half of one intent and half of another. */
static uint64_t player_intent_pack(const player_intent_t *intent) {
    uint32_t yaw = 0u;
    memcpy(&yaw, &intent->yaw, sizeof(yaw));
    return (uint64_t)yaw | ((uint64_t)(uint8_t)intent->forward << PLAYER_INTENT_SHIFT_FORWARD) |
           ((uint64_t)(uint8_t)intent->right << PLAYER_INTENT_SHIFT_RIGHT) |
           ((uint64_t)(intent->jump != 0u ? 1u : 0u) << PLAYER_INTENT_SHIFT_JUMP) |
           ((uint64_t)(intent->sneak != 0u ? 1u : 0u) << PLAYER_INTENT_SHIFT_SNEAK) |
           ((uint64_t)(intent->sprint != 0u ? 1u : 0u) << PLAYER_INTENT_SHIFT_SPRINT);
}

static player_intent_t player_intent_unpack(uint64_t word) {
    player_intent_t intent = {
        .yaw = 0.0f32, .forward = 0, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};
    const uint32_t yaw = (uint32_t)word;
    memcpy(&intent.yaw, &yaw, sizeof(yaw));
    intent.forward = (int8_t)(uint8_t)(word >> PLAYER_INTENT_SHIFT_FORWARD);
    intent.right = (int8_t)(uint8_t)(word >> PLAYER_INTENT_SHIFT_RIGHT);
    intent.jump = (uint8_t)((word >> PLAYER_INTENT_SHIFT_JUMP) & 1u);
    intent.sneak = (uint8_t)((word >> PLAYER_INTENT_SHIFT_SNEAK) & 1u);
    intent.sprint = (uint8_t)((word >> PLAYER_INTENT_SHIFT_SPRINT) & 1u);
    return intent;
}

void player_init(player_t *player, physics_t *physics) {
    memset(player, 0, sizeof(*player));
    player->borrowed_physics = physics;
}

void player_deinit(player_t *player) {
    memset(player, 0, sizeof(*player));
}

/* In one word, so that the sim never sees one look's yaw with another's pitch. */
static uint64_t player_look_pack(float32_t yaw, float32_t pitch) {
    uint32_t yaw_bits = 0u, pitch_bits = 0u;
    memcpy(&yaw_bits, &yaw, sizeof(yaw_bits));
    memcpy(&pitch_bits, &pitch, sizeof(pitch_bits));
    return (uint64_t)yaw_bits | ((uint64_t)pitch_bits << 32u);
}

static void player_view_fill(const player_t *player, player_view_t *view, int64_t tick_ns) {
    const player_movement_t *const movement = &player->movement;
    view->previous[0] = movement->previous[0];
    view->previous[1] = movement->previous[1];
    view->previous[2] = movement->previous[2] + movement->previous_eye;
    view->current[0] = movement->position[0];
    view->current[1] = movement->position[1];
    view->current[2] = movement->position[2] + movement->eye_height;
    view->tick_ns = tick_ns;
    view->tick = player->tick_count;
    view->target = player->target;
}

/* In one word, so that main never sees part of one tick's with part of another's. */
static void player_vitals_publish(player_t *player) {
    const player_survival_t *const survival = &player->survival;
    const player_vitals_t vitals = {
        .health = survival->health,
        .air = survival->air > 0 ? (uint16_t)survival->air : 0u,
        .food = survival->food,
        .state = survival->state,
    };
    uint64_t word = 0u;
    memcpy(&word, &vitals, sizeof(word));
    atomic_store_explicit(&player->vitals_word, word, memory_order_relaxed);
}

void player_spawn(player_t *player, float64_t x, float64_t y, float64_t z) {
    player_movement_start(&player->movement, x, y, z);
    player_survival_start(&player->survival, x, y, z);
    player_vitals_publish(player);
    player->tick_count = 0u;
    memset(&player->target, 0, sizeof(player->target));
    render_exchange_start(&player->views);
    /* Whichever slot is read first holds where it stands. */
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        player_view_fill(player, &player->view_slots[i], 0);
    }
    /* The reader's own slot, which the writer will not touch until it has taken another. */
    player->view_slot = player->views.front;
    atomic_store_explicit(&player->intent_word, 0u, memory_order_relaxed);
    atomic_store_explicit(&player->intent_sent_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&player->look_word, 0u, memory_order_relaxed);
    atomic_store_explicit(&player->jump_latch, 0u, memory_order_relaxed);
    atomic_store_explicit(&player->hands_word, 0u, memory_order_relaxed);
    atomic_store_explicit(&player->hands_latch, 0u, memory_order_relaxed);
    player_interaction_start(&player->interaction, PLAYER_DROP_SEED);
    player_combat_start(&player->combat);
}

void player_look_send(player_t *player, float32_t yaw, float32_t pitch) {
    atomic_store_explicit(&player->look_word, player_look_pack(yaw, pitch), memory_order_relaxed);
}

void player_send(player_t *player, const player_intent_t *intent, int64_t now_ns) {
    atomic_store_explicit(&player->intent_word, player_intent_pack(intent), memory_order_relaxed);
    atomic_store_explicit(&player->intent_sent_ns, now_ns, memory_order_relaxed);
    if (intent->jump != 0u) {
        atomic_store_explicit(&player->jump_latch, 1u, memory_order_relaxed);
    }
}

bool player_sneak_sent(const player_t *player) {
    return player_intent_unpack(atomic_load_explicit(&player->intent_word, memory_order_relaxed)).sneak != 0u;
}

void player_hands_send(player_t *player, bool attack, bool place) {
    const uint32_t hands = (attack ? (uint32_t)PLAYER_HAND_ATTACK : 0u) | (place ? (uint32_t)PLAYER_HAND_PLACE : 0u);
    atomic_store_explicit(&player->hands_word, hands, memory_order_relaxed);
    if (hands != 0u) {
        atomic_fetch_or_explicit(&player->hands_latch, hands, memory_order_relaxed);
    }
}

void player_interact_tick(player_t *player, world_edit_t *edit, int64_t tick_ns) {
    uint32_t hands = atomic_load_explicit(&player->hands_word, memory_order_relaxed);
    hands |= atomic_exchange_explicit(&player->hands_latch, 0u, memory_order_relaxed);
    if (player->hands_held_off != 0u) {
        hands = 0u;
    }
    /* Looked at and left for player_tick, whose jump it is. */
    const player_intent_t intent =
        player_intent_unpack(atomic_load_explicit(&player->intent_word, memory_order_relaxed));
    bool jump = intent.jump != 0u || atomic_load_explicit(&player->jump_latch, memory_order_relaxed) != 0u;
    bool sneak = intent.sneak != 0u;
    /* As for the keys: nobody at the controls lets go. */
    if (tick_ns - atomic_load_explicit(&player->intent_sent_ns, memory_order_relaxed) > PLAYER_INTENT_STALE_NS) {
        hands = 0u;
        jump = false;
        sneak = false;
    }
    /* What keeps the body going has the buttons first, and may leave the hands none. */
    player_survival_buttons_t buttons = {
        .borrowed_target = &player->target,
        .borrowed_movement = &player->movement,
        .borrowed_physics = player->borrowed_physics,
        .borrowed_map = edit->borrowed_map,
        .attack = (hands & PLAYER_HAND_ATTACK) != 0u ? 1u : 0u,
        .place = (hands & PLAYER_HAND_PLACE) != 0u ? 1u : 0u,
        .jump = jump ? 1u : 0u,
        .sneak = sneak ? 1u : 0u,
        ._pad0 = {0},
    };
    player_survival_buttons(&player->survival, &buttons);
    /* A body in the way is what the button is on, not what is behind it. */
    if (player->combat.aiming_body != 0u) {
        buttons.attack = 0u;
    }
    player->combat.aiming_body = 0u;
    const player_interaction_input_t input = {
        .borrowed_target = &player->target,
        .borrowed_movement = &player->movement,
        .attack = buttons.attack,
        .place = buttons.place,
        ._pad0 = {0},
    };
    player_interaction_tick(&player->interaction, edit, &input);
}

bool player_attack_tick(player_t *player, const entity_store_t *store, const world_map_t *map,
                        const player_combat_weapon_t *weapon, int64_t tick_ns, player_combat_swing_t *out) {
    /* Only a look at the buttons: player_interact_tick takes the latch next. */
    uint32_t hands = atomic_load_explicit(&player->hands_word, memory_order_relaxed) |
                     atomic_load_explicit(&player->hands_latch, memory_order_relaxed);
    if (player->hands_held_off != 0u ||
        tick_ns - atomic_load_explicit(&player->intent_sent_ns, memory_order_relaxed) > PLAYER_INTENT_STALE_NS) {
        hands = 0u;
    }
    const uint64_t look = atomic_load_explicit(&player->look_word, memory_order_relaxed);
    const uint32_t yaw_bits = (uint32_t)look, pitch_bits = (uint32_t)(look >> 32u);
    float32_t yaw = 0.0f32, pitch = 0.0f32;
    memcpy(&yaw, &yaw_bits, sizeof(yaw));
    memcpy(&pitch, &pitch_bits, sizeof(pitch));
    const player_combat_input_t input = {
        .borrowed_movement = &player->movement,
        .borrowed_store = store,
        .borrowed_map = map,
        .weapon = *weapon,
        .yaw = yaw,
        .pitch = pitch,
        .attack = (hands & PLAYER_HAND_ATTACK) != 0u ? 1u : 0u,
        .may_hit = player_survival_holds_body(&player->survival) ? 0u : 1u,
        ._pad0 = {0},
    };
    return player_combat_tick(&player->combat, &input, out);
}

void player_attack_landed(player_t *player, const player_combat_swing_t *swing) {
    player_survival_exhaust(&player->survival, PLAYER_COMBAT_TIRES);
    player_combat_landed(&player->movement, swing);
}

void player_pickup_reach(const player_t *player, collision_box_t *out) {
    const player_movement_t *const movement = &player->movement;
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    player_movement_box(movement->position, height, out);
    out->min[COLLISION_AXIS_X] -= PLAYER_PICKUP_REACH_ACROSS;
    out->min[COLLISION_AXIS_Y] -= PLAYER_PICKUP_REACH_ACROSS;
    out->min[COLLISION_AXIS_Z] -= PLAYER_PICKUP_REACH_UP;
    out->max[COLLISION_AXIS_X] += PLAYER_PICKUP_REACH_ACROSS;
    out->max[COLLISION_AXIS_Y] += PLAYER_PICKUP_REACH_ACROSS;
    out->max[COLLISION_AXIS_Z] += PLAYER_PICKUP_REACH_UP;
}

void player_tick(player_t *player, const world_map_t *map, int64_t tick_ns) {
    player_intent_t intent = player_intent_unpack(atomic_load_explicit(&player->intent_word, memory_order_relaxed));
    const uint32_t latched = atomic_exchange_explicit(&player->jump_latch, 0u, memory_order_relaxed);
    intent.jump = intent.jump != 0u || latched != 0u ? 1u : 0u;
    /* Nobody at the controls lets go of every key, and keeps looking where it last looked. */
    if (tick_ns - atomic_load_explicit(&player->intent_sent_ns, memory_order_relaxed) > PLAYER_INTENT_STALE_NS) {
        intent.forward = 0;
        intent.right = 0;
        intent.jump = 0u;
        intent.sneak = 0u;
        intent.sprint = 0u;
    }
    /* Dead or asleep, it does not matter who is at the controls. */
    if (player_survival_holds_body(&player->survival)) {
        intent.forward = 0;
        intent.right = 0;
        intent.jump = 0u;
        intent.sneak = 0u;
        intent.sprint = 0u;
    } else if (!player_survival_can_sprint(&player->survival)) {
        intent.sprint = 0u;
    }
    player_movement_tick(&player->movement, player->borrowed_physics, map, &intent);
    if (player->survival.state != PLAYER_SURVIVAL_ALIVE) {
        /* Lying down, however the body would stand. */
        const float64_t lowered = player->movement.previous_eye - PLAYER_SURVIVAL_EYE_LOWERS;
        player->movement.eye_height = lowered < PLAYER_SURVIVAL_EYE_LYING ? PLAYER_SURVIVAL_EYE_LYING : lowered;
    }
    player->tick_count++;
    /* After the move: what it aims at is from where it now stands. */
    const uint64_t look = atomic_load_explicit(&player->look_word, memory_order_relaxed);
    const uint32_t yaw_bits = (uint32_t)look, pitch_bits = (uint32_t)(look >> 32u);
    float32_t yaw = 0.0f32, pitch = 0.0f32;
    memcpy(&yaw, &yaw_bits, sizeof(yaw));
    memcpy(&pitch, &pitch_bits, sizeof(pitch));
    const player_movement_t *const movement = &player->movement;
    const float64_t eye[3] = {movement->position[0], movement->position[1],
                              movement->position[2] + movement->eye_height};
    player_camera_target(map, eye, yaw, pitch, &player->target);
    player_view_fill(player, &player->view_slots[player->views.back], tick_ns);
    (void)render_exchange_publish(&player->views);
}

void player_survive_tick(player_t *player, const world_map_t *map) {
    player_survival_tick(&player->survival, &player->movement, player->borrowed_physics, map,
                         player->interaction.mined_count);
    player_vitals_publish(player);
}

player_vitals_t player_vitals_read(const player_t *player) {
    const uint64_t word = atomic_load_explicit(&player->vitals_word, memory_order_relaxed);
    player_vitals_t vitals;
    memcpy(&vitals, &word, sizeof(vitals));
    return vitals;
}

void player_eye_at(player_t *player, int64_t now_ns, float64_t out[3]) {
    uint32_t slot = 0u;
    if (render_exchange_take(&player->views, &slot)) {
        player->view_slot = slot;
    }
    const player_view_t *const view = &player->view_slots[player->view_slot];
    const int64_t since_ns = now_ns - view->tick_ns;
    const int64_t held_ns = since_ns < 0 ? 0 : (since_ns > PLAYER_TICK_NS ? PLAYER_TICK_NS : since_ns);
    const float64_t along = (float64_t)held_ns / (float64_t)PLAYER_TICK_NS;
    for (uint32_t a = 0u; a < 3u; a++) {
        out[a] = view->previous[a] + (view->current[a] - view->previous[a]) * along;
    }
}

const player_target_t *player_target_view(const player_t *player) {
    return &player->view_slots[player->view_slot].target;
}
