/* See README.md. */
#include "game/index.h"

#include "game/atlas/index.h"
#include "game/command/index.h"
#include "game/view/index.h"
#include "time/index.h"

#include <math.h>
#include <string.h>

void game_walk_send(game_t *game, const render_fly_input_t *input, bool jump, bool sneak, int64_t now_ns) {
    player_camera_look(&game->camera,
                       game_view_look_scaled(input->mouse_dx, game->config.sensitivity_percent, &game->look_rest[0]),
                       game_view_look_scaled(input->mouse_dy, game->config.sensitivity_percent, &game->look_rest[1]));
    player_look_send(game->borrowed_player, game->camera.yaw, game->camera.pitch);
    const player_intent_t intent = {.yaw = game->camera.yaw,
                                    .forward = input->forward,
                                    .right = input->right,
                                    .jump = jump ? 1u : 0u,
                                    .sneak = sneak ? 1u : 0u,
                                    .sprint = input->fast,
                                    ._pad0 = {0}};
    player_send(game->borrowed_player, &intent, now_ns);
}

void game_hands_send(game_t *game, bool attack, bool place) {
    player_hands_send(game->borrowed_player, attack, place);
}

void game_turn_send(game_t *game) {
    player_turn_send(game->borrowed_player);
}

void game_hotbar_send(game_t *game, const bool keys[INVENTORY_HOTBAR_SLOTS], int64_t wheel) {
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        return;
    }
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        if (keys[slot]) {
            item_hotbar_send(item, slot);
        }
    }
    /* Plan 006 phase 01: flying, the wheel is the fly speed, and the hotbar stays. */
    bool flying = false;
    uint32_t speed = 0u, factor = 0u;
    if (game->borrowed_player != nullptr) {
        player_flight_read(game->borrowed_player, &flying, &speed, &factor);
    }
    if (flying) {
        const int64_t held = wheel > 64 ? 64 : (wheel < -64 ? -64 : wheel);
        if (held != 0) {
            player_fly_wheel_send(game->borrowed_player, (int32_t)held);
        }
        return;
    }
    /* Round the ends comes to the same, so that a wheel never overflows what it goes through. */
    item_hotbar_scroll_send(item, -(int32_t)(wheel % (int64_t)INVENTORY_HOTBAR_SLOTS));
}

void game_input_from_script(const display_headless_tick_t *tick, input_frame_t *out) {
    static_assert(sizeof(tick->state->down) == sizeof(out->down.words));
    memset(out, 0, sizeof(*out));
    memcpy(out->down.words, tick->state->down, sizeof(out->down.words));
    out->mouse_dx = tick->state->tick_dx;
    out->mouse_dy = tick->state->tick_dy;
    out->wheel = tick->state->tick_wheel;
    for (uint32_t i = 0u; i < tick->event_count; i++) {
        const display_script_event_t *const event = &tick->events[i];
        if (event->kind == DISPLAY_SCRIPT_KEY) {
            input_keys_t *const keys = event->pressed != 0u ? &out->pressed : &out->released;
            keys->words[event->code / 64u] |= 1ull << (event->code % 64u);
        }
    }
}

void game_fly_from_script(const display_script_state_t *state, render_fly_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->mouse_dx = state->tick_dx;
    out->mouse_dy = state->tick_dy;
    out->wheel = state->tick_wheel;
    out->forward = (int8_t)((display_script_state_is_down(state, KEY_W) ? 1 : 0) -
                            (display_script_state_is_down(state, KEY_S) ? 1 : 0));
    out->right = (int8_t)((display_script_state_is_down(state, KEY_D) ? 1 : 0) -
                          (display_script_state_is_down(state, KEY_A) ? 1 : 0));
    out->up = (int8_t)((display_script_state_is_down(state, KEY_SPACE) ? 1 : 0) -
                       (display_script_state_is_down(state, KEY_LEFTSHIFT) ? 1 : 0));
    out->fast = display_script_state_is_down(state, KEY_LEFTCTRL) ? 1u : 0u;
}

void game_fly_from_input(const input_frame_t *frame, render_fly_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->mouse_dx = frame->mouse_dx;
    out->mouse_dy = frame->mouse_dy;
    out->wheel = frame->wheel;
    out->forward =
        (int8_t)((input_keys_has(&frame->down, KEY_W) ? 1 : 0) - (input_keys_has(&frame->down, KEY_S) ? 1 : 0));
    out->right =
        (int8_t)((input_keys_has(&frame->down, KEY_D) ? 1 : 0) - (input_keys_has(&frame->down, KEY_A) ? 1 : 0));
    out->up = (int8_t)((input_keys_has(&frame->down, KEY_SPACE) ? 1 : 0) -
                       (input_keys_has(&frame->down, KEY_LEFTSHIFT) ? 1 : 0));
    out->fast = input_keys_has(&frame->down, KEY_LEFTCTRL) ? 1u : 0u;
}

void game_debug_send(game_t *game, bool down) {
    if (down && game->hud_debug_key == 0u) {
        game->hud_debug ^= 1u;
    }
    game->hud_debug_key = down ? 1u : 0u;
}

