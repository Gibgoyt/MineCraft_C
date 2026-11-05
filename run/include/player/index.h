/* See run/src/player/README.md. */
#ifndef MC_RUN_PLAYER_INDEX_H
#define MC_RUN_PLAYER_INDEX_H

#include "machine/index.h"
#include "physics/index.h"
#include "player/camera/index.h"
#include "player/combat/index.h"
#include "player/felling/index.h"
#include "player/interaction/index.h"
#include "player/movement/index.h"
#include "player/survival/index.h"
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

/* How far past its body the player picks things up: the published numbers. */
#define PLAYER_PICKUP_REACH_ACROSS 1.0f64
#define PLAYER_PICKUP_REACH_UP 0.5f64

/* The two buttons, in hands_word and hands_latch. */
enum : uint32_t {
    PLAYER_HAND_ATTACK = 1u,
    PLAYER_HAND_PLACE = 2u,
};

/* One tick's worth of where the eye was and is and what it aims at, from sim to main. */
typedef struct player_view_t {
    float64_t previous[3];  /* 0 */
    float64_t current[3];   /* 24 */
    int64_t tick_ns;        /* 48: when this tick was due */
    uint64_t tick;          /* 56: which one, counting from 1, and 0 before any */
    player_target_t target; /* 64 */
} player_view_t;
static_assert(sizeof(player_view_t) == 160);

/* A body's health as main may see it, for a screen to draw. Goes across in one word, so that it stays whole. */
typedef struct player_vitals_t {
    float32_t health; /* 0 */
    uint16_t air;     /* 4: ticks left, and 0 when drowning */
    uint8_t food;     /* 6 */
    uint8_t state;    /* 7: player_survival_state_t */
} player_vitals_t;
static_assert(sizeof(player_vitals_t) == 8);

