/* See README.md. */
#include "game/session/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "simd/copy/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint8_t {
    /* One to draw into each buffer, so that what is on the screen, whichever it is, says so before the long frame. */
    GAME_SESSION_LOADING_FRAMES = DISPLAY_FLIP_BUFFERS,
    /* What a change waits for: a wheel turned over a slider is several, and making a far field anew for each of them would be for nothing. At
     * 120 Hz this is an eighth of a second. */
    GAME_SESSION_APPLY_FRAMES = 15,
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

/* What settings comes to, at the size the session was given. */
static void game_session_config_from_settings(game_session_t *session) {
    const settings_values_t *const values = &session->borrowed_settings->values;
    game_config_t *const config = &session->config;
    config->render_distance = values->render_distance;
    config->full_detail = values->full_detail;
    config->table_chunks = values->full_detail;
    config->features_off = SHADE_FEATURES_PLAY & ~values->effects;
    config->god_rays = (values->effects & SHADE_FEATURE_GOD_RAYS) != 0u ? 1u : 0u;
    config->fov_degrees = values->fov_degrees;
    config->sensitivity_percent = values->sensitivity_percent;
    config->render_scale_percent = values->render_scale_percent;
    config->uncapped = (uint8_t)(values->vsync != 0u ? 0u : 1u);
    config->show_coordinates = values->show_coordinates;
    config->show_date = values->show_date;
}

void game_session_settings_set(game_session_t *session, settings_t *settings) {
    session->borrowed_settings = settings;
    game_session_config_from_settings(session);
    ui_menu_settings_enable(&session->borrowed_ui->menu);
}

void game_session_deinit(game_session_t *session) {
    memset(session, 0, sizeof(*session));
}

/* What a person reads, once the console is back. */
static void game_session_card_gone_say(bool saved_all) {
    LOG_ERROR("session: the card has gone, so nothing more can be drawn by this run. %s Start the program again; if "
              "this comes back, the kernel's log (dmesg) will have the card's side of it",
              saved_all ? "The world was saved first." : "The world's last changes could not be saved.");
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
    game_open_alive_tell(game);
    save_t *const save = game->borrowed_save;
    bool saved_all = true;
    if (save != nullptr && save->world.open != 0u) {
        if (session->leave_unsaved != 0u) {
            saved_all = false;
            LOG_WARN("session: left the world with what changed since its last save given up");
        } else if (session->world_saved == 0u) {
            const save_world_status_t saved = game_save_finish(game);
            if (saved != SAVE_WORLD_OK) {
                LOG_ERROR("session: the world's changes could not all be saved: %s", save_world_status_name(saved));
                session->failed = 1u;
                saved_all = false;
            }
        }
        LOG_INFO("save: %" PRIu64 " saves of %" PRIu64 " sections, %" PRIu64 " sections read back, %" PRIu64
                 " past reading",
                 save->world.saves_done, save->world.sections_written,
                 atomic_load_explicit(&save->world.sections_loaded, memory_order_relaxed),
                 atomic_load_explicit(&save->world.sections_damaged, memory_order_relaxed));
        LOG_INFO("autosave: %" PRIu64 " saves, %" PRIu64
                 " failed; the longest took %.3f ms of the sim and %.3f ms of a worker",
                 save->autosave.saves_done, save->autosave.saves_failed,
                 (libc_double_t)time_ns_to_ms(save->autosave.capture_ns_most),
                 (libc_double_t)time_ns_to_ms(save->autosave.write_ns_most));
    }
    session->leave_unsaved = 0u;
    session->world_saved = 0u;
    const bool lost = game_gpu_lost(game);
    if (game_gpu_stuck(game)) {
        game_session_card_gone_say(saved_all);
        return; /* whatever the gpu thread is holding stays, and the process ends: see game_gpu_stuck */
    }
    game_open_alive_tell(game);
    game_close(game);
    if (save != nullptr) {
        save_close(save);
    }
    if (lost) {
        game_session_card_gone_say(saved_all); /* last, past what closing logs of the card's side */
    }
}

