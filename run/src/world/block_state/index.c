/* See README.md. */
#include "world/block_state/index.h"

uint16_t block_state_allowed(uint32_t id) {
    if (id >= BLOCK_COUNT) {
        return 0u;
    }
    const uint32_t states = block_defs[id].states;
    return (uint16_t)(((states & BLOCK_STATES_FACING) != 0u ? (uint32_t)BLOCK_STATE_FACING_MASK : 0u) |
                      ((states & BLOCK_STATES_HALF) != 0u ? (uint32_t)BLOCK_STATE_UPPER : 0u) |
                      ((states & BLOCK_STATES_OPEN) != 0u ? (uint32_t)BLOCK_STATE_OPEN : 0u));
}

bool block_state_turns(uint32_t id) {
    return id < BLOCK_COUNT && (block_defs[id].states & BLOCK_STATES_FACING) != 0u;
}

uint16_t block_state_clean(uint32_t voxel) {
    const uint16_t id = block_state_id(voxel);
    return (uint16_t)(id | (voxel & block_state_allowed(id)));
}

uint16_t block_state_placed(uint32_t id, uint32_t facing) {
    return block_state_turns(id) ? block_state_make(id, facing, false, false) : block_state_id(id);
}