/* The player. There is one, under app_t. The sim writes everything up to the intent, and main the rest up to survival. */
typedef struct player_t {
    player_movement_t movement;                      /* 0: the sim's */
    physics_t *borrowed_physics;                     /* 112 */
    uint64_t tick_count;                             /* 120: the sim's */
    render_exchange_t views;                         /* 128: sim to main, over view_slots */
    player_view_t view_slots[RENDER_EXCHANGE_SLOTS]; /* 320 */
    player_target_t target;                          /* 800: the sim's: what it aims at, as of the last tick */
    player_interaction_t interaction;                /* 896: the sim's: mining, placing and what is in the hand */
    /* Main to sim. None of these carries a pointer or vouches for anything written before it, so all are relaxed. */
    _Atomic uint64_t intent_word;   /* 2048: the newest player_intent_t, packed */
    _Atomic int64_t intent_sent_ns; /* 2056 */
    _Atomic uint64_t look_word;     /* 2064: the newest yaw and pitch, packed */
    _Atomic uint32_t jump_latch;    /* 2072: a jump pressed since the sim last looked */
    uint32_t view_slot;             /* 2076: main's: the slot it reads */
    _Atomic uint32_t hands_word;    /* 2080: PLAYER_HAND_* held now */
    _Atomic uint32_t hands_latch;   /* 2084: PLAYER_HAND_* pressed since the sim last looked */
    /* Sim to main: the newest player_vitals_t, relaxed, as only what is on the screen depends on it. */
    _Atomic uint64_t vitals_word; /* 2088 */
    uint8_t hands_held_off;       /* 2096: the sim's: a screen has the buttons, so the hands do nothing */
    uint8_t _pad0[7];             /* 2097 */
    player_combat_t combat;       /* 2104: the sim's: what the hand remembers of swinging */
    /* A line of its own: main writes the line above all the time. */
    player_survival_t survival; /* 2112: the sim's */
} player_t;
static_assert(sizeof(player_t) == 2304);
static_assert(alignof(player_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(player_t, views) == 128);
static_assert(offsetof(player_t, target) == 800);
static_assert(offsetof(player_t, interaction) == 896);
static_assert(offsetof(player_t, intent_word) == 2048);
static_assert(offsetof(player_t, survival) == 2112);

/* Remembers what it moves with, and takes nothing. Physics must outlive it. */
void player_init(player_t *player, physics_t *physics);

/* Safe on a zeroed player_t. */
void player_deinit(player_t *player);

/* A body at rest, its feet at this place, with nothing wanted yet. Before the threads start. */
void player_spawn(player_t *player, float64_t x, float64_t y, float64_t z);

/* What the player wants now. Main, as often as it likes, with now_ns from the same clock as the ticks'. A jump pressed between
 * two ticks still jumps. Never waits. */
void player_send(player_t *player, const player_intent_t *intent, int64_t now_ns);

/* Whether the keys main sent last say to sneak. The sim. */
bool player_sneak_sent(const player_t *player);

/* Where the player looks now, for what it aims at. Main, as often as it likes. A yaw and pitch stay a pair. Never waits. */
void player_look_send(player_t *player, float32_t yaw, float32_t pitch);

/* What the two buttons are doing now. Main, as often as it likes, right after player_send, whose time also says how fresh this is.
 * A click between two ticks still counts for one tick. Never waits. */
void player_hands_send(player_t *player, bool attack, bool place);

/* The hands' tick, on what player_tick last aimed at, so on the block the player sees outlined: the sim, or whichever one thread holds
 * the map, right before player_tick, so that the body and the next aim both meet the world as this leaves it. Never waits and never
 * touches the heap. What it broke waits in the interaction's ring, for whoever puts items in the world. */
void player_interact_tick(player_t *player, world_edit_t *edit, int64_t tick_ns);

/* The same, with somewhere for a log an axe cuts to set the rest of its tree going: player_interact_tick is this with nullptr, and an axe cuts one log.
 * The caller then runs player_felling_tick. */
void player_interact_fell_tick(player_t *player, world_edit_t *edit, player_felling_t *felling, int64_t tick_ns);

/* The hand's tick, right before player_interact_tick on the same thread: a press of the left button on a body in reach returns true and
 * says what it does, for the caller to apply. While the button is on a body the hands that follow do not mine. A run that does not call this
 * has hands that only mine. Never waits and never touches the heap. */
bool player_attack_tick(player_t *player, const entity_store_t *store, const world_map_t *map,
                        const player_combat_weapon_t *weapon, int64_t tick_ns, player_combat_swing_t *out);

/* That swing has landed: it tires the body, and a sprint's push stops the sprint. */
void player_attack_landed(player_t *player, const player_combat_swing_t *swing);

/* What an item's box has to overlap to be picked up, with the body where it now is. The thread its ticks are on. */
void player_pickup_reach(const player_t *player, collision_box_t *out);

/* One tick. The sim, or whichever one thread holds the map, with tick_ns the time this tick was due. Never waits and never
 * touches the heap. */
void player_tick(player_t *player, const world_map_t *map, int64_t tick_ns);

/* One tick of what keeps the body going, right after player_tick on the same thread, so on what the body has just done and now touches.
 * Tests of how a body moves can leave it out, and then nothing ever hurts it. Never waits and never touches the heap. */
void player_survive_tick(player_t *player, const world_map_t *map);

/* The body's health as of the last tick. Main, as often as it likes. Never waits. */
player_vitals_t player_vitals_read(const player_t *player);

/* Where the eye is to be drawn at now_ns: from a tick's start to its end, over the tick after it was due, so that a picture
 * moves smoothly at any rate and is never more than a tick behind. Main. Never waits. */
void player_eye_at(player_t *player, int64_t now_ns, float64_t out[3]);

/* What the tick player_eye_at last read aims at. Main. Stays whole until player_eye_at is next called. */
const player_target_t *player_target_view(const player_t *player);

#endif
