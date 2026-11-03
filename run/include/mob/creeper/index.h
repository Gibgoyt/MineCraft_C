/* See run/src/mob/creeper/README.md. */
#ifndef MC_RUN_MOB_CREEPER_INDEX_H
#define MC_RUN_MOB_CREEPER_INDEX_H

#include "arena/index.h"
#include "entity/index.h"
#include "mob/path/index.h"
#include "player/movement/index.h"
#include "player/survival/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/edit/index.h"

#include <stddef.h>
#include <stdint.h>

/* The published numbers, in ticks at 20 Hz and blocks. README.md has them in a table. */
enum : uint32_t {
    MOB_CREEPER_FUSE_TICKS = 30u,
    MOB_CREEPER_UNSEEN_TICKS = 60u,    /* out of sight this long, and it gives up */
    MOB_CREEPER_NOTICE_ONE_IN = 10u,   /* with the player in sight, its chance each tick of noticing */
    MOB_CREEPER_STROLL_ONE_IN = 120u,  /* with nobody to chase, its chance each tick of setting off */
    MOB_CREEPER_STROLL_XY = 10u,       /* how far a stroll goes along x or y */
    MOB_CREEPER_STROLL_Z = 7u,         /* and along z */
    MOB_CREEPER_ASK_MIN_TICKS = 4u,    /* between asking the way, at least */
    MOB_CREEPER_ASK_SPREAD_TICKS = 8u, /* and up to this many more, so that a pack does not ask all at once */
    /* How long a stroll waits for its answer before it thinks better of it. */
    MOB_CREEPER_STROLL_PATIENCE_TICKS = 200u,
    /* The most that burst in one tick; the rest wait for the next, with their fuses run. A burst is at most 11 cubed cells at this power,
     * so that this many cannot lap the edit log's 65536 records, which its readers read once a tick. */
    MOB_CREEPER_BURSTS_PER_TICK = 16u,
};
#define MOB_CREEPER_POWER 3.0f64
#define MOB_CREEPER_NOTICE_REACH 16.0f64 /* it notices and chases this far off */
#define MOB_CREEPER_LIGHT_REACH 3.0f64   /* nearer than this, the fuse starts */
#define MOB_CREEPER_ABORT_REACH 7.0f64   /* further than this, the fuse runs back */
#define MOB_CREEPER_STROLL_PACE 0.8f64   /* of its speed */
#define MOB_CREEPER_HEAD_TURN_MAX 1.3f32 /* how far its head turns from its body */

/* What one is doing, when no fuse is burning. */
typedef enum mob_creeper_state_t : uint8_t {
    MOB_CREEPER_STATE_IDLE = 0,
    MOB_CREEPER_STATE_STROLLING = 1,
    MOB_CREEPER_STATE_CHASING = 2,
} mob_creeper_state_t;

/* One body's mind. Indexed by the slot of its handle. */
typedef struct mob_creeper_body_t {
    entity_handle_t owner; /* 0: whose this is; a new one starts over */
    int32_t goal[3];       /* 4: the cell it last asked the way to */
    uint32_t asked_serial; /* 16: the answer it had when it set off on a stroll, so as to know the new one */
    uint16_t ask_wait;     /* 20: ticks until it may ask again, or a stroll stops waiting */
    uint16_t unseen;       /* 22: ticks since it last saw who it chases */
    uint8_t fuse;          /* 24: 0 to MOB_CREEPER_FUSE_TICKS */
    uint8_t state;         /* 25: a mob_creeper_state_t */
    uint8_t lit;           /* 26: it stands still, and the fuse runs forward */
    uint8_t asked;         /* 27: goal means something */
    uint8_t _pad0[4];      /* 28 */
} mob_creeper_body_t;
static_assert(sizeof(mob_creeper_body_t) == 32);

/* Every creeper's mind in one world. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_creeper_t {
    mob_creeper_body_t *borrowed_bodies; /* 0: ENTITY_STORE_CAPACITY */
    terrain_rng_t random;                /* 8 */
    uint64_t burst_count;                /* 16 */
    uint64_t died_count;                 /* 24: went some other way, leaving what it drops */
    uint64_t dropped_count;              /* 32: items they left */
} mob_creeper_t;
static_assert(sizeof(mob_creeper_t) == 40);

/* What a start takes from its arena. */
enum : uint64_t {
    MOB_CREEPER_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t) + 64ull,
};

/* What a tick works on, all of it the sim's. */
typedef struct mob_creeper_scene_t {
    entity_t *borrowed_entity;            /* 0 */
    mob_path_t *borrowed_path;            /* 8 */
    world_edit_t *borrowed_edit;          /* 16 */
    player_movement_t *borrowed_movement; /* 24: who it chases; nullptr if nobody */
    player_survival_t *borrowed_survival; /* 32: with borrowed_movement */
} mob_creeper_scene_t;
static_assert(sizeof(mob_creeper_scene_t) == 40);

/* Nothing yet, from the arena, which lasts as long as the world. Its numbers come from this seed until told otherwise. */
void mob_creeper_start(mob_creeper_t *creeper, arena_t *arena, uint64_t seed);

/* Where its random numbers come from from here on. Before the first tick. */
void mob_creeper_seed_set(mob_creeper_t *creeper, uint64_t seed);

/* One tick of every creeper in the store, on the thread holding the map: it takes away any with no health left and leaves what it drops,
 * and for the rest sets wish, jump and facing, runs the fuse, and sets off those whose fuse has run out. After mob_path_tick, and before
 * entity_tick moves them. Other kinds are not touched, but for what a blast does to them. Never waits and never touches the heap. */
void mob_creeper_tick(mob_creeper_t *creeper, const mob_creeper_scene_t *scene);

#endif
