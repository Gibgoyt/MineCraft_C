/*
 * What a voxel's uint16_t holds: a block id and the state beside it. One definition for the world, the saves, the card and the
 * tools, so it is under types/, compiled by GCC and by nvcc. See run/include/types/block_state/README.md.
 *
 *   bits 0-11   the block id, into the generated block table: 4096 ids, the most compile/src/game_tables/blocks/ allows
 *   bits 12-13  facing: which of the four ways the block was turned to, a block_facing_t
 *   bit  14     half: the upper of a block two tall, such as a door
 *   bit  15     open: a door or a gate that is open
 *
 * A block uses only the bits its file declares, and every other bit is 0, so a block with no state is its id and nothing else:
 * every world and save from before this had only ids, and reads the same. Facing 0 is north, the way every block faced before
 * there was a facing.
 */
#ifndef MC_RUN_TYPES_BLOCK_STATE_INDEX_H
#define MC_RUN_TYPES_BLOCK_STATE_INDEX_H

#include "types/index.h"

#include <stdint.h>

#ifdef __CUDACC__
#define BLOCK_STATE_BOTH_SIDES __host__ __device__
#else
#define BLOCK_STATE_BOTH_SIDES
#endif

enum : uint32_t {
    BLOCK_STATE_ID_BITS = 12u,
    BLOCK_STATE_ID_LIMIT = 1u << BLOCK_STATE_ID_BITS, /* one more than the highest id a voxel can hold */
    BLOCK_STATE_ID_MASK = BLOCK_STATE_ID_LIMIT - 1u,
    BLOCK_STATE_FACING_SHIFT = 12u,
    BLOCK_STATE_FACING_MASK = 3u << BLOCK_STATE_FACING_SHIFT,
    BLOCK_STATE_UPPER = 1u << 14,
    BLOCK_STATE_OPEN = 1u << 15,
    BLOCK_STATE_BITS_MASK = BLOCK_STATE_FACING_MASK | BLOCK_STATE_UPPER | BLOCK_STATE_OPEN,
};
static_assert((BLOCK_STATE_ID_MASK | BLOCK_STATE_BITS_MASK) == 0xFFFFu);
static_assert((BLOCK_STATE_ID_MASK & BLOCK_STATE_BITS_MASK) == 0u);

/* The four ways, as quarter turns clockwise seen from above, from north. y is north and x is east, as everywhere. A block's
 * facing is the way the player looked when placing it: stairs rise that way, a ladder or a torch on a wall has its wall that
 * way, and a bench lies across it. */
typedef enum block_facing_t : uint8_t {
    BLOCK_FACING_NORTH = 0, /* +y: how every block faced before facings */
    BLOCK_FACING_EAST = 1,  /* +x */
    BLOCK_FACING_SOUTH = 2, /* -y */
    BLOCK_FACING_WEST = 3,  /* -x */
} block_facing_t;
enum : uint32_t { BLOCK_FACINGS = 4u };

BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_id(uint32_t voxel) {
    return (uint16_t)(voxel & BLOCK_STATE_ID_MASK);
}

/* Only the state: the bits above the id. */
BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_bits(uint32_t voxel) {
    return (uint16_t)(voxel & BLOCK_STATE_BITS_MASK);
}

BLOCK_STATE_BOTH_SIDES static inline uint32_t block_state_facing(uint32_t voxel) {
    return (voxel & BLOCK_STATE_FACING_MASK) >> BLOCK_STATE_FACING_SHIFT;
}

BLOCK_STATE_BOTH_SIDES static inline bool block_state_upper(uint32_t voxel) {
    return (voxel & BLOCK_STATE_UPPER) != 0u;
}

BLOCK_STATE_BOTH_SIDES static inline bool block_state_open(uint32_t voxel) {
    return (voxel & BLOCK_STATE_OPEN) != 0u;
}

/* A voxel from its parts. The id must be below BLOCK_STATE_ID_LIMIT and the facing below BLOCK_FACINGS. This sets the bits it is
 * given whether or not the block declares them: run/include/world/block_state/ knows what each block may hold. */
BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_make(uint32_t id, uint32_t facing, bool upper, bool open) {
    return (uint16_t)((id & BLOCK_STATE_ID_MASK) | ((facing & 3u) << BLOCK_STATE_FACING_SHIFT) |
                      (upper ? (uint32_t)BLOCK_STATE_UPPER : 0u) | (open ? (uint32_t)BLOCK_STATE_OPEN : 0u));
}

BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_with_facing(uint32_t voxel, uint32_t facing) {
    return (uint16_t)((voxel & ~(uint32_t)BLOCK_STATE_FACING_MASK & 0xFFFFu) |
                      ((facing & 3u) << BLOCK_STATE_FACING_SHIFT));
}

BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_with_upper(uint32_t voxel, bool upper) {
    return (uint16_t)((voxel & ~(uint32_t)BLOCK_STATE_UPPER & 0xFFFFu) | (upper ? (uint32_t)BLOCK_STATE_UPPER : 0u));
}

BLOCK_STATE_BOTH_SIDES static inline uint16_t block_state_with_open(uint32_t voxel, bool open) {
    return (uint16_t)((voxel & ~(uint32_t)BLOCK_STATE_OPEN & 0xFFFFu) | (open ? (uint32_t)BLOCK_STATE_OPEN : 0u));
}

