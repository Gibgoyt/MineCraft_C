/* See README.md. */
#include "game/session/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "simd/copy/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

enum : uint8_t {
    /* One to draw into each buffer, so that what is on the screen, whichever it is, says so before the long frame. */
    GAME_SESSION_LOADING_FRAMES = DISPLAY_FLIP_BUFFERS,
};

void game_session_init(game_session_t *session, game_t *game, ui_t *ui, terrain_t *terrain, input_t *input,
                       const game_config_t *config, const char *saves, uint64_t blank_seed) {
    ASSERT_ALWAYS(game->borrowed_player != nullptr);
    memset(session, 0, sizeof(*session));
    session->borrowed_game = game;
    session->borrowed_ui = ui;
    session->borrowed_terrain = terrain;
    session->borrowed_input = input;
    session->config = *config;
    ui_menu_saves_set(&ui->menu, saves, blank_seed);
    ui_menu_open(&ui->menu, UI_MENU_SCREEN_MAIN);
}

void game_session_deinit(game_session_t *session) {
    memset(session, 0, sizeof(*session));
}

/* Between frames: the threads stopped and the world gone, if there is one. */
static void game_session_world_close(game_session_t *session) {
    game_t *const game = session->borrowed_game;
    if (game->open == 0u) {
        return;
    }
    if (game->threads_running != 0u && !game_loop_stop(game)) {
        session->failed = 1u;
    }
    game_report(game);
    game_close(game);
}

/* A world's terrain is its seed's and nothing else's, so each opening makes it again. False, having said why, on failure. */
static bool game_session_world_open(game_session_t *session) {
    game_t *const game = session->borrowed_game;
    const ui_menu_world_t *const world = &session->borrowed_ui->menu.chosen;
    const int64_t started_ns = time_now_ns();
    terrain_deinit(session->borrowed_terrain);
    if (!terrain_init(session->borrowed_terrain, world->seed) || !game_open(game, &session->config)) {
        return false;
    }
    LOG_INFO("session: world '%s' is open, from %s, with seed %" PRIu64 ", in %.3f s", world->name, world->directory,
             world->seed, (libc_double_t)time_ns_to_seconds(time_now_ns() - started_ns));
    if (session->borrowed_input != nullptr) {
        game_loop_source_begin(game, session->borrowed_input);
    }
    session->worlds_opened++;
    return true;
}

/* What a frame asked for. False to stop running, for good: something failed, and it has said why. */
static bool game_session_advance(game_session_t *session) {
    const game_session_next_t next = (game_session_next_t)session->next;
    session->next = GAME_SESSION_NEXT_NONE;
    switch (next) {
    case GAME_SESSION_NEXT_OPEN_WORLD:
        if (!game_session_world_open(session)) {
            session->failed = 1u;
            return false;
        }
        session->state = GAME_SESSION_PLAYING;
        return true;
    case GAME_SESSION_NEXT_TITLE:
        game_session_world_close(session);
        session->state = GAME_SESSION_MENU;
        ui_menu_open(&session->borrowed_ui->menu, UI_MENU_SCREEN_MAIN);
        LOG_INFO("session: back on the title screen");
        return session->failed == 0u;
    case GAME_SESSION_NEXT_QUIT:
        session->state = GAME_SESSION_DONE;
        LOG_INFO("session: quit");
        return true;
    case GAME_SESSION_NEXT_NONE:
        break;
    }
    return true;
}

/* One frame of menus, on a copy of picture or on nothing, into target. */
static bool game_session_menu_draw(game_session_t *session, const input_frame_t *frame, bool escape,
                                   const uint8_t *picture, display_buffer_t *target, ui_menu_action_t *out_action) {
    ui_t *const ui = session->borrowed_ui;
    ui_widget_input_feed(&ui->widget, frame);
    const bool started = picture != nullptr ? ui_frame_begin_over(ui, picture, target->width, target->height)
                                            : ui_frame_begin_blank(ui, target->width, target->height);
    if (!started) {
        return false;
    }
    *out_action = ui_menu_frame(&ui->menu, escape);
    const uint8_t *const drawn = ui_frame_end(ui, true);
    const uint64_t row_bytes = (uint64_t)target->width * sizeof(uint32_t);
    copy_stream_rows_batch(target->owned_map, target->pitch, drawn, row_bytes, row_bytes, target->height);
    return true;
}

