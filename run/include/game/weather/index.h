/* See run/src/game/weather/README.md. */
#ifndef MC_RUN_GAME_WEATHER_INDEX_H
#define MC_RUN_GAME_WEATHER_INDEX_H

#include "climate/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "machine/index.h"
#include "render/frame/index.h"
#include "render/index.h"
#include "types/index.h"
#include "world/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 08: the sim's side of the weather the player sees and feels. Every GAME_WEATHER_SAMPLE_TICKS it asks the climate
 * for the weather at the eye (climate_weather_at); every tick it decides lightning in the thunderstorm cells near the player by
 * hash, keeps the rain top map round the eye, and sends all of it to the thread that draws through a triple buffer of slots. The
 * wind it works out is what pushes the body (player_tick_wind). */

enum : uint32_t {
    GAME_WEATHER_SAMPLE_TICKS = 10u,     /* ticks between climate_weather_at calls: half a second */
    GAME_LIGHTNING_RANGE = 2000u,        /* blocks: cells this near can strike where the player sees */
    GAME_LIGHTNING_CLOUD_BASE = 180u,    /* blocks above the ground, at most the world's top */
    GAME_LIGHTNING_JITTER = 8u,          /* blocks sideways a joint may be */
    GAME_LIGHTNING_SLOT_TICKS = 20u,     /* a strike is decided for each second */
    GAME_LIGHTNING_BOLT_TICKS = 4u,      /* 0.2 s: the ticks of a second in which its bolt is seen */
    GAME_LIGHTNING_FORCED_TICKS = 40u,   /* --weather thunder: a strike every 2 seconds */
    GAME_LIGHTNING_FORCED_BLOCKS = 300u, /* and 300 blocks east of the eye */
    GAME_WEATHER_CELLS_MAX = 256u,       /* cells kept from a day's list near the player: more is a bug, said once */
    GAME_WEATHER_CELLS_MOVE = 500u, /* blocks the player goes from where the list was made before it is made again */
    GAME_WEATHER_MAP_MOVE = 8u,     /* blocks the eye goes from the map's middle before it is made again */
};
/* A strike for each second with chance GAME_LIGHTNING_RATE times the cell's strength: a strong cell strikes three times a minute. */
#define GAME_LIGHTNING_RATE 0.05f64

/* What --weather asks for: numbers given in place of the climate's, for golden pictures and the bench. */
typedef enum game_weather_kind_t : uint8_t {
    GAME_WEATHER_SAMPLED = 0, /* the climate's, or nothing with no climate */
    GAME_WEATHER_CLEAR = 1,
    GAME_WEATHER_RAIN = 2,
    GAME_WEATHER_SNOW = 3,
    GAME_WEATHER_SLEET = 4,
    GAME_WEATHER_STORM = 5,
    GAME_WEATHER_THUNDER = 6,
    GAME_WEATHER_FOG = 7,
    GAME_WEATHER_DUST = 8,
    GAME_WEATHER_HURRICANE = 9,
    GAME_WEATHER_KIND_COUNT = 10,
} game_weather_kind_t;

/* A strike: where, and how its bolt goes. */
typedef struct game_weather_strike_t {
    float32_t bolt[POST_BOLT_SEGMENTS + 1u][3]; /* 0: joints from the cloud down, in blocks from origin */
    int32_t origin[3];                          /* 156: the block it strikes, on the ground */
    float32_t age;                              /* 168: seconds since it struck */
    float32_t distance;                         /* 172: blocks from the eye */
} game_weather_strike_t;
static_assert(sizeof(game_weather_strike_t) == 176);

/* The weather's state: the exchange and its slots, the drawing thread's eased numbers, and the sim's. In the game's World arena,
 * beside the column jobs (game_weather_of). */
