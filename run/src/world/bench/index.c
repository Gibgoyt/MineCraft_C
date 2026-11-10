/* See README.md. */
#include "world/bench/index.h"

bool bench_is_half(uint32_t id) {
    return id == BLOCK_ID_WOODWORKING_BENCH_WEST || id == BLOCK_ID_WOODWORKING_BENCH_EAST;
}

bool bench_half_other(uint16_t voxel, int32_t *out_step_x, int32_t *out_step_y, uint16_t *out_other) {
    const uint16_t id = block_state_id(voxel);
    if (!bench_is_half(id)) {
        return false;
    }
    const uint32_t facing = block_state_facing(voxel);
    /* The placer's right hand: a quarter turn clockwise from the way they looked. */
    const uint32_t right = block_facing_turn(facing, 1u);
    const bool first = id == BLOCK_ID_WOODWORKING_BENCH_WEST;
    *out_step_x = first ? block_facing_step_x(right) : -block_facing_step_x(right);
    *out_step_y = first ? block_facing_step_y(right) : -block_facing_step_y(right);
    *out_other =
        block_state_with_facing(first ? BLOCK_ID_WOODWORKING_BENCH_EAST : BLOCK_ID_WOODWORKING_BENCH_WEST, facing);
    return true;
}