/* The world's files, if this game keeps any. Anything but SAVE_WORLD_OK has said why, in the log, and they are left as they were. */
static save_world_status_t game_session_save_open(const game_session_t *session, const ui_menu_world_t *world) {
    save_t *const save = session->borrowed_game->borrowed_save;
    if (save == nullptr) {
        return SAVE_WORLD_OK;
    }
    char directory[FILE_PATH_BYTES];
    const int32_t written =
        snprintf(directory, sizeof(directory), "%s/%s", session->borrowed_ui->menu.borrowed_saves, world->directory);
    if (written <= 0 || written >= FILE_PATH_BYTES) {
        LOG_ERROR("session: the path of world '%s' is too long", world->name);
        return SAVE_WORLD_ERROR_PATH;
    }
    const save_world_status_t status = save_open(save, directory, world->seed);
    if (status != SAVE_WORLD_OK) {
        LOG_ERROR("session: world '%s' will not be opened: %s", world->name, save_world_status_name(status));
    }
    return status;
}

/* A world whose files say no is that world's trouble and not the program's: back to the list, which says why, for a person to choose another. Under KMS the log is
 * nothing anyone can read. */
static void game_session_world_refused(game_session_t *session, save_world_status_t status) {
    ui_menu_t *const menu = &session->borrowed_ui->menu;
    ui_menu_open(menu, UI_MENU_SCREEN_SELECT);
    char message[UI_MENU_MESSAGE_BYTES];
    (void)snprintf(message, sizeof(message), "Could not open it: %s", save_world_status_name(status));
    ui_menu_message_set(menu, message);
    session->state = GAME_SESSION_MENU;
}

/* A world's terrain is its seed's and nothing else's, so each opening makes it again. False, having said why, if the program cannot go on. True and still on the menus
 * if only this world's files were refused. */
static bool game_session_world_open(game_session_t *session) {
    game_t *const game = session->borrowed_game;
    const ui_menu_world_t *const world = &session->borrowed_ui->menu.chosen;
    const int64_t started_ns = time_now_ns();
    if (session->borrowed_settings != nullptr) {
        game_session_config_from_settings(session);
    }
    terrain_deinit(session->borrowed_terrain);
    game_open_alive_tell(game);
    if (!terrain_init(session->borrowed_terrain, world->seed)) {
        return false;
    }
    game_open_alive_tell(game);
    const save_world_status_t saved = game_session_save_open(session, world);
    game_open_alive_tell(game);
    if (saved != SAVE_WORLD_OK) {
        game_session_world_refused(session, saved);
        return true;
    }
    /* A person is here to answer the census screen, so a census stops the world and waits for them. */
    game_census_screens_set(game, true);
    session->census_runs_seen = 0u;
    if (!game_open(game, &session->config)) {
        if (game->borrowed_save != nullptr) {
            save_close(game->borrowed_save);
        }
        return false;
    }
    LOG_INFO("session: world '%s' is open, from %s, with seed %" PRIu64 ", in %.3f s", world->name, world->directory,
             world->seed, (libc_double_t)time_ns_to_seconds(time_now_ns() - started_ns));
    if (session->borrowed_input != nullptr) {
        game_loop_source_begin(game, session->borrowed_input);
    }
    session->worlds_opened++;
    session->state = GAME_SESSION_PLAYING;
    return true;
}

/* Tried before a world is closed, as a full disk may yet be emptied by a person with the world still open. True to go on and close it, with nothing more to
 * save. False if it stays open, on the pause screen, which says why and offers a way out. */