typedef struct game_weather_t {
    render_exchange_t exchange;                         /* 0: sim to the thread that draws, over slots */
    render_weather_slot_t slots[RENDER_EXCHANGE_SLOTS]; /* 192 */
    uint8_t _pad0[48];                                  /* 25488 */
    /* A line of its own: the thread that draws writes this. */
    render_weather_t gpu; /* 25536 */
    /* A line of its own from here: the sim's. */
    climate_now_t now;                                          /* 25792: the last sample */
    climate_thunder_t cells[GAME_WEATHER_CELLS_MAX];            /* 25856: a day's cells near the player */
    int16_t map[POST_WEATHER_MAP_SIDE * POST_WEATHER_MAP_SIDE]; /* 42240: rain top round the eye, z from sea level */
    float64_t cells_x;                                          /* 50432: where the cells' list was made */
    float64_t cells_y;                                          /* 50440 */
    int64_t cells_day;                                          /* 50448: D of that list */
    uint64_t log_cursor;                                        /* 50456: this module's place in the edit log */
    uint64_t sample_tick;                                       /* 50464: the tick of now */
    uint64_t map_ready_count; /* 50472: the map's ready columns when the map was made */
    float32_t wind_from[2];   /* 50480: the wind blended from, at sample_tick */
    float32_t wind_to[2];     /* 50488: and to, by the next sample */
    float32_t wind[2];        /* 50496: this tick's, for the body */
    int32_t map_origin[2];    /* 50504: the map's lowest corner, whole blocks */
    uint32_t map_revision;    /* 50512 */
    uint32_t cell_count;      /* 50516 */
    uint32_t map_unloaded;    /* 50520: entries of columns not loaded when made */
    uint8_t kind;             /* 50524: game_weather_kind_t */
    uint8_t map_built;        /* 50525 */
    uint8_t sampled;          /* 50526: now holds a sample */
    uint8_t cells_ready;      /* 50527: cells is a whole list */
    uint8_t cells_full_said;  /* 50528 */
    uint8_t _pad1[95];        /* 50529 */
} game_weather_t;
static_assert(sizeof(game_weather_t) == 50624);
static_assert(alignof(game_weather_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_weather_t, gpu) == 25536);
static_assert(offsetof(game_weather_t, now) == 25792);
static_assert(offsetof(game_weather_t, map) == 42240);
static_assert(offsetof(game_weather_t, cells_x) == 50432);
static_assert(offsetof(game_weather_t, kind) == 50524);

/* The kind --weather names, or false for a word it does not know. */
bool game_weather_kind_parse(const char *word, game_weather_kind_t *out);
/* The word for a kind. */
const char *game_weather_kind_name(game_weather_kind_t kind);
/* The fixed weather of a forced kind, as climate_weather_at would give it. */
void game_weather_forced_now(game_weather_kind_t kind, climate_now_t *out);

/* Empties it, with this kind (GAME_WEATHER_SAMPLED to follow the climate), and the exchange started. Before the threads. */
void game_weather_start(game_weather_t *weather, game_weather_kind_t kind);

/* The rain top of one column, z from sea level: its highest voxel that is not air, or POST_WEATHER_MAP_COVERED if it is not loaded,
 * or WORLD_EDIT_TOP_Z_NONE if it is all air. The thread holding the map. */
int16_t game_weather_column_top(world_t *world, int32_t x, int32_t y);

/* The strike of a cell in a one-second slot, if it strikes then: by hash of the seed, the cell's id and the slot, with chance
 * GAME_LIGHTNING_RATE times its strength at t (in days). Its place within the cell's radius, from the same hash. Pure. */
bool game_weather_cell_strikes(uint64_t seed, const climate_thunder_t *cell, uint64_t slot, float64_t t,
                               float64_t out_place[2]);

/* A bolt from the cloud base down to ground at a place, its joints by hash of seed and slot: GAME_LIGHTNING_JITTER at most sideways,
 * the last at the ground exactly. eye is where it is seen from, for its distance. Pure. */
void game_weather_bolt_make(uint64_t seed, uint64_t slot, const float64_t place[2], float64_t ground_z,
                            float64_t world_top, const float64_t eye[3], game_weather_strike_t *out);

/* f = 1 / (1 + (d / RENDER_FLASH_DISTANCE)^2). Pure. */
float32_t game_weather_flash(float32_t distance);

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

typedef struct game_t game_t;

/* The game's weather, beside its column jobs in the World arena, or nullptr with no world open. Implemented in game/index.c, which
 * lays them out. */
game_weather_t *game_weather_of(const game_t *game);

/* --weather: from now on this world's weather is this kind (GAME_WEATHER_SAMPLED for the climate's again), from the next tick. The
 * world must be open and the threads not running. */
void game_weather_force(game_t *game, game_weather_kind_t kind);

/* The sim, at the start of every tick: samples, lightning, the rain top map, the wind, and a slot sent. tick_ns is the tick's own
 * time, which the slot carries for the thread that draws (0 with no threads). */
void game_weather_tick(game_t *game, int64_t tick_ns);

/* This tick's wind on the body, blocks a second: (0, 0) with no weather. The sim. */
void game_weather_wind(const game_t *game, float32_t out[2]);

/* The thread that draws: takes the newest slot, eases toward it, sends the rain top, then draws the frame with it (or without it, with
 * no weather). Anything but GPU_STATUS_OK is the card failing, with frame.gpu.last_error saying why. */
gpu_status_t game_weather_draw(game_t *game, const render_camera_t *camera, int64_t now_ns);

#endif
