/* See run/src/entity/explosion/README.md. */
#ifndef MC_RUN_ENTITY_EXPLOSION_INDEX_H
#define MC_RUN_ENTITY_EXPLOSION_INDEX_H

#include "arena/index.h"
#include "entity/item_drop/index.h"
#include "entity/store/index.h"
#include "physics/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/edit/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* The strongest blast asked for, which is what bounds the cells one can reach: 1.3 times this, at 0.225 lost per 0.3 walked, comes to
 * 13.87 blocks, and so to less than EXPLOSION_REACH cells whichever cell the middle is in. */
#define EXPLOSION_POWER_MAX 8.0f64

enum : uint32_t {
    EXPLOSION_RAYS_PER_SIDE = 16u,
    /* The cells on the faces of a cube of that side: one ray through each. */
    EXPLOSION_RAYS = 1352u,
    EXPLOSION_REACH = 15u,
    EXPLOSION_SIDE = 2u * EXPLOSION_REACH + 1u,
    EXPLOSION_CELLS = EXPLOSION_SIDE * EXPLOSION_SIDE * EXPLOSION_SIDE,
    EXPLOSION_SEEN_WORDS = EXPLOSION_CELLS / 64u + 1u,
    /* How far up what needs the ground under it goes with a block broken under it. */
    EXPLOSION_STACK_MAX = 16u,
};

/* What it takes to end an item lying in the way. */
#define EXPLOSION_ITEM_HEALTH 5.0f32
/* How high a body's eye is, as a share of its height. */
#define EXPLOSION_EYE_SHARE 0.85f64

/* One block a blast has found it can break. */
typedef struct explosion_cell_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    int32_t z;        /* 8 */
    uint16_t block;   /* 12 */
    uint8_t _pad0[2]; /* 14 */
} explosion_cell_t;
static_assert(sizeof(explosion_cell_t) == 16);

/* What a blast does to one body. */
typedef struct explosion_blow_t {
    float64_t push[COLLISION_AXES]; /* 0: blocks per tick to add to its velocity */
    float64_t impact;               /* 24: 0 to 1: nearness times how much of the body the blast reaches */
    float32_t damage;               /* 32: half hearts */
    uint8_t _pad0[4];               /* 36 */
} explosion_blow_t;
static_assert(sizeof(explosion_blow_t) == 40);

/* What blasts in one world work in. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct explosion_t {
    uint64_t *borrowed_seen;          /* 0: EXPLOSION_SEEN_WORDS, one bit per cell around the middle of this blast */
    explosion_cell_t *borrowed_cells; /* 8: EXPLOSION_CELLS: what this blast will break */
    terrain_rng_t random;             /* 16: how far each ray gets, and what comes out of what breaks */
    uint64_t broken_count;            /* 24: blocks, over all blasts */
    uint32_t count;                   /* 32: of borrowed_cells */
    uint32_t blast_count;             /* 36 */
} explosion_t;
static_assert(sizeof(explosion_t) == 40);

/* What a start takes from its arena: its two arrays, with an alignment's slack for each. */
enum : uint64_t {
    EXPLOSION_BYTES = (uint64_t)EXPLOSION_SEEN_WORDS * sizeof(uint64_t) +
                      (uint64_t)EXPLOSION_CELLS * sizeof(explosion_cell_t) + 2ull * 64ull,
};

/* No blast yet, and what comes next depends only on this seed. From the arena, which lasts as long as the world. */
void explosion_start(explosion_t *explosion, arena_t *arena, uint64_t seed);

/* How much of a blast this block stands against: the published number. */
float64_t explosion_resistance(uint16_t block);

/* Whether a straight line from one place to another meets nothing a body would collide with. A plant or a fluid does not stop it, a
 * column not loaded does, and above or below what gameplay reaches there is nothing to stop it. A line that starts inside a box is
 * stopped, one that only touches a face is not, and one going along a face is stopped by a low face and not by a high one. Looks and
 * changes nothing. */
bool explosion_line_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                          const float64_t to[COLLISION_AXES]);

/* How much of box a blast at center reaches, from 0 to 1: the share of a grid of points on it with a clear line there. */
float64_t explosion_exposure(const world_map_t *map, const float64_t center[COLLISION_AXES],
                             const collision_box_t *box);

/* What a blast of this power at center does to a body with its feet at position, its box and its eye so far above its feet. False if it
 * is out of reach, or has its eye exactly at the middle, so that no way is away. */
[[nodiscard]] bool explosion_blow(const world_map_t *map, const float64_t center[COLLISION_AXES], float64_t power,
                                  const float64_t position[COLLISION_AXES], const collision_box_t *box,
                                  float64_t eye_height, explosion_blow_t *out);

/* The first half of a blast of this power, with the world as it stands: finds what will break, hurts and pushes each body in the store
 * and each item lying near. Whoever has bodies of its own hits them now, with explosion_blow, and then calls explosion_finish. A power
 * above 0, up to EXPLOSION_POWER_MAX. A body waiting for its column is left alone. Health stops at 0: whoever owns a body takes it away.
 * Never waits and never touches the heap. */
void explosion_begin(explosion_t *explosion, const world_map_t *map, entity_store_t *store, item_drop_store_t *drops,
                     const float64_t center[COLLISION_AXES], float64_t power);

/* The second half: breaks what explosion_begin found, and what stood on it, and one block in power of them gives its drops. A block the
 * edit could find no room for stays. Returns how many went. */
uint32_t explosion_finish(explosion_t *explosion, world_edit_t *edit, item_drop_store_t *drops, float64_t power);

#endif
