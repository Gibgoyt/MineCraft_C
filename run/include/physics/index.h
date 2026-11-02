/* See run/src/physics/README.md. */
#ifndef MC_RUN_PHYSICS_INDEX_H
#define MC_RUN_PHYSICS_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "physics/collision/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* What physics_move splits a longer move into, and the most a box may measure on any side. Together they bound the boxes one
 * sweep has to look at, which is what sizes the region. */
#define PHYSICS_STEP_BLOCKS 8.0f64
#define PHYSICS_BOX_SIZE_MAX 8.0f64
/* Further than this in one call is a broken velocity. */
#define PHYSICS_MOVE_BLOCKS_MAX 4096.0f64

/* One per thread that moves things, and one thread at a time: the arena has no locks. Zero bytes mean not running. */
typedef struct physics_t {
    arena_t arena_boxes;        /* 0: over owned_region; reset by every move */
    heap_region_t owned_region; /* 64 */
    uint8_t _pad0[48];          /* 80 */
} physics_t;
static_assert(sizeof(physics_t) == 128);
static_assert(alignof(physics_t) == 64);

/* Reserves the region. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool physics_init(physics_t *physics);

/* Gives it back. Safe on a zeroed physics_t. */
void physics_deinit(physics_t *physics);

/* Moves box by delta through the world around it, and says how it went. See collision_sweep. Never touches the heap. */
void physics_move(physics_t *physics, const world_map_t *map, const collision_box_t *box,
                  const float64_t delta[COLLISION_AXES], collision_move_t *out);

/* Whether box is free of every solid thing, and of every column that is not loaded. Never touches the heap. */
bool physics_box_is_clear(physics_t *physics, const world_map_t *map, const collision_box_t *box);

#endif