bool game_loop_input_frame(game_t *game, const input_frame_t *frame, display_buffer_t *back) {
    const bool screen_up = game_screen_input(game, frame);
    /* A tap over between two polls is in pressed and not in down. */
    game_debug_send(game, input_keys_has(&frame->down, KEY_F3) || input_keys_has(&frame->pressed, KEY_F3));
    const int64_t now_ns = time_now_ns();
    const float32_t seconds = (float32_t)time_ns_to_seconds(now_ns - game->last_frame_ns);
    game->last_frame_ns = now_ns;
    render_fly_input_t input;
    game_fly_from_input(frame, &input);
    if (screen_up) {
        /* The body stands, and the mouse is the pointer. Sent all the same, or the sim would take it that nobody is at the controls.
         * Flying, only the Atlas comes up, and the eye stays where it is. */
        memset(&input, 0, sizeof(input));
        if (game->borrowed_player != nullptr) {
            game_walk_send(game, &input, false, false, now_ns);
            game_hands_send(game, false, false);
        }
    } else if (game->borrowed_player != nullptr) {
        game_walk_send(game, &input, input_keys_has(&frame->down, KEY_SPACE),
                       input_keys_has(&frame->down, KEY_LEFTSHIFT), now_ns);
        /* A click over between two polls is in pressed and not in down. */
        game_hands_send(game, input_keys_has(&frame->down, BTN_LEFT) || input_keys_has(&frame->pressed, BTN_LEFT),
                        input_keys_has(&frame->down, BTN_RIGHT) || input_keys_has(&frame->pressed, BTN_RIGHT));
        if (input_keys_has(&frame->pressed, KEY_R)) {
            game_turn_send(game);
        }
        bool hotbar[INVENTORY_HOTBAR_SLOTS];
        for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
            hotbar[slot] = input_keys_has(&frame->pressed, KEY_1 + slot);
        }
        game_hotbar_send(game, hotbar, frame->wheel);
    }
    return game_loop_frame(game, &input, seconds, back);
}

/* A pause starts the frame clock again, so the time stopped is not a step of flying. */
void game_pause_set(game_t *game, bool paused) {
    atomic_store_explicit(&game->paused, paused ? 1u : 0u, memory_order_relaxed);
    game->last_frame_ns = time_now_ns();
}

void game_loop_input_clear(game_t *game) {
    if (game->borrowed_player != nullptr) {
        const render_fly_input_t none = {0};
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
    }
}

/* ---- The game screens ------------------------------------------------------------------------------------------------ */

/* What the screens need to know of this moment, on the sim. */
void game_screen_sim(game_t *game, ui_screen_sim_t *out) {
    const player_t *const player = game->borrowed_player;
    const float64_t *const position = player->movement.position;
    *out = (ui_screen_sim_t){
        .borrowed_item = game->borrowed_item,
        .spill = game_spill,
        .borrowed_context = game,
        .cell = {(int32_t)floor(position[0]), (int32_t)floor(position[1]), (int32_t)floor(position[2])},
        .may_open = player_survival_holds_body(&player->survival) ? 0u : 1u,
        ._pad0 = {0}};
}

static void game_center_on_eye(game_t *game) {
    const int32_t cell_x = (int32_t)floor(game->camera.position[0]);
    const int32_t cell_y = (int32_t)floor(game->camera.position[1]);
    atomic_store_explicit(&game->center_x, column_chunk_of(cell_x), memory_order_relaxed);
    atomic_store_explicit(&game->center_y, column_chunk_of(cell_y), memory_order_relaxed);
}

void game_fly(game_t *game, const render_fly_input_t *input, float32_t seconds) {
    render_fly_input_t scaled = *input;
    scaled.mouse_dx = game_view_look_scaled(input->mouse_dx, game->config.sensitivity_percent, &game->look_rest[0]);
    scaled.mouse_dy = game_view_look_scaled(input->mouse_dy, game->config.sensitivity_percent, &game->look_rest[1]);
    render_camera_fly(&game->camera, &scaled, seconds);
    game_center_on_eye(game);
}

/* The eye goes where the ticks have got to, by now_ns. */
void game_walk_view(game_t *game, int64_t now_ns) {
    player_eye_at(game->borrowed_player, now_ns, game->camera.position);
    game_center_on_eye(game);
}

bool game_has_screens(const game_t *game) {
    return game->borrowed_ui != nullptr && game->borrowed_item != nullptr && game->borrowed_player != nullptr;
}

bool game_screen_input(game_t *game, const input_frame_t *frame) {
    if (game_command_input(game, frame)) {
        return true; /* plan 006 phase 01: the command box has the keys: the body stands, and the world goes on */
    }
    if (game_atlas_input(game, frame)) {
        return true; /* the Atlas has the input: the body stands, and the world goes on */
    }
    if (!game_has_screens(game)) {
        return false;
    }
    return ui_screen_input(game->borrowed_ui, frame, game->config.width, game->config.height);
}

bool game_screen_up(game_t *game) {
    /* The command box first: Esc closes it before anything else sees Esc (plan 006 phase 01). */
    return game_command_up(game) || game_atlas_up(game) || (game_has_screens(game) && ui_screen_up(game->borrowed_ui));
}
