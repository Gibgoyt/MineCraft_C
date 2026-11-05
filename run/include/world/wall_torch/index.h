/* See run/src/world/wall_torch/README.md. */
#ifndef MC_RUN_WORLD_WALL_TORCH_INDEX_H
#define MC_RUN_WORLD_WALL_TORCH_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

enum : uint32_t { WALL_TORCH_SIDES = 4u };

/* Each torch on a wall, one for each side of a block. */
static const uint16_t WALL_TORCH_BLOCKS[WALL_TORCH_SIDES] = {BLOCK_ID_TORCH_WALL_EAST, BLOCK_ID_TORCH_WALL_WEST,
                                                             BLOCK_ID_TORCH_WALL_NORTH, BLOCK_ID_TORCH_WALL_SOUTH};

/* Whether this block is a torch on a wall. If so, and they are not nullptr, the way it leans out: one of them 1 or -1 and the other 0.
 * The block it hangs from is the cell the other way, at (x - out_x, y - out_y, z). */
bool wall_torch_out(uint16_t block, int32_t *out_x, int32_t *out_y);

/* The torch that leans this way, or BLOCK_NO_ID for anything but one of the four sides. */
uint16_t wall_torch_of(int32_t out_x, int32_t out_y);

#endif
