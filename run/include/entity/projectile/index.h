/* See run/src/entity/projectile/README.md. */
#ifndef MC_RUN_ENTITY_PROJECTILE_INDEX_H
#define MC_RUN_ENTITY_PROJECTILE_INDEX_H

#include "arena/index.h"
#include "entity/grid/index.h"
#include "entity/store/index.h"
#include "physics/collision/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Arrows in the world at once. A full store loses one to make room. */
    PROJECTILE_CAPACITY = 512u,
    /* How long one stays in a block, as published, and how long one may fly with nothing to hit, which is our own bound. */
    PROJECTILE_STUCK_TICKS = 1200u,
    PROJECTILE_FLIGHT_TICKS = 1200u,
    /* The most bodies one arrow looks at in a tick. Far more than fit along one tick of its flight. */
    PROJECTILE_BODIES_MAX = 64u,
};

/* The published numbers, in blocks and ticks. */
#define PROJECTILE_GRAVITY 0.05f64
#define PROJECTILE_DRAG 0.99f64
#define PROJECTILE_WATER_DRAG 0.6f64
/* A hit is the speed times this, rounded up. */
#define PROJECTILE_DAMAGE 2.0f64
/* What an inaccuracy of 1 turns a shot by, at most, along each axis, for a direction of length 1. */
#define PROJECTILE_SPREAD 0.0172275f64
/* How much wider than a body's box an arrow counts as meeting it. */
#define PROJECTILE_BODY_MARGIN 0.3f64
#define PROJECTILE_KNOCKBACK 0.4f64
/* How far short of the face it hit one that sticks stops. Our own: it only has to be drawn outside the block. */
#define PROJECTILE_BACK_OFF 0.05f64

/* One arrow. It has no box of its own against blocks: its position is a point, and that point's path is what hits. */
typedef struct projectile_t {
    float64_t position[COLLISION_AXES]; /* 0 */
    float64_t velocity[COLLISION_AXES]; /* 24: blocks per tick; zero while stuck */
    entity_handle_t shooter;            /* 48: it never hits this body; ENTITY_HANDLE_NONE for no body */
    uint32_t age;                       /* 52: ticks in a loaded column, from when it was fired or last stuck */
    int32_t stuck_cell[COLLISION_AXES]; /* 56: with stuck */
    uint8_t stuck;                      /* 68: it stays until that cell has nothing to stay in */
    uint8_t _pad0[3];                   /* 69 */
} projectile_t;
static_assert(sizeof(projectile_t) == 72);

/* Every arrow in a world. Plain numbers, so that the same world and shots give the same bytes. The sim's. */
typedef struct projectile_store_t {
    projectile_t
        *borrowed_items;  /* 0: PROJECTILE_CAPACITY; the first count are in use, in no order that means anything */
    terrain_rng_t random; /* 8: how each shot goes astray */
    /* These add up: fired is all the others, plus count. */
    uint64_t fired_count;   /* 16 */
    uint64_t body_hits;     /* 24 */
    uint64_t target_hits;   /* 32: on what projectile_tick was given to aim at */
    uint64_t expired_count; /* 40: lay or flew too long */
    uint64_t burned_count;  /* 48: lava */
    uint64_t lost_count;    /* 56: pushed out of a full store */
    uint64_t stuck_count;   /* 64: not one of those: how many times one stuck in a block */
    uint32_t count;         /* 72 */
    uint8_t _pad0[4];       /* 76 */
} projectile_store_t;
static_assert(sizeof(projectile_store_t) == 80);

/* What a store takes from its arena: its array, with an alignment's slack. */
enum : uint64_t {
    PROJECTILE_STORE_BYTES = (uint64_t)PROJECTILE_CAPACITY * sizeof(projectile_t) + alignof(projectile_t),
};

/* How much a hit on the target came to, and which way the arrow was going, along x and y with length 1, or zero if straight down. The
 * listener chooses what follows. On the sim thread, during projectile_tick. */
typedef void projectile_hurt_t(void *borrowed_context, float32_t amount, const float64_t direction[COLLISION_AXES]);

/* An empty store, from the arena, with shots that go astray from this seed. */
void projectile_store_start(projectile_store_t *store, arena_t *arena, uint64_t seed);

/* An arrow leaves origin along direction, which need not have length 1 but must not be zero, at this speed, turned at random by up to
 * inaccuracy times PROJECTILE_SPREAD along each axis. A full store loses what has lain longest, or else what has flown longest. */
void projectile_fire(projectile_store_t *store, entity_handle_t shooter, const float64_t origin[COLLISION_AXES],
                     const float64_t direction[COLLISION_AXES], float64_t speed, float64_t inaccuracy);

/* One tick of every arrow: what it meets on the way, whichever is first, then drag and gravity. A body is hurt and knocked back here.
 * The target, which may be nullptr, is the box of what is not among the bodies, and hurt is told if it is hit. An arrow in a column
 * not loaded, or about to enter one, waits. After entity_tick, so that the grid is of where the bodies are. Never waits and never
 * touches the heap. */
void projectile_tick(projectile_store_t *store, entity_store_t *bodies, entity_grid_t *grid, const world_map_t *map,
                     const collision_box_t *target, projectile_hurt_t *hurt, void *borrowed_context);

/* What a blow sends something with this velocity off with, along direction as projectile_hurt_t gives it: the published sum. */
void projectile_knockback(float64_t velocity[COLLISION_AXES], const float64_t direction[COLLISION_AXES],
                          bool on_ground);

/* Whether nothing solid is on the line between two places, and every column it crosses is loaded. What has no collision box, such as a
 * plant or water, is no matter. */
bool projectile_line_is_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                              const float64_t to[COLLISION_AXES]);

#endif
