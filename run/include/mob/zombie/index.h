/* See run/src/mob/zombie/README.md. */
#ifndef MC_RUN_MOB_ZOMBIE_INDEX_H
#define MC_RUN_MOB_ZOMBIE_INDEX_H

#include "arena/index.h"
#include "entity/item_drop/index.h"
#include "entity/store/index.h"
#include "mob/path/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* Every number here is a published one, in ticks at 20 Hz. README.md has them in a table. */
enum : uint32_t {
    MOB_ZOMBIE_ATTACK_TICKS = 20u,   /* between one blow and the next */
    MOB_ZOMBIE_NOTICE_ONE_IN = 10u,  /* its chance per tick of looking round for something to chase */
    MOB_ZOMBIE_UNSEEN_TICKS = 60u,   /* unseen for longer than this, it gives up */
    MOB_ZOMBIE_SIGHT_EVERY = 5u,     /* ticks between looks while it chases; not published, and only saves rays */
    MOB_ZOMBIE_REPATH_MIN = 4u,      /* the least between two asks for a path */
    MOB_ZOMBIE_REPATH_SPREAD = 7u,   /* and up to this less one more */
    MOB_ZOMBIE_REPATH_FAR = 5u,      /* and this more past 16 blocks */
    MOB_ZOMBIE_REPATH_FARTHER = 10u, /* or this more past 32 */
    MOB_ZOMBIE_REPATH_FAILED = 10u,  /* and this more after an answer that does not arrive */
    MOB_ZOMBIE_STROLL_ONE_IN = 120u, /* its chance per tick, with nothing to chase, of setting off somewhere */
    MOB_ZOMBIE_STROLL_ACROSS = 10u,  /* how far off that is at most, along x and along y */
    MOB_ZOMBIE_STROLL_UP = 7u,       /* and along z */
    MOB_ZOMBIE_STROLL_TICKS =
        200u, /* the longest it keeps at it; not published, and only ends a stroll that goes nowhere */
    MOB_ZOMBIE_STROLL_NODES = 256u, /* a search's budget: not published, and more than a walk this short needs */
};

#define MOB_ZOMBIE_DAMAGE 3.0f32
#define MOB_ZOMBIE_FOLLOW_RANGE 35.0f64
/* It only notices what is within this of its own height, though it keeps chasing what then climbs. */
#define MOB_ZOMBIE_NOTICE_UP 4.0f64
/* How far past its box its arms reach, sideways. Not up or down: the boxes must meet there. */
#define MOB_ZOMBIE_ATTACK_REACH 0.828f64
#define MOB_ZOMBIE_EYE 1.74f64
/* How far its head turns from its body, and looks up or down. Not published: only how it looks. */
#define MOB_ZOMBIE_HEAD_YAW_MAX 1.3f64
#define MOB_ZOMBIE_HEAD_PITCH_MAX 0.7f64

/* What there is to chase, this tick. Plain numbers: a mob does not know what a player is. */
typedef struct mob_zombie_view_t {
    float64_t position[COLLISION_AXES]; /* 0: the middle of the soles of its feet */
    float64_t eye;                      /* 24: above position */
    float64_t half_width;               /* 32 */
    float64_t height;                   /* 40 */
    uint8_t alive;                      /* 48: 0: nothing is there to chase, and whatever chased it stops */
    uint8_t _pad0[7];                   /* 49 */
} mob_zombie_view_t;
static_assert(sizeof(mob_zombie_view_t) == 56);

/* What one zombie is up to. Indexed by the slot of its handle, and good only while owner is its handle. */
typedef struct mob_zombie_body_t {
    float64_t asked[COLLISION_AXES]; /* 0: where its target was when it last asked for a path */
    entity_handle_t owner;           /* 24 */
    uint16_t unseen;                 /* 28: ticks since it last saw its target */
    uint16_t repath;                 /* 30: ticks until it may ask again */
    uint8_t cooldown;                /* 32: ticks until it may strike again */
    uint8_t chasing;                 /* 33 */
    uint16_t stroll;                 /* 34: ticks left of the stroll it is on, and 0 if it is on none */
    uint8_t _pad0[4];                /* 36 */
} mob_zombie_body_t;
static_assert(sizeof(mob_zombie_body_t) == 40);

/* Every zombie's mind. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_zombie_t {
    mob_zombie_body_t *borrowed_bodies; /* 0: ENTITY_STORE_CAPACITY, by a handle's slot */
    terrain_rng_t random;               /* 8 */
    uint64_t struck_count;              /* 16: blows it landed */
    uint64_t died_count;                /* 24: bodies that died and left what they carried */
    uint64_t dropped_count;             /* 32: items they left */
} mob_zombie_t;
static_assert(sizeof(mob_zombie_t) == 40);

/* What a start takes from its arena: the bodies, with an alignment's slack. */
enum : uint64_t {
    MOB_ZOMBIE_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t) + 64ull,
};

/* No zombie is up to anything yet, and what comes next depends only on this seed. From the arena, which lasts as long as the world. */
void mob_zombie_start(mob_zombie_t *zombie, arena_t *arena, uint64_t seed);

/* Whether a straight line between two places meets nothing solid. Plants and fluids hide nothing. A column not loaded hides everything.
 * Touching a box without going into it does not count. */
bool mob_zombie_line_is_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                              const float64_t to[COLLISION_AXES]);

/* Whether the body at this index can reach what is in view: its box, widened by MOB_ZOMBIE_ATTACK_REACH sideways, overlaps view's. */
bool mob_zombie_in_reach(const entity_store_t *store, uint32_t index, const mob_zombie_view_t *view);

/* One tick of every zombie, after mob_path_tick and before the bodies move. Only a body whose kind is MOB_ID_ZOMBIE is touched. One whose
 * health has reached 0 leaves what it carried in drops, and goes. One waiting for its column does nothing. The rest stroll about until
 * they notice view, then walk after it and strike it. Returns the blow view takes, and 0 if none: whoever keeps its health applies it, as however many struck this
 * tick count as one. Never waits and never touches the heap. */
float32_t mob_zombie_tick(mob_zombie_t *zombie, mob_path_t *path, entity_store_t *store, item_drop_store_t *drops,
                          const world_map_t *map, const mob_zombie_view_t *view);

#endif
