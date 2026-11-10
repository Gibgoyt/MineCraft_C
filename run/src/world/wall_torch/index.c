/* See README.md. */
#include "world/wall_torch/index.h"

bool wall_torch_out(uint16_t voxel, int32_t *out_x, int32_t *out_y) {
    if (block_state_id(voxel) != BLOCK_ID_WALL_TORCH) {
        return false;
    }
    /* It faces its wall, and leans out the other way. */
    const uint32_t facing = block_state_facing(voxel);
    if (out_x != nullptr) {
        *out_x = -block_facing_step_x(facing);
    }
    if (out_y != nullptr) {
        *out_y = -block_facing_step_y(facing);
    }
    return true;
}

uint16_t wall_torch_of(int32_t out_x, int32_t out_y) {
    const bool one_side = (out_x == 0) != (out_y == 0) && out_x >= -1 && out_x <= 1 && out_y >= -1 && out_y <= 1;
    if (!one_side) {
        return BLOCK_NO_ID;
    }
    return block_state_make(BLOCK_ID_WALL_TORCH, block_facing_of_step(-out_x, -out_y), false, false);
}

bool wall_torch_hangs_from(uint16_t voxel, int32_t step_x, int32_t step_y) {
    int32_t out_x = 0, out_y = 0;
    return wall_torch_out(voxel, &out_x, &out_y) && out_x == step_x && out_y == step_y;
}
