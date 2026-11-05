/* See README.md. */
#include "world/wall_torch/index.h"

bool wall_torch_out(uint16_t block, int32_t *out_x, int32_t *out_y) {
    if (block >= BLOCK_COUNT) {
        return false;
    }
    int32_t x = 0, y = 0;
    const uint8_t shape = block_defs[block].shape;
    if (shape == BLOCK_SHAPE_TORCH_EAST) {
        x = 1;
    } else if (shape == BLOCK_SHAPE_TORCH_WEST) {
        x = -1;
    } else if (shape == BLOCK_SHAPE_TORCH_NORTH) {
        y = 1;
    } else if (shape == BLOCK_SHAPE_TORCH_SOUTH) {
        y = -1;
    } else {
        return false;
    }
    if (out_x != nullptr) {
        *out_x = x;
    }
    if (out_y != nullptr) {
        *out_y = y;
    }
    return true;
}

uint16_t wall_torch_of(int32_t out_x, int32_t out_y) {
    for (uint32_t side = 0u; side < WALL_TORCH_SIDES; side++) {
        int32_t x = 0, y = 0;
        (void)wall_torch_out(WALL_TORCH_BLOCKS[side], &x, &y);
        if (x == out_x && y == out_y) {
            return WALL_TORCH_BLOCKS[side];
        }
    }
    return BLOCK_NO_ID;
}
