/* See run/src/game/session/README.md. */
#ifndef MC_RUN_GAME_SESSION_INDEX_H
#define MC_RUN_GAME_SESSION_INDEX_H

#include "display/index.h"
#include "game/index.h"
#include "input/index.h"
#include "settings/index.h"
#include "terrain/index.h"
#include "ui/index.h"

#include <stdint.h>

typedef enum game_session_state_t : uint8_t {
    GAME_SESSION_MENU = 0,    /* the main screen and what it leads to, with no world */
    GAME_SESSION_PLAYING = 1, /* a world open, and the player in it */
    GAME_SESSION_PAUSED =
        2,                 /* a world open, with no time passing in it, and its last picture behind the pause screen */
    GAME_SESSION_DONE = 3, /* Quit Game: nothing is left to do */
    /* a world open with its clock stopped by a census, and the census screen over its last picture: plan 004 phase 01 */
    GAME_SESSION_CENSUS = 4,
} game_session_state_t;

/* What a frame asked for, which a later one does. */
typedef enum game_session_next_t : uint8_t {
    GAME_SESSION_NEXT_NONE = 0,
    GAME_SESSION_NEXT_OPEN_WORLD = 1,
    GAME_SESSION_NEXT_TITLE = 2,
    GAME_SESSION_NEXT_QUIT = 3,
} game_session_next_t;

/* The game from the title screen to Quit Game. It owns nothing and holds no memory: it decides when the others open and close. */
typedef struct game_session_t {
    game_t *borrowed_game;         /* 0 */
    ui_t *borrowed_ui;             /* 8 */
    terrain_t *borrowed_terrain;   /* 16 */
    input_t *borrowed_input;       /* 24: nullptr under --headless, where a script is the input */
    uint64_t worlds_opened;        /* 32 */
    uint64_t pauses;               /* 40 */
    game_config_t config;          /* 48: what each world opens with */
    settings_t *borrowed_settings; /* 88: nullptr for none, and no Settings button */
    display_t *borrowed_display;   /* 96: under KMS, while it runs */
    uint8_t state;                 /* 104: game_session_state_t */
    uint8_t next;                  /* 105: game_session_next_t */
    uint8_t failed;                /* 106: a world would not open or its threads failed, having said why */
    uint8_t escape_used;           /* 107: this Esc paused the game, and must not also resume it */
    uint8_t loading_frames;        /* 108: of the loading screen still to show, under KMS, before a world opens */
    uint8_t settings_up;           /* 109: the Settings screen is what shows, over whichever screen brought it up */
    uint8_t apply_frames;          /* 110: frames until what was changed goes to the world open; 0 for nothing to do */
    uint8_t save_failed;   /* 111: Done has failed to keep them once and said so, so another Done leaves anyway */
    uint8_t input_kinds;   /* 112: INPUT_KIND_*: what was plugged in, last frame */
    uint8_t kinds_missing; /* 113: INPUT_KIND_*: what has gone, and the screen is saying so until it is back */
    uint8_t leave_unsaved; /* 114: Leave Unsaved was chosen: what could not be saved is given up */
    uint8_t world_saved;   /* 115: all of this world is on disk, and closing it has nothing more to save */
    uint32_t
        census_runs_seen; /* 116: game_t.census_runs when the last census screen opened, to say how many weeks the next ran */
    display_frame_source_t source; /* 120: under KMS, while it runs: what each frame calls */
    void *
        borrowed_source_context; /* 128: and with what. What comes back from another VT is the session's, whatever a test has wrapped around the source */
} game_session_t;
static_assert(sizeof(game_session_t) == 136);

/* Starts on the main screen. All of these must outlive it, game must have its player set, and saves is where worlds live.
 * blank_seed is what a world made with no seed gets. input is nullptr under --headless. */
void game_session_init(game_session_t *session, game_t *game, ui_t *ui, terrain_t *terrain, input_t *input,
                       const game_config_t *config, const char *saves, uint64_t blank_seed);

/* Settings from here on: its Settings buttons work, and each world opens with what settings holds, at the size in config. Before anything has
 * run. settings must outlive it. */
void game_session_settings_set(game_session_t *session, settings_t *settings);

/* Forgets all that. Whatever it opened has been closed by game_session_run or game_session_end. */
void game_session_deinit(game_session_t *session);

/* What display_frames_run calls, with a game_session_t * as context: one poll, then one frame of whatever is on. A world opens and
 * closes in here, with a loading screen up first, and those are the only frames that log or take long: see the README. False once
 * Quit Game has been chosen, or something has failed. */
bool game_session_source(void *context, display_buffer_t *back);

/* What display_frames_run calls on coming back from another VT, with a game_session_t * as context. */
void game_session_discard(void *context);

/* Runs it all through KMS, until Quit Game, a stop signal or a failure, on one taking of the console. source is game_session_source
 * with the session as context; a test may wrap it, to see each frame. Closes any world before it returns. */
display_status_t game_session_run(game_session_t *session, display_t *display, display_frame_source_t source,
                                  void *context);

/* The same without a display, for display_headless_run: a script is the input, one thread does everything, and nothing reads a
 * clock, so one script gives one set of pictures. After Quit Game the rest of the script does nothing. */
void game_session_headless_scene(game_session_t *session, display_headless_scene_t *out);

/* Stops and closes any world still open, for after display_headless_run. False if anything in the session failed. */
bool game_session_end(game_session_t *session);

#endif
