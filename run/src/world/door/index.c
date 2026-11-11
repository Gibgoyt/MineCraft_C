/* See README.md. */
#include "world/door/index.h"

bool door_is(uint32_t id) {
    return id < BLOCK_COUNT && block_defs[id].shape == BLOCK_SHAPE_DOOR;
}

bool door_opens_by_hand(uint32_t id) {
    return door_is(id) && id != BLOCK_ID_IRON_DOOR;
}

bool door_half_other(uint16_t voxel, int32_t *out_step_z, uint16_t *out_other) {
    if (!door_is(block_state_id(voxel))) {
        return false;
    }
    const bool upper = block_state_upper(voxel);
    *out_step_z = upper ? -1 : 1;
    *out_other = block_state_with_upper(voxel, !upper);
    return true;
}

uint32_t door_hinged_facing(uint32_t facing, bool door_left, bool door_right, uint32_t left_walls,
                            uint32_t right_walls) {
    bool hinge_right = right_walls > left_walls;
    if (door_left != door_right) {
        hinge_right = door_left;
    }
    return block_facing_turn(facing, hinge_right ? 2u : 0u);
}