/* ---- Facings ------------------------------------------------------------------------------------------------------------ */

/* quarters more quarter turns clockwise. 3 is one anticlockwise. */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_facing_turn(uint32_t facing, uint32_t quarters) {
    return (facing + quarters) & 3u;
}

/* The facing a voxel is drawn, aimed at and collides by: its own, and a quarter turn anticlockwise more when it is open, as a door
 * swings on its hinge at its left end. Only a block whose file says open: true ever has the open bit (block_state_clean). */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_state_drawn_facing(uint32_t voxel) {
    return block_facing_turn(block_state_facing(voxel), block_state_open(voxel) ? 3u : 0u);
}

/* Which of its block's six pictures a face of a voxel shows, from the block's own face, in the order below. A block two tall has
 * one set of six for both halves: its upper half shows its top and bottom pictures on its south and north faces, and the other way
 * round, so that a door's two wide faces are its upper picture above and its side picture below. Any other voxel shows its own. */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_state_picture_face(uint32_t voxel, uint32_t own_face) {
    if (!block_state_upper(voxel)) {
        return own_face;
    }
    return own_face == 2u ? 4u : (own_face == 3u ? 5u : (own_face == 4u ? 2u : (own_face == 5u ? 3u : own_face)));
}

/* One step the way it faces. */
BLOCK_STATE_BOTH_SIDES static inline int32_t block_facing_step_x(uint32_t facing) {
    return (facing & 3u) == BLOCK_FACING_EAST ? 1 : ((facing & 3u) == BLOCK_FACING_WEST ? -1 : 0);
}
BLOCK_STATE_BOTH_SIDES static inline int32_t block_facing_step_y(uint32_t facing) {
    return (facing & 3u) == BLOCK_FACING_NORTH ? 1 : ((facing & 3u) == BLOCK_FACING_SOUTH ? -1 : 0);
}

/* The facing a step is, for one of the four unit steps. Anything else is north. */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_facing_of_step(int32_t step_x, int32_t step_y) {
    if (step_x > 0) {
        return BLOCK_FACING_EAST;
    }
    if (step_x < 0) {
        return BLOCK_FACING_WEST;
    }
    return step_y < 0 ? (uint32_t)BLOCK_FACING_SOUTH : (uint32_t)BLOCK_FACING_NORTH;
}

/* The nearest of the four to a look along (dx, dy), the way a player faces a block when placing it. On a tie, x wins; a look
 * straight up or down is north. */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_facing_of_look(float64_t dx, float64_t dy) {
    const float64_t ax = dx < F64(0.0) ? -dx : dx;
    const float64_t ay = dy < F64(0.0) ? -dy : dy;
    if (ax >= ay && ax > F64(0.0)) {
        return dx > F64(0.0) ? (uint32_t)BLOCK_FACING_EAST : (uint32_t)BLOCK_FACING_WEST;
    }
    return dy < F64(0.0) ? (uint32_t)BLOCK_FACING_SOUTH : (uint32_t)BLOCK_FACING_NORTH;
}

/* A face of a block turned to facing, in the order of the block table's faces and the card's: 0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z.
 * to_world is where the block's own face local ends up; to_local is which of its own faces is seen on the world's face. The top
 * and the bottom stay where they are. */
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_facing_face_to_world(uint32_t facing, uint32_t local) {
    /* One quarter clockwise: west to north, east to south, south to west, north to east. */
    uint32_t face = local;
    for (uint32_t i = 0u; i < (facing & 3u) && face < 4u; i += 1u) {
        face = face == 0u ? 3u : (face == 1u ? 2u : (face == 2u ? 0u : 1u));
    }
    return face;
}
BLOCK_STATE_BOTH_SIDES static inline uint32_t block_facing_face_to_local(uint32_t facing, uint32_t world) {
    return block_facing_face_to_world(block_facing_turn(0u, 4u - (facing & 3u)), world);
}

/* A box inside a voxel, in units with size a whole block, turned with its voxel from north to facing about the middle. Only x and
 * y move. low and high are the box's corners on x, y and z, and stay its low and high corners. */
BLOCK_STATE_BOTH_SIDES static inline void block_facing_box_turn(uint32_t facing, uint32_t size, uint8_t low[3],
                                                                uint8_t high[3]) {
    for (uint32_t i = 0u; i < (facing & 3u); i += 1u) {
        /* (x, y) to (y, size - x): north to east. */
        const uint32_t low_x = low[0], high_x = high[0];
        low[0] = low[1];
        high[0] = high[1];
        low[1] = (uint8_t)(size - high_x);
        high[1] = (uint8_t)(size - low_x);
    }
}

/* A point inside a turned voxel, from 0 to 1 on x and y, back to where it is on the block as it faces north: what a texture is
 * read by, so that a block's pictures turn with it. */
BLOCK_STATE_BOTH_SIDES static inline void block_facing_point_to_local(uint32_t facing, float32_t *x, float32_t *y) {
    for (uint32_t i = 0u; i < (facing & 3u); i += 1u) {
        /* The other way from block_facing_box_turn's: (x, y) to (1 - y, x). */
        const float32_t old_x = *x;
        *x = F32(1.0) - *y;
        *y = old_x;
    }
}

#endif
