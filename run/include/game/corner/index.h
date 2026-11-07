/* See run/src/game/corner/README.md. */
#ifndef MC_RUN_GAME_CORNER_INDEX_H
#define MC_RUN_GAME_CORNER_INDEX_H

#include "climate/index.h"
#include "climate/label/index.h"
#include "climate/warning/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "types/index.h"
#include "ui/hud/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 10: the HUD's top right corner while walking. The sim works out the temperature at the body's height, the wind,
 * the season and the storm warning, and packs each into a word; main turns the words into text for ui_hud_corner_draw. */

enum : uint32_t {
    GAME_CORNER_CLIMATE_TICKS = 20u,         /* the sim works out the climate line this often: 1 s at 20 Hz */
    GAME_CORNER_SEASON_READY = 1u << 16,     /* in hud_season: set once worked out */
    GAME_CORNER_WARNING_SHIFT = 56u,         /* in hud_warning: the kind's bits */
    GAME_CORNER_WARNING_HURRICANE = 1u << 0, /* in hud_warning's bits 32 to 55: a hurricane now, not a tropical storm */
    GAME_CORNER_TEXT_BYTES = 64u,            /* the climate line or the warning, with its NUL */
};
/* Below this speed, in blocks a second, the line says "wind calm" and has no arrow. */
#define GAME_CORNER_CALM 0.5f32

/* The storm warning's job: what it is asked and what it found. The sim fills it and submits; the job writes the answer and then
 * state, release; the sim takes the answer after reading state, acquire. Its own line. */
typedef struct game_corner_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const climate_t *borrowed_climate; /* 0 */
    float64_t x;                                                        /* 8: the body's feet */
    float64_t y;                                                        /* 16 */
    float64_t t;                                                        /* 24: D + f */
    climate_warning_t answer;                                           /* 32 */
    _Atomic uint32_t state;                                             /* 56: GAME_CORNER_JOB_* */
    uint8_t ok;                                                         /* 60: climate_warning_at's answer was whole */
    uint8_t _pad0[3];                                                   /* 61 */
} game_corner_job_t;
static_assert(sizeof(game_corner_job_t) == MC_CPU_CACHE_LINE_BYTES);

enum : uint32_t {
    GAME_CORNER_JOB_IDLE = 0u,
    GAME_CORNER_JOB_RUNNING = 1u,
    GAME_CORNER_JOB_DONE = 2u,
};

/* The words, and the warning's job, in the World arena beside the column jobs (game_corner_of), as game_t is at its size. The
 * sim writes the words, and main reads the four atomics, relaxed: only what is on the screen depends on them, each word stays
 * whole, and a frame of a word a tick old beside a new one is gone by the next. */
typedef struct game_corner_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic int32_t hud_temperature_dc; /* 0: T_HUD in tenths of a degree C */
    /* 4: speed in tenths of a block a second, bits 0-15; the direction it blows toward, bits 16-31, in 65536ths of a turn
     * anticlockwise from east */
    _Atomic uint32_t hud_wind;
    _Atomic uint32_t
        hud_season; /* 8: climate_season_label_t: season bits 0-7, also bits 8-15; GAME_CORNER_SEASON_READY */
    uint32_t warning_week_seen; /* 12: the sim's: 1 + the week the warning was last worked out in; 0 for never */
    /* 16: climate_warning_kind_t in bits 56-63; for a named one, GAME_CORNER_WARNING_HURRICANE in bits 32-55 and the name seed in
     * bits 0-31 */
    _Atomic uint64_t hud_warning;
    uint64_t climate_tick;  /* 24: the sim's: the tick the climate line was last worked out on */
    uint64_t warning_tick;  /* 32: the sim's: the tick the warning was last worked out on */
    uint64_t respawns_seen; /* 40: the sim's: the body's respawns when last looked */
    uint8_t climate_done;   /* 48: the sim's: the climate line has been worked out at least once */
    uint8_t warning_done;   /* 49: the sim's: the warning has been worked out at least once */
    uint8_t _pad0[14];      /* 50 */
    job_group_t group;      /* 64: the warning's job, so whoever closes the world can wait for it */
    game_corner_job_t job;  /* 128 */
} game_corner_t;
static_assert(sizeof(game_corner_t) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_corner_t, hud_warning) == 16);
static_assert(offsetof(game_corner_t, group) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_corner_t, job) == 2u * MC_CPU_CACHE_LINE_BYTES);

/* Nothing worked out yet, and no job. Before the threads. */
void game_corner_start(game_corner_t *corner);

/* Waits for the warning's job, if one is running. Whoever closes the world, with the threads stopped, before its memory goes. Any
 * thread that is not a worker. */
void game_corner_stop(game_corner_t *corner, const job_system_t *job);

/* ---- The words, pure --------------------------------------------------------------------------------------------------- */

/* A wind (u east, v north, blocks a second) as hud_wind. */
uint32_t game_corner_wind_pack(float32_t u, float32_t v);
/* Its speed, blocks a second, and the direction it blows toward, radians anticlockwise from east, from 0 to 2 pi. */
void game_corner_wind_unpack(uint32_t word, float32_t *out_speed, float32_t *out_toward);
/* The compass point it blows from, of 8, each 45 degrees wide, N from 337.5 to 22.5: atan2(-u, -v) clockwise from north. */
const char *game_corner_compass(float32_t toward);
/* The arrow on the screen, x right and y down, for a wind blowing toward `toward` seen by an eye of this yaw (radians, its
 * facing (cos yaw, sin yaw)): straight up is blowing the way the eye faces. alpha = toward - yaw; (-sin alpha, -cos alpha). */
void game_corner_arrow(float32_t toward, float32_t yaw, float32_t out[2]);
/* "31.2 °C  wind 6.1 m/s from SW", or "31.2 °C  wind calm"; a temperature of -0.04 is "0.0", never "-0.0". */
void game_corner_climate_text(int32_t temperature_dc, uint32_t wind, char *out, uint32_t bytes);
/* A warning as hud_warning, and back to its text: "Storm warning: Hurricane Tavela", "Storm warning: hurricane likely",
 * "Storm warning: storm", or "" for none. */
uint64_t game_corner_warning_pack(const climate_warning_t *warning);
void game_corner_warning_text(uint64_t word, char *out, uint32_t bytes);

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

typedef struct game_t game_t;

/* The game's words, beside its column jobs in the World arena, or nullptr with no world open. In game/index.c. */
game_corner_t *game_corner_of(const game_t *game);

/* The sim's, from game_hud_publish each tick: every GAME_CORNER_CLIMATE_TICKS, and at once when force is set or the body has come
 * back from death, the temperature at the body's height (T now at the ground there, less Gamma times the height above it), the
 * wind now and the season; every CLIMATE_WARNING_TICKS, and on the first tick of a new census week, the storm warning, worked out
 * on a worker (climate_warning_at takes milliseconds) and published on the first tick after it is done. A value whose tile is not
 * built yet keeps the last one. Nothing with no body or no climate. Whoever opens a world also calls it once, with force, before
 * the threads start. */
void game_hud_climate_publish(game_t *game, bool force);

/* Main's: the corner's text from the words and the config, with main's own eye for the arrow. Line 0 is always empty; line 1 is
 * the date with the season once worked out (else phase 01's season), if show_date is on; line 2 the climate line if its bit is on
 * and it was ever worked out; line 3 the warning if its bit is on and there is one. Never waits. */
void game_hud_corner_read(const game_t *game, ui_hud_corner_t *out);

#endif
