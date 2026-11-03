/* See run/src/mob/README.md. */
#ifndef MC_RUN_MOB_INDEX_H
#define MC_RUN_MOB_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "mob/path/index.h"
#include "world/index.h"

#include <stddef.h>
#include <stdint.h>

/* The minds of what walks a world. There is one, under app_t. Zero bytes mean not running. */
typedef struct mob_t {
    arena_t arena_world;        /* 0: over owned_region; World: reset at each mob_open and mob_close */
    heap_region_t owned_region; /* 64 */
    job_system_t *borrowed_job; /* 80: what its searches run on */
    uint8_t _pad0[40];          /* 88 */
    mob_path_t path;            /* 128 */
} mob_t;
static_assert(sizeof(mob_t) == 448);
static_assert(alignof(mob_t) == 64);

/* Reserves the region, and remembers what it runs its searches on, which must outlive it. nullptr runs them on the sim. On failure logs
 * why, leaves nothing behind and returns false. */
[[nodiscard]] bool mob_init(mob_t *mob, job_system_t *job);

/* Gives it back. Safe on a zeroed mob_t. */
void mob_deinit(mob_t *mob);

/* Nothing asked and nothing known. Before the threads start, with the world open, which must stay open until mob_close. */
void mob_open(mob_t *mob, const world_t *world);

/* What was known about the world is gone. The threads must have stopped, and this waits for any search job still out. Not on a worker.
 * Before the world closes. Safe if none is open. */
void mob_close(mob_t *mob);

/* One tick, on the thread holding the world's map. Never touches the heap. */
void mob_tick(mob_t *mob);

#endif
