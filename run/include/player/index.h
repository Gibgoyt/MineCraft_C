/* See run/src/player/README.md. */
#ifndef MC_RUN_PLAYER_INDEX_H
#define MC_RUN_PLAYER_INDEX_H

#include "machine/index.h"
#include "physics/index.h"
#include "player/movement/index.h"
#include "render/frame/index.h"
#include "world/map/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : int64_t {
    PLAYER_TICK_NS = 1000000000 / MC_GAME_TICK_HZ,
    /* An intent older than this means no one is at the controls: main has stopped, and most likely gone to another VT. */
    PLAYER_INTENT_STALE_NS = 250000000,
};

/* One tick's worth of where the eye was and is, from sim to main. */
typedef struct player_view_t {
    float64_t previous[3]; /* 0 */
    float64_t current[3];  /* 24 */
    int64_t tick_ns;       /* 48: when this tick was due */
    uint64_t tick;         /* 56: which one, counting from 1, and 0 before any */
} player_view_t;
static_assert(sizeof(player_view_t) == 64);

/* The player. There is one, under app_t. The sim writes everything up to the last line, and main that line. */
typedef struct player_t {
    player_movement_t movement;                      /* 0: the sim's */
    physics_t *borrowed_physics;                     /* 112 */
    uint64_t tick_count;                             /* 120: the sim's */
    render_exchange_t views;                         /* 128: sim to main, over view_slots */
    player_view_t view_slots[RENDER_EXCHANGE_SLOTS]; /* 320 */
    /* Main to sim. None of the three carries a pointer or vouches for anything written before it, so all are relaxed. */
    _Atomic uint64_t intent_word;   /* 512: the newest player_intent_t, packed */
    _Atomic int64_t intent_sent_ns; /* 520 */
    _Atomic uint32_t jump_latch;    /* 528: a jump pressed since the sim last looked */
    uint32_t view_slot;             /* 532: main's: the slot it reads */
    uint8_t _pad0[40];              /* 536 */
} player_t;
static_assert(sizeof(player_t) == 576);
static_assert(alignof(player_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(player_t, views) == 128);
static_assert(offsetof(player_t, intent_word) == 512);

/* Remembers what it moves with, and takes nothing. Physics must outlive it. */
void player_init(player_t *player, physics_t *physics);

/* Safe on a zeroed player_t. */
void player_deinit(player_t *player);

/* A body at rest, its feet at this place, with nothing wanted yet. Before the threads start. */
void player_spawn(player_t *player, float64_t x, float64_t y, float64_t z);

/* What the player wants now. Main, as often as it likes, with now_ns from the same clock as the ticks'. A jump pressed between
 * two ticks still jumps. Never waits. */
void player_send(player_t *player, const player_intent_t *intent, int64_t now_ns);

/* One tick. The sim, or whichever one thread holds the map, with tick_ns the time this tick was due. Never waits and never
 * touches the heap. */
void player_tick(player_t *player, const world_map_t *map, int64_t tick_ns);

/* Where the eye is to be drawn at now_ns: from a tick's start to its end, over the tick after it was due, so that a picture
 * moves smoothly at any rate and is never more than a tick behind. Main. Never waits. */
void player_eye_at(player_t *player, int64_t now_ns, float64_t out[3]);

#endif