static bool game_session_leave_saved(game_session_t *session) {
    game_t *const game = session->borrowed_game;
    save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u || session->leave_unsaved != 0u) {
        return true;
    }
    if (game->threads_running != 0u && !game_loop_stop(game)) {
        session->failed = 1u; /* the card: this world is over, and closing it makes the one last try */
        return true;
    }
    game_open_alive_tell(game);
    const save_world_status_t saved = game_save_finish(game);
    if (saved == SAVE_WORLD_OK) {
        session->world_saved = 1u;
        return true;
    }
    LOG_ERROR("session: the world's changes could not all be saved, so it stays open: %s",
              save_world_status_name(saved));
    /* The threads start again on Resume. Nothing is lost by the wait: the world's time has stopped, in a pause. */
    save_autosave_resume(&save->autosave, game->tick_count);
    ui_menu_t *const menu = &session->borrowed_ui->menu;
    const char *const trouble = save_autosave_trouble(&save->autosave);
    char message[UI_MENU_MESSAGE_BYTES];
    (void)snprintf(message, sizeof(message), "Not saved: %s. Free some space and quit again",
                   trouble != nullptr ? trouble : save_world_status_name(saved));
    ui_menu_message_set(menu, message);
    ui_menu_leave_offer(menu);
    return false;
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
        return true;
    case GAME_SESSION_NEXT_TITLE:
        if (!game_session_leave_saved(session)) {
            return true;
        }
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

/* ---- The Settings screen ----------------------------------------------------------------------------------------------- */

/* What the screen did this frame. Acted on after the frame ends, as anything it does may take the picture away from under it. */
static void game_session_settings_answer(game_session_t *session, ui_screen_settings_action_t action) {
    if (action == UI_SCREEN_SETTINGS_CHANGED) {
        session->apply_frames = GAME_SESSION_APPLY_FRAMES;
    } else if (action == UI_SCREEN_SETTINGS_DONE) {
        session->apply_frames = 1u;
        session->settings_up = 2u; /* leaves once that is done */
    }
}

static void game_session_settings_open(game_session_t *session) {
    ui_t *const ui = session->borrowed_ui;
    const game_t *const game = session->borrowed_game;
    const uint32_t live_max = game->open != 0u ? game->borrowed_world->map.table_radius - WORLD_MAP_KEEP_EXTRA : 0u;
    ui_screen_settings_open(&ui->settings_screen, &ui->widget, &session->borrowed_settings->values, live_max,
                            session->borrowed_game->borrowed_job->worker_count, session->state == GAME_SESSION_PAUSED);
    session->settings_up = 1u;
    session->save_failed = 0u;
}

/* Hands what has changed on: to the world if one is open, and to the display. False if the card failed, having said why. */
static bool game_session_settings_apply(game_session_t *session) {
    game_session_config_from_settings(session);
    audio_t *const audio = session->borrowed_game->borrowed_audio;
    const uint32_t volume = session->borrowed_settings->values.sound_volume_percent;
    /* With a sound to judge it by, as this comes when a hand lets go of the slider. */
    if (audio != nullptr && audio_volume_get(audio) != volume) {
        audio_volume_set(audio, volume, true);
    }
    if (session->borrowed_display != nullptr) {
        display_flip_vsync_set(&session->borrowed_display->flip, session->config.uncapped == 0u);
    }
    game_t *const game = session->borrowed_game;
    return game->open == 0u || game_reconfigure(game, &session->config);
}

/* Keeps them, and leaves unless that failed for the first time, when it says so and stays. */
static void game_session_settings_leave(game_session_t *session) {
    ui_t *const ui = session->borrowed_ui;
    const file_status_t status = settings_save(session->borrowed_settings);
    if (status != FILE_OK && session->save_failed == 0u) {
        char message[UI_SCREEN_SETTINGS_MESSAGE_BYTES];
        (void)snprintf(message, sizeof(message), "Could not keep them: %s", file_status_name(status));
        ui_screen_settings_message_set(&ui->settings_screen, message);
        session->save_failed = 1u;
        session->settings_up = 1u;
        return;
    }
    session->settings_up = 0u;
}

