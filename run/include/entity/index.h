/* See run/src/entity/README.md. */
#ifndef MC_RUN_ENTITY_INDEX_H
#define MC_RUN_ENTITY_INDEX_H

#include "arena/index.h"
#include "entity/animation/index.h"
#include "entity/explosion/index.h"
#include "entity/grid/index.h"
#include "entity/item_drop/index.h"
#include "entity/projectile/index.h"
#include "entity/store/index.h"
#include "heap/index.h"
#include "physics/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* Everything in a world that moves and is not a block or the player. There is one, under app_t. Zero bytes mean not running. */
typedef struct entity_t {
    arena_t arena_world;            /* 0: over owned_region; World: reset at each entity_open and entity_close */
    heap_region_t owned_region;     /* 64 */
    physics_t *borrowed_physics;    /* 80 */
    item_drop_store_t item_drops;   /* 88 */
    entity_store_t store;           /* 184: every body with a mind of its own; run/src/mob/ gives them one */
    entity_grid_t grid;             /* 472: where they were at the end of the last tick */
    explosion_t explosion;          /* 544: what blasts work in */
    projectile_store_t projectiles; /* 584: arrows; run/src/game/ ticks them, as only it knows the player's box */
    uint8_t _pad0[40];              /* 664 */
} entity_t;
static_assert(sizeof(entity_t) == 704);
static_assert(alignof(entity_t) == 64);

/* Reserves the region, and remembers what it moves things with, which must outlive it and be the thread's whose ticks these are. On
 * failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool entity_init(entity_t *entity, physics_t *physics);

/* Gives it back. Safe on a zeroed entity_t. */
void entity_deinit(entity_t *entity);

/* Nothing in the world yet, and what comes next depends only on this seed. Before the threads start. */
void entity_open(entity_t *entity, uint64_t seed);

/* What was in the world is gone: nothing is saved until Stage 16.1. The threads must have stopped. Safe if none is open. */
void entity_close(entity_t *entity);

/* One tick of everything in the world. The sim, or whichever one thread holds the map. Never waits and never touches the heap. */
void entity_tick(entity_t *entity, const world_map_t *map);

#endif
