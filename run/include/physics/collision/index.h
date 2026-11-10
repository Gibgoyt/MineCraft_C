/* See run/src/physics/collision/README.md. */
#ifndef MC_RUN_PHYSICS_COLLISION_INDEX_H
#define MC_RUN_PHYSICS_COLLISION_INDEX_H

#include "arena/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    COLLISION_AXIS_X = 0u,
    COLLISION_AXIS_Y = 1u,
    COLLISION_AXIS_Z = 2u, /* up */
    COLLISION_AXES = 3u,
    COLLISION_BOXES_PER_BLOCK = 2u, /* stairs */
};

/* Two faces closer than this touch, and do not overlap. It covers what float64_t loses adding a box's size to a position, as
 * far out as 2^26 blocks. */
#define COLLISION_EPSILON 1.0e-7f64

/* An axis-aligned box in world coordinates. In float64_t and not math's aabb_t, whose float32_t has lost a contact's
 * exactness by a few hundred thousand blocks out. */
typedef struct collision_box_t {
    float64_t min[COLLISION_AXES]; /* 0 */
    float64_t max[COLLISION_AXES]; /* 24 */
} collision_box_t;
static_assert(sizeof(collision_box_t) == 48);

/* How a sweep ended. */
typedef struct collision_move_t {
    float64_t moved[COLLISION_AXES]; /* 0: how far the box really went */
    uint8_t hit_x;                   /* 24: something stopped it on this axis */
    uint8_t hit_y;                   /* 25 */
    uint8_t hit_z_down;              /* 26: it came down on something */
    uint8_t hit_z_up;                /* 27: it went up into something */
    uint8_t _pad0[4];                /* 28 */
} collision_move_t;
static_assert(sizeof(collision_move_t) == 32);

/* What a cell holds, for anything that moves. Below the world is bedrock, so nothing falls out of it; above it is air. False if
 * its column is not loaded, with *out left alone. collision_cell_read gives the block id, and collision_cell_read_voxel the id
 * with its state, run/include/types/block_state/, which is what collision_block_boxes turns a block's boxes by. */
bool collision_cell_read(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint16_t *out);
bool collision_cell_read_voxel(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint16_t *out);

/* The boxes a block at this cell collides with, from config/blocks/: none unless it is solid, half a cell for a slab, two for
 * stairs, and otherwise the whole cell. voxel is the block with its state, and the boxes turn with its facing as the card's
 * do: an id alone is the block facing north. Out has room for COLLISION_BOXES_PER_BLOCK. Returns how many. */
uint32_t collision_block_boxes(uint16_t voxel, int32_t x, int32_t y, int32_t z, collision_box_t *out);

/* A box inside cell x, y, z, in 32nds of a block as the card's table holds them, turned from north to facing about the middle of
 * the cell: block_facing_box_turn, so that a block collides and is aimed at as it is drawn. */
void collision_part_box(const uint8_t low[COLLISION_AXES], const uint8_t high[COLLISION_AXES], uint32_t facing,
                        int32_t x, int32_t y, int32_t z, collision_box_t *out);

/* Every box any part of around could meet, from the arena. A column that is not loaded is a wall, so that nothing walks or falls
 * into what is not there yet. Takes COLLISION_BOXES_PER_BLOCK boxes per cell around touches, and aborts as for running out if
 * that does not fit. */
collision_box_t *collision_gather(arena_t *arena, const world_map_t *map, const collision_box_t *around,
                                  uint32_t *out_count);

/* Moves box along delta through boxes, stopping at the first face in the way, and goes on along what is left on the other axes.
 * It cannot pass through anything, however far it goes. A box it already overlaps does not stop it, so anything caught inside a
 * block can walk out. Where one box's two faces are met at the same moment, its top or bottom counts first. Costs time in
 * proportion to count. */
void collision_sweep(const collision_box_t *boxes, uint32_t count, const collision_box_t *box,
                     const float64_t delta[COLLISION_AXES], collision_move_t *out);

/* Whether box overlaps any of them. Two that only touch do not. */
bool collision_overlaps_any(const collision_box_t *boxes, uint32_t count, const collision_box_t *box);

#endif
