/* See run/src/mob/README.md. */
#ifndef MC_RUN_MOB_INDEX_H
#define MC_RUN_MOB_INDEX_H

#include "arena/index.h"
#include "entity/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "mob/combat/index.h"
#include "mob/creeper/index.h"
#include "mob/passive/index.h"
#include "mob/path/index.h"
#include "mob/skeleton/index.h"
#include "mob/zombie/index.h"
#include "world/index.h"

#include <stddef.h>
#include <stdint.h>

/* The minds of what walks a world. There is one, under app_t. Zero bytes mean not running. */
typedef struct mob_t {
    arena_t arena_world;        /* 0: over owned_region; World: reset at each mob_open and mob_close */
    heap_region_t owned_region; /* 64 */
    job_system_t *borrowed_job; /* 80: what its searches run on */
    mob_zombie_t zombie;        /* 88 */
    mob_skeleton_t skeleton;    /* 128: run/src/game/ ticks it, between this tick and the bodies' */
    mob_creeper_t creeper;      /* 168 */
    mob_combat_t combat;        /* 208: run/src/game/ gives it the player's blows, and ticks it after the bodies move */
    mob_passive_t passive;      /* 240: every animal, ticked with the zombies by mob_minds_tick */
    mob_path_t path;            /* 256 */
} mob_t;
static_assert(sizeof(mob_t) == 576);
static_assert(alignof(mob_t) == 64);

/* Reserves the region, and remembers what it runs its searches on, which must outlive it. nullptr runs them on the sim. On failure logs
 * why, leaves nothing behind and returns false. */
[[nodiscard]] bool mob_init(mob_t *mob, job_system_t *job);

/* Gives it back. Safe on a zeroed mob_t. */
void mob_deinit(mob_t *mob);

/* Nothing asked and nothing known, and what its parts choose depends only on this seed. Before the threads start, with the world open,
 * which must stay open until mob_close. */
void mob_open(mob_t *mob, const world_t *world, uint64_t seed);

/* What was known about the world is gone. The threads must have stopped, and this waits for any search job still out. Not on a worker.
 * Before the world closes. Safe if none is open. */
void mob_close(mob_t *mob);

/* One tick of what answers them, on the thread holding the world's map. Never touches the heap. The minds themselves, which need the
 * player, are ticked by run/src/game/ after this, before the bodies move. */
void mob_tick(mob_t *mob);

/* One tick of what every mob does, after mob_tick and before the bodies move, in the entity the bodies are in. Returns the blow view takes,
 * and 0 if none. Never touches the heap. */
float32_t mob_minds_tick(mob_t *mob, entity_t *entity, const mob_zombie_view_t *view);

#endif
