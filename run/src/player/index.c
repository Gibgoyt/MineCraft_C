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

void player_spawn(player_t *player, float64_t x, float64_t y, float64_t z) {
    player_movement_start(&player->movement, x, y, z);
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
    player_movement_tick(&player->movement, player->borrowed_physics, map, &intent);
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
