/* See run/src/world/block_state/README.md. The bits themselves are in run/include/types/block_state/; this is what each block
 * may hold of them, which only the generated table knows. */
#ifndef MC_RUN_WORLD_BLOCK_STATE_INDEX_H
#define MC_RUN_WORLD_BLOCK_STATE_INDEX_H

#include "tables/blocks/index.h"
#include "types/block_state/index.h"

#include <stdint.h>

/* The state bits a block may hold, of BLOCK_STATE_BITS_MASK, from what its file declares: facing, half and open. 0 for a block
 * with none, and for an id past the table. */
uint16_t block_state_allowed(uint32_t id);

/* Whether the block turns: its file says facing: horizontal. */
bool block_state_turns(uint32_t id);

/* The voxel with only the bits its block declares, and an id past the table as it is, with no state: whatever reads it goes by
 * the id alone. What a save or a hand-made world holds goes through this before it is believed. */
uint16_t block_state_clean(uint32_t voxel);

/* What putting a block down facing this way makes: the block with that facing if it turns, and otherwise its id alone. */
uint16_t block_state_placed(uint32_t id, uint32_t facing);

#endif
