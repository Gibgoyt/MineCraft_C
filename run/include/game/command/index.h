/* See run/src/game/command/README.md. */
#ifndef MC_RUN_GAME_COMMAND_INDEX_H
#define MC_RUN_GAME_COMMAND_INDEX_H

#include "input/index.h"
#include "player/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 006 phase 01: the command box, in the Atlas's pattern: T or / opens it over the running game, Enter runs what was typed, Esc
 * closes it, and Up and Down bring back the last 16 commands. The parser is a pure function of the text. */

enum : uint32_t {
    GAME_COMMAND_TEXT_BYTES = 96u, /* one command or one line, with its NUL */
    GAME_COMMAND_HISTORY = 16u,    /* commands Up and Down go back through */
    GAME_COMMAND_HELP_LINES = 11u,
};

/* What a command is. */
typedef enum game_command_kind_t : uint8_t {
    GAME_COMMAND_BAD = 0,
    GAME_COMMAND_TP = 1,         /* target, relative and tp */
    GAME_COMMAND_FLY = 2,        /* fly mode on or off */
    GAME_COMMAND_SPEED = 3,      /* value: the factor, 1 to 10 */
    GAME_COMMAND_SPEED_SHOW = 4, /* /speed alone: say the factor */
    GAME_COMMAND_TIME_DAY = 5,   /* value: the day of the year, 1 to 365, as the HUD shows it */
    GAME_COMMAND_TIME_HOUR = 6,  /* value: the hour of the clock, 0 to 24 */
    GAME_COMMAND_HELP = 7,
} game_command_kind_t;

/* Latitude, y and back: y = DEG / 90 x CLIMATE_POLE_Y, north positive (master 2.6). */
#define GAME_COMMAND_LAT_MOST 90.0f64

/* A parsed command. For a teleport, each axis of target is a place, or with its PLAYER_COMMAND_RELATIVE_* bit a step from the feet;
 * whole numbers without ~ across are a block's middle. Places are held inside PLAYER_COMMAND_PLACE_MOST across and the gameplay heights
 * already; the sim holds what a step comes to the same way. */
typedef struct game_command_parsed_t {
    float64_t target[3];                 /* 0 */
    float64_t value;                     /* 24 */
    uint32_t relative;                   /* 32: PLAYER_COMMAND_RELATIVE_* */
    uint8_t kind;                        /* 36: game_command_kind_t */
    uint8_t tp;                          /* 37: PLAYER_COMMAND_TP_EXACT or PLAYER_COMMAND_TP_GROUND */
    uint8_t latitude;                    /* 38: it was /tp lat, and target[1] is from the latitude */
    uint8_t _pad0[1];                    /* 39 */
    char error[GAME_COMMAND_TEXT_BYTES]; /* 40: why it is bad, for the red line */
} game_command_parsed_t;
static_assert(sizeof(game_command_parsed_t) == 136);

/* The box, in the World arena beside the column jobs (game_command_of): main's alone. */
typedef struct game_command_t {
    char text[GAME_COMMAND_TEXT_BYTES];                          /* 0: what is being typed */
    char history[GAME_COMMAND_HISTORY][GAME_COMMAND_TEXT_BYTES]; /* 96: round, history_next the oldest once full */
    char line[GAME_COMMAND_TEXT_BYTES];                          /* 1632: the preview, or what the last command said */
    char error[GAME_COMMAND_TEXT_BYTES];                         /* 1728: the red line */
    uint32_t history_count;                                      /* 1824: kept, at most GAME_COMMAND_HISTORY */
    uint32_t history_next;                                       /* 1828: where the next one goes */
    uint32_t browse;                                             /* 1832: 0 for the text typed, n for n commands back */
    uint8_t up;                                                  /* 1836 */
    uint8_t help;                                                /* 1837: /help's lines are shown */
    uint8_t _pad0[26];                                           /* 1838 */
} game_command_t;
static_assert(sizeof(game_command_t) == 1864);
static_assert(offsetof(game_command_t, history_count) == 1824);

/* ---- Pure -------------------------------------------------------------------------------------------------------------------- */

/* Reads one command, with or without its leading /. False, with out->error set and nothing else meant, for anything it cannot read. */
bool game_command_parse(const char *text, game_command_parsed_t *out);

/* y from a latitude, and the latitude the climate gives that y: the round trip the preview shows. */
float64_t game_command_lat_y(float64_t degrees);

/* What a command's text means before Enter, for the line over the box: for /tp lat, "y 25532 · 60.0° N". Empty if nothing to say. */
void game_command_preview(const char *text, char *out, uint32_t bytes);

/* The command's history: push one, and the one n back (1 the newest), or nullptr past the oldest. */
void game_command_history_push(game_command_t *command, const char *text);
const char *game_command_history_back(const game_command_t *command, uint32_t n);

/* /help's lines. */
const char *const *game_command_help_lines(void);

/* ---- Wiring into a game ------------------------------------------------------------------------------------------------------ */

typedef struct game_t game_t;

/* The game's box, beside its column jobs in the World arena, or nullptr with no world open. In game/index.c. */
game_command_t *game_command_of(const game_t *game);

/* Nothing typed and nothing kept, for a world opening. */
void game_command_start(game_command_t *command);

/* Main: one poll's input. T or / opens the box when nothing else is up; while up it has every key: typing, Backspace, Up and Down,
 * Enter, which runs it, and Esc, which closes it. True if the box is up after it and had the input. */
bool game_command_input(game_t *game, const input_frame_t *frame);

/* Whether it is up. */
bool game_command_up(const game_t *game);

/* Main: runs one command now, as Enter does. False, with the red line set and nothing changed, for one that is bad or refused. */
bool game_command_run(game_t *game, const char *text);

/* Main: the box over the picture, between ui_frame_begin_over and ui_frame_end. */
void game_command_draw(game_t *game);

/* Main: the HUD's line for flying and the speed factor, "Flying · 116 b/s · speed 5x", or empty when neither. */
void game_command_flight_line(const game_t *game, char *out, uint32_t bytes);

#endif