/* After a frame of the screen: what its changes have waited for, and leaving. A person's hand still on a slider puts off the first. */
static bool game_session_settings_step(game_session_t *session) {
    if (session->settings_up == 0u) {
        return true;
    }
    const bool held = session->borrowed_ui->widget.active_id != 0u && session->settings_up == 1u;
    if (session->apply_frames != 0u && !held) {
        session->apply_frames--;
        if (session->apply_frames == 0u && !game_session_settings_apply(session)) {
            return false;
        }
    }
    if (session->settings_up == 2u) {
        game_session_settings_leave(session);
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
    if (session->state == GAME_SESSION_CENSUS) {
        if (ui_screen_census_frame(&ui->census_screen, &ui->widget, escape) == UI_SCREEN_CENSUS_CONTINUE) {
            game_pause_set(session->borrowed_game, false);
            session->state = GAME_SESSION_PLAYING;
        }
    } else if (session->settings_up != 0u) {
        game_session_settings_answer(session, ui_screen_settings_frame(&ui->settings_screen, escape));
    } else {
        *out_action = ui_menu_frame(&ui->menu, escape);
    }
    const uint8_t *const drawn = ui_frame_end(ui, true);
    const uint64_t row_bytes = (uint64_t)target->width * sizeof(uint32_t);
    copy_stream_rows_batch(target->owned_map, target->pitch, drawn, row_bytes, row_bytes, target->height);
    return true;
}

/* ---- The census screen ------------------------------------------------------------------------------------------------ */

/* The sim has stopped the world for the census of week: the screen comes up over its last picture. The world is paused already;
 * game_pause_set again starts the frame clock afresh, so the time on the screen is not owed afterwards. */
static void game_session_census_open(game_session_t *session, uint64_t week) {
    game_t *const game = session->borrowed_game;
    ui_t *const ui = session->borrowed_ui;
    game_loop_input_clear(game);
    game_pause_set(game, true);
    /* census_runs is the sim's, written before the latch's release, which game_census_take acquired. */
    const uint32_t weeks = game->census_runs - session->census_runs_seen;
    session->census_runs_seen = game->census_runs;
    ui_screen_census_open(&ui->census_screen, &ui->widget, game->day_zero,
                          atomic_load_explicit(&game->clock_units, memory_order_relaxed), weeks,
                          game_calendar_south(game));
    LOG_INFO("session: the census screen for week %" PRIu64 " is up, after %" PRIu32 " weeks", week, weeks);
    session->state = GAME_SESSION_CENSUS;
    /* The Esc that may have come with it is not also an answer to it. */
    session->escape_used = 1u;
}

/* True if a census had stopped the world, and its screen is now up. */
static bool game_session_census_look(game_session_t *session) {
    uint64_t week = 0u;
    if (!game_census_take(session->borrowed_game, &week)) {
        return false;
    }
    game_session_census_open(session, week);
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
    case UI_MENU_ACTION_LEAVE_UNSAVED:
        session->leave_unsaved = 1u;
        session->next = GAME_SESSION_NEXT_TITLE;
        return true;
    case UI_MENU_ACTION_RESUME:
        /* A census may have stopped the world while the pause screen was up, even in the frame Esc came: its screen first. */
        if (game_session_census_look(session)) {
            return false;
        }
        game_pause_set(session->borrowed_game, false);
        session->state = GAME_SESSION_PLAYING;
        return false;
    case UI_MENU_ACTION_SETTINGS:
        game_session_settings_open(session);
        return false;
    case UI_MENU_ACTION_NONE:
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
    /* The screen a person comes to in order to leave, so the one that should say what leaving would come to. */
    const save_t *const save = session->borrowed_game->borrowed_save;
    const char *const trouble =
        save != nullptr && save->world.open != 0u ? save_autosave_trouble(&save->autosave) : nullptr;
    if (trouble != nullptr) {
        char message[UI_MENU_MESSAGE_BYTES];
        (void)snprintf(message, sizeof(message), "Not saving: %s", trouble);
        ui_menu_message_set(&session->borrowed_ui->menu, message);
    }
}

/* A keyboard or mouse gone: a world with a person in it stops for them, as what they had held has just been let go of and they may have no way to reach Esc. On
 * the screen, where they can read it, until it is back. Plugging it back in does not end a pause: that is for a person to do, with the hands they now have. */
static void game_session_devices_look(game_session_t *session) {
    const uint8_t now = input_kinds_now(session->borrowed_input);
    const uint8_t gone = (uint8_t)(session->input_kinds & ~now);
    session->input_kinds = now;
    ui_menu_t *const menu = &session->borrowed_ui->menu;
    if (gone != 0u) {
        if (session->state == GAME_SESSION_PLAYING && session->next == GAME_SESSION_NEXT_NONE) {
            game_loop_input_clear(session->borrowed_game);
            game_session_pause(session);
        }
        session->kinds_missing |= gone;
    }
    if (session->kinds_missing == 0u) {
        return;
    }
    if ((session->kinds_missing & ~now) == 0u) {
        session->kinds_missing = 0u;
        ui_menu_message_set(menu, "");
    } else if (gone != 0u) {
        ui_menu_message_set(menu, (session->kinds_missing & ~now) == INPUT_KIND_MOUSE
                                      ? "The mouse has gone: plug one in"
                                      : ((session->kinds_missing & ~now) == INPUT_KIND_KEYBOARD
                                             ? "The keyboard has gone: plug one in"
                                             : "Keyboard and mouse have gone: plug them in"));
    }
}

/* The paused or menu frame both ways of running share. False if a frame could not be started, which has been said. */
static bool game_session_screen_frame(game_session_t *session, const input_frame_t *frame, bool escape,
                                      display_buffer_t *back, bool *out_next) {
    game_t *const game = session->borrowed_game;
    /* Behind this screen a person is looking for what a change has done. Behind the others a pause costs nothing. */
    if (session->state == GAME_SESSION_PAUSED && session->settings_up != 0u && !game_view_refresh(game)) {
        session->failed = 1u;
        return false;
    }
    const bool over_world = session->state == GAME_SESSION_PAUSED || session->state == GAME_SESSION_CENSUS;
    const uint8_t *const picture = over_world ? game_picture_held(game) : nullptr;
    ui_menu_action_t action = UI_MENU_ACTION_NONE;
    if (!game_session_menu_draw(session, frame, escape && session->escape_used == 0u, picture, back, &action)) {
        session->failed = 1u;
        return false;
    }
    session->escape_used = 0u;
    if (!game_session_settings_step(session)) {
        session->failed = 1u;
        return false;
    }
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
    game_session_devices_look(session);
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
        /* A census that has stopped the world makes this frame the census screen's. */
        if (!game_session_census_look(session)) {
            if (!escape || game_screen_up(game)) {
                if (game_loop_input_frame(game, frame, back)) {
                    return true;
                }
                session->failed = 1u;
                return false;
            }
            /* Whatever was held stays held in nobody's hands: the sim keeps the last it was told, and is told again on
             * resuming. */
            game_loop_input_clear(game);
            game_session_pause(session);
        }
    }
    (void)game_gpu_alive(game); /* it says so, and that is failed */
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

/* A world can take many seconds to open on an unlucky seed, inside one frame. The watchdog must not take that for a hang, and a person who asks for another VT
 * meanwhile must not wait for the end of it. */
static void game_session_open_alive(void *context) {
    display_t *const display = context;
    display_long_work(display);
}

static bool game_session_run_source(void *context, display_buffer_t *back) {
    game_session_t *const session = context;
    return session->source(session->borrowed_source_context, back);
}

display_status_t game_session_run(game_session_t *session, display_t *display, display_frame_source_t source,
                                  void *context) {
    session->source = source;
    session->borrowed_source_context = context;
    game_open_alive_set(session->borrowed_game, game_session_open_alive, display);
    session->borrowed_display = display;
    display_flip_vsync_set(&display->flip, session->config.uncapped == 0u);
    const display_status_t status = display_frames_run(display, game_session_run_source, game_session_discard, session);
    game_session_world_close(session);
    game_open_alive_set(session->borrowed_game, nullptr, nullptr);
    session->borrowed_display = nullptr;
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
    const bool ticked = scene.tick(scene.borrowed_context, tick);
    /* The next draw shows the census, if this tick's sim began one. */
    (void)game_session_census_look(session);
    return ticked;
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