/* What the menus' answer means. True if a next step is now waiting for the frames to stop. */
static bool game_session_action(game_session_t *session, ui_menu_action_t action) {
    switch (action) {
    case UI_MENU_ACTION_PLAY:
        session->next = GAME_SESSION_NEXT_OPEN_WORLD;
        return true;
    case UI_MENU_ACTION_QUIT_GAME:
        session->next = GAME_SESSION_NEXT_QUIT;
        return true;
    case UI_MENU_ACTION_QUIT_TO_TITLE:
        session->next = GAME_SESSION_NEXT_TITLE;
        return true;
    case UI_MENU_ACTION_RESUME:
        game_pause_set(session->borrowed_game, false);
        session->state = GAME_SESSION_PLAYING;
        return false;
    case UI_MENU_ACTION_NONE:
    case UI_MENU_ACTION_SETTINGS: /* Stage 14.5's screen goes here; the button stays disabled until it does */
        return false;
    }
    return false;
}

static void game_session_pause(game_session_t *session) {
    game_pause_set(session->borrowed_game, true);
    ui_menu_open(&session->borrowed_ui->menu, UI_MENU_SCREEN_PAUSE);
    session->state = GAME_SESSION_PAUSED;
    session->escape_used = 1u;
    session->pauses++;
}

/* The paused or menu frame both ways of running share. False if a frame could not be started, which has been said. */
static bool game_session_screen_frame(game_session_t *session, const input_frame_t *frame, bool escape,
                                      display_buffer_t *back, bool *out_next) {
    const uint8_t *const picture =
        session->state == GAME_SESSION_PAUSED ? session->borrowed_game->borrowed_last_picture : nullptr;
    ui_menu_action_t action = UI_MENU_ACTION_NONE;
    if (!game_session_menu_draw(session, frame, escape && session->escape_used == 0u, picture, back, &action)) {
        session->failed = 1u;
        return false;
    }
    session->escape_used = 0u;
    *out_next = game_session_action(session, action);
    return true;
}

/* ---- Under KMS --------------------------------------------------------------------------------------------------------- */

bool game_session_source(void *context, display_buffer_t *back) {
    game_session_t *const session = context;
    game_t *const game = session->borrowed_game;
    const input_frame_t *const frame = input_poll(session->borrowed_input);
    const bool escape = input_keys_has(&frame->pressed, KEY_ESC);
    bool next = false;
    if (session->next == GAME_SESSION_NEXT_OPEN_WORLD) {
        if (session->loading_frames == 0u) {
            /* The buffer waiting for this frame says loading, and stays up until the world's first picture is in it. */
            return game_session_advance(session);
        }
        session->loading_frames--;
        return game_session_screen_frame(session, frame, false, back, &next);
    }
    if (session->state == GAME_SESSION_PLAYING) {
        if (!game_loop_threads_ensure(game)) {
            session->failed = 1u;
            return false;
        }
        if (!escape || game_screen_up(game)) {
            if (game_loop_input_frame(game, frame, back)) {
                return true;
            }
            session->failed = 1u;
            return false;
        }
        /* Whatever was held stays held in nobody's hands: the sim keeps the last it was told, and is told again on resuming. */
        game_loop_input_clear(game);
        game_session_pause(session);
    }
    if (atomic_load_explicit(&game->failed, memory_order_acquire) != 0u) {
        session->failed = 1u;
        return false;
    }
    if (!game_session_screen_frame(session, frame, escape, back, &next)) {
        return false;
    }
    if (!next) {
        return true;
    }
    if (session->next == GAME_SESSION_NEXT_OPEN_WORLD) {
        session->loading_frames = GAME_SESSION_LOADING_FRAMES;
        ui_menu_open(&session->borrowed_ui->menu, UI_MENU_SCREEN_LOADING);
        return true;
    }
    return game_session_advance(session) && session->state != GAME_SESSION_DONE;
}

