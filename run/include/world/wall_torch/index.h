/* See run/src/world/wall_torch/README.md. */
#ifndef MC_RUN_WORLD_WALL_TORCH_INDEX_H
#define MC_RUN_WORLD_WALL_TORCH_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

/* The sides of a block a torch can hang from: one for each facing. */
enum : uint32_t { WALL_TORCH_SIDES = BLOCK_FACINGS };

/* A wall torch facing its wall that way, as a constant for a table: what block_state_make says, and wall_torch_of the other way round. */
#define WALL_TORCH_FACING(facing) ((uint16_t)(BLOCK_ID_WALL_TORCH | ((uint32_t)(facing) << BLOCK_STATE_FACING_SHIFT)))

/* Whether this voxel is a torch on a wall. If so, and they are not nullptr, the way it leans out: one of them 1 or -1 and the other 0.
 * The block it hangs from is the cell the other way, at (x - out_x, y - out_y, z), which is the way it faces. An id alone is a wall
 * torch facing north, leaning south. */
bool wall_torch_out(uint16_t voxel, int32_t *out_x, int32_t *out_y);

/* The wall torch that leans this way, as a voxel facing its wall, or BLOCK_NO_ID for anything but one of the four sides. */
uint16_t wall_torch_of(int32_t out_x, int32_t out_y);

/* Whether the torch in a voxel beside a block, one step (step_x, step_y) from it, hangs from that block. */
bool wall_torch_hangs_from(uint16_t voxel, int32_t step_x, int32_t step_y);

#endif
