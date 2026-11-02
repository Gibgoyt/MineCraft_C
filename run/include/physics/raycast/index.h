/* See run/src/physics/raycast/README.md. */
#ifndef MC_RUN_PHYSICS_RAYCAST_INDEX_H
#define MC_RUN_PHYSICS_RAYCAST_INDEX_H

#include "physics/collision/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Stairs, the most boxes anything can be aimed at by. */
    RAYCAST_BOXES_PER_BLOCK = COLLISION_BOXES_PER_BLOCK,
};

/* The longest ray asked for. Far more than anything reaches, and it bounds the cells one cast can read. */
#define RAYCAST_DISTANCE_MAX 512.0f64

/* How a cast ended. */
typedef enum raycast_result_t : uint8_t {
    RAYCAST_RESULT_MISS = 0,       /* nothing to aim at along all of it */
    RAYCAST_RESULT_HIT = 1,        /* a block */
    RAYCAST_RESULT_NOT_LOADED = 2, /* it reached a column that is not loaded, and cannot say what is there */
} raycast_result_t;

/* Says the ray started inside the box, so it came in through no face. */
enum : uint8_t { RAYCAST_AXIS_NONE = 3u };

/* What a ray found. Only result means anything unless result is RAYCAST_RESULT_HIT, except that cell and distance then say
 * where a ray stopped at a column that is not loaded. */
typedef struct raycast_hit_t {
    collision_box_t bounds; /* 0: around all of the block's boxes */
    float64_t distance;     /* 48: blocks from the origin to where it came in, 0 if it started inside */
    int32_t cell[3];        /* 56 */
    uint16_t block;         /* 68 */
    uint8_t result;         /* 70: raycast_result_t */
    uint8_t axis;           /* 71: of the face it came in through: COLLISION_AXIS_*, or RAYCAST_AXIS_NONE */
    int8_t normal_sign;     /* 72: which way that face looks, -1 or 1, and 0 with RAYCAST_AXIS_NONE */
    uint8_t _pad0[7];       /* 73 */
} raycast_hit_t;
static_assert(sizeof(raycast_hit_t) == 80);

/* What a block at this cell can be aimed at by: what it collides with if it is solid, and if not a box around what is drawn.
 * Nothing for air and fluids, which a ray goes on through. Out has room for RAYCAST_BOXES_PER_BLOCK. Returns how many. */
uint32_t raycast_block_boxes(uint16_t block, int32_t x, int32_t y, int32_t z, collision_box_t *out);

/* Walks the cells from origin along direction, which need not be unit length, in order, for up to max_distance blocks, and
 * stops at the first box of any block. A face exactly at max_distance counts. Touching a box without going into it does not,
 * and neither does a box the origin is on the far side of. Outside gameplay's heights there is nothing to hit, and a ray goes
 * on. A zero or broken direction, a broken origin, or a max_distance not above 0 or above RAYCAST_DISTANCE_MAX is a miss.
 * Whichever thread holds the map. Never allocates. */
raycast_result_t raycast_cast(const world_map_t *map, const float64_t origin[COLLISION_AXES],
                              const float64_t direction[COLLISION_AXES], float64_t max_distance, raycast_hit_t *out);

/* The cell in front of the face a hit came in through: where a block placed against it goes. The cell itself if the ray came
 * in through no face. */
void raycast_hit_in_front(const raycast_hit_t *hit, int32_t out[COLLISION_AXES]);

#endif