void game_session_discard(void *context) {
    game_session_t *const session = context;
    if (session->state == GAME_SESSION_MENU) {
        input_test_pattern_discard(session->borrowed_input);
    } else {
        game_loop_discard(session->borrowed_game);
    }
}

/* A world can take many seconds to open on an unlucky seed, inside one frame, and the watchdog must not take that for a hang. */
static void game_session_open_alive(void *context) {
    display_t *const display = context;
    display_vt_guard_pet(&display->guard);
}

display_status_t game_session_run(game_session_t *session, display_t *display, display_frame_source_t source,
                                  void *context) {
    game_open_alive_set(session->borrowed_game, game_session_open_alive, display);
    const display_status_t status = display_frames_run(display, source, game_session_discard, context);
    game_session_world_close(session);
    game_open_alive_set(session->borrowed_game, nullptr, nullptr);
    return status;
}

bool game_session_end(game_session_t *session) {
    game_session_world_close(session);
    return session->failed == 0u;
}

/* ---- Under --headless -------------------------------------------------------------------------------------------------- */

/* A poll's worth of input out of a script's tick: what input_poll would have said. */
static void game_session_script_frame(const display_headless_tick_t *tick, input_frame_t *out) {
    static_assert(sizeof(tick->state->down) == sizeof(out->down.words));
    memset(out, 0, sizeof(*out));
    memcpy(out->down.words, tick->state->down, sizeof(out->down.words));
    for (uint32_t i = 0u; i < tick->event_count; i++) {
        const display_script_event_t *const event = &tick->events[i];
        if (event->kind == DISPLAY_SCRIPT_KEY) {
            input_keys_t *const edge = event->pressed != 0u ? &out->pressed : &out->released;
            edge->words[event->code / 64u] |= (uint64_t)1 << (event->code % 64u);
        }
    }
    out->mouse_dx = tick->state->tick_dx;
    out->mouse_dy = tick->state->tick_dy;
    out->wheel = tick->state->tick_wheel;
}

static bool game_session_scene_tick(void *context, const display_headless_tick_t *tick) {
    game_session_t *const session = context;
    if (session->state != GAME_SESSION_PLAYING) {
        return true;
    }
    input_frame_t frame;
    game_session_script_frame(tick, &frame);
    if (input_keys_has(&frame.pressed, KEY_ESC) && session->borrowed_game->borrowed_last_picture != nullptr &&
        !game_screen_up(session->borrowed_game)) {
        game_session_pause(session);
        return true;
    }
    display_headless_scene_t scene;
    game_headless_scene(session->borrowed_game, &scene);
    return scene.tick(scene.borrowed_context, tick);
}

static bool game_session_scene_draw(void *context, const display_headless_tick_t *tick, display_buffer_t *frame) {
    game_session_t *const session = context;
    if (session->state == GAME_SESSION_DONE) {
        return true;
    }
    if (session->state == GAME_SESSION_PLAYING) {
        display_headless_scene_t scene;
        game_headless_scene(session->borrowed_game, &scene);
        return scene.draw(scene.borrowed_context, tick, frame);
    }
    input_frame_t input;
    game_session_script_frame(tick, &input);
    bool next = false;
    if (!game_session_screen_frame(session, &input, input_keys_has(&input.pressed, KEY_ESC), frame, &next)) {
        return false;
    }
    return !next || game_session_advance(session);
}

void game_session_headless_scene(game_session_t *session, display_headless_scene_t *out) {
    *out = (display_headless_scene_t){
        .borrowed_context = session, .tick = game_session_scene_tick, .draw = game_session_scene_draw};
}
