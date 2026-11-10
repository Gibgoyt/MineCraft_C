/* See run/src/mob/passive/README.md. */
#ifndef MC_RUN_MOB_PASSIVE_INDEX_H
#define MC_RUN_MOB_PASSIVE_INDEX_H

#include "arena/index.h"
#include "entity/item_drop/index.h"
#include "entity/store/index.h"
#include "mob/path/index.h"
#include "mob/zombie/index.h"
#include "tables/mobs/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* In ticks at 20 Hz and blocks. README.md has them in a table. */
enum : uint32_t {
    MOB_PASSIVE_FLEE_TICKS = 100u,    /* how long a hit sends it running: the published panic of 5 seconds */
    MOB_PASSIVE_STROLL_ONE_IN = 120u, /* its chance per tick, at rest, of setting off somewhere, as a zombie's */
    MOB_PASSIVE_STROLL_ACROSS = 10u,  /* how far off that is at most, along x and along y */
    MOB_PASSIVE_STROLL_UP = 3u,       /* and along z */
    MOB_PASSIVE_STROLL_TICKS = 200u,  /* the longest it keeps at it */
    MOB_PASSIVE_STROLL_NODES = 256u,  /* a search's budget */
    MOB_PASSIVE_HERD_RANGE = 16u,     /* it looks for others of its kind within this, along x and along y */
    MOB_PASSIVE_HERD_FAR = 8u,        /* further than this from where they stand, its stroll goes back towards them */
    MOB_PASSIVE_HERD_SPREAD = 3u,     /* and ends this far about that middle, at most, along x and along y */
};
static_assert(MOB_YIELDS_MAX == 4u); /* mob_passive_body_t's yield_wait */

/* How much faster than its speed it runs from a blow. Not published as such: the published panic is a goal's speed of 2 for a cow
 * whose walk is 1, and 1.25 for a chicken. */
#define MOB_PASSIVE_FLEE_SPEED 1.5f64

/* What one animal is up to. Indexed by the slot of its handle, and good only while owner is its handle: a new body in the slot starts
 * at zero, with every yield ready. Saved with the world, so a field added here is saved too. */
typedef struct mob_passive_body_t {
    float64_t from[2]; /* 0: x and y of where the last blow came from: it runs away from there */
    uint32_t
        yield_wait[MOB_YIELDS_MAX]; /* 16: ticks until each of its sex's yields, in order, can be had again; 0 now */
    float32_t last_health;          /* 32: what it had a tick ago: less now is a hit */
    entity_handle_t owner;          /* 36 */
    uint16_t flee;                  /* 40: ticks left of running away, and 0 if it is not */
    uint16_t stroll;                /* 42: ticks left of the stroll it is on, and 0 if it is on none */
    uint8_t _pad0[4];               /* 44 */
} mob_passive_body_t;
static_assert(sizeof(mob_passive_body_t) == 48);
static_assert(alignof(mob_passive_body_t) == 8);
static_assert(offsetof(mob_passive_body_t, from) == 0);
static_assert(offsetof(mob_passive_body_t, yield_wait) == 16);
static_assert(offsetof(mob_passive_body_t, last_health) == 32);
static_assert(offsetof(mob_passive_body_t, owner) == 36);
static_assert(offsetof(mob_passive_body_t, flee) == 40);
static_assert(offsetof(mob_passive_body_t, stroll) == 42);
static_assert(offsetof(mob_passive_body_t, _pad0) == 44);

/* What the animals' mind goes on with besides its bodies. In the arena, so that mob_t, and app_t on main's stack, do not grow. */
typedef struct mob_passive_tally_t {
    terrain_rng_t random;   /* 0 */
    uint64_t fled_count;    /* 8: hurts that sent one running */
    uint64_t died_count;    /* 16: bodies that died and left what they carried */
    uint64_t dropped_count; /* 24: items they left */
} mob_passive_tally_t;
static_assert(sizeof(mob_passive_tally_t) == 32);

/* Every animal's mind. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_passive_t {
    mob_passive_body_t *borrowed_bodies; /* 0: ENTITY_STORE_CAPACITY, by a handle's slot */
    mob_passive_tally_t *borrowed_tally; /* 8 */
} mob_passive_t;
static_assert(sizeof(mob_passive_t) == 16);

/* What a start takes from its arena: the bodies and the tally, with an alignment's slack for each. */
enum : uint64_t {
    MOB_PASSIVE_BYTES =
        (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_passive_body_t) + sizeof(mob_passive_tally_t) + 128ull,
};

/* No animal is up to anything yet, and what comes next depends only on this seed. From the arena, which lasts as long as the world. */
void mob_passive_start(mob_passive_t *passive, arena_t *arena, uint64_t seed);

/* What the body at this index is up to, starting it over if its slot was another's. Only for a body whose kind is a passive mob. For
 * stage 3's shearing, milking and laying, which read and set yield_wait. */
mob_passive_body_t *mob_passive_body_of(mob_passive_t *passive, const entity_store_t *store, uint32_t index);

/* A male's or a female's row of this mob in mob_sexes, or nullptr for ENTITY_STORE_SEX_NONE or a mob without them. */
const mob_sex_t *mob_passive_sex_of(uint16_t kind, uint8_t sex);

/* One tick of every passive mob, after mob_path_tick and before the bodies move. Only a body whose kind's mind is MOB_MIND_PASSIVE is
 * touched. One whose health has reached 0 leaves its kind's drops and its sex's in drops, and goes. One waiting for its column does
 * nothing, and no time passes for it. One whose health went down since its last tick runs from where view stands for
 * MOB_PASSIVE_FLEE_TICKS, whatever its sex. The rest stand, and now and then stroll, back towards others of their kind if they have
 * strayed. None ever strikes anything. Every yield's wait runs down. Never waits and never touches the heap. */
void mob_passive_tick(mob_passive_t *passive, mob_path_t *path, entity_store_t *store, item_drop_store_t *drops,
                      const mob_zombie_view_t *view);

#endif
