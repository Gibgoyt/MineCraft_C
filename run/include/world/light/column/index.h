/* See run/src/world/light/column/README.md. */
#ifndef MC_RUN_WORLD_LIGHT_COLUMN_INDEX_H
#define MC_RUN_WORLD_LIGHT_COLUMN_INDEX_H

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "types/block_state/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <stdint.h>

enum : uint32_t {
    WORLD_LIGHT_SKY = 0u,
    WORLD_LIGHT_BLOCK = 1u,
    WORLD_LIGHT_CHANNELS = 2u,
    /* An opacity of this or more passes no light on. */
    WORLD_LIGHT_OPAQUE = 15u,
};
static_assert((uint32_t)WORLD_LIGHT_OPAQUE == (uint32_t)SECTION_LIGHT_MAX);

enum : uint64_t {
    /* A column's voxels, section after section, each in section_voxel_index order: so a voxel's number is z * 256 + y * 16 + x. */
    WORLD_LIGHT_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    /* What world_light_column_alone works in: two lists through the voxels. */
    WORLD_LIGHT_COLUMN_SCRATCH_BYTES = 2u * WORLD_LIGHT_COLUMN_VOXELS * sizeof(uint32_t),
};

/* ---- The rules, as run/src/world/light/README.md gives them. Here so that both ways of working light out share them. ---- */

/* What a voxel of this opacity gets from a neighbour holding value. falls: this is sky going straight down. */
static inline uint32_t world_light_into(uint32_t value, uint32_t opacity, bool falls) {
    if (falls && value == SECTION_LIGHT_MAX && opacity == 0u) {
        return value;
    }
    const uint32_t cost = opacity == 0u ? 1u : opacity;
    return value > cost ? value - cost : 0u;
}

/* By the block alone: light goes round a block the same whichever way it faces. */
static inline const block_def_t *world_light_def(uint16_t state) {
    const uint16_t id = block_state_id(state);
    ASSERT_ALWAYS(id < BLOCK_COUNT);
    return &block_defs[id];
}

static inline bool world_light_is_opaque(const block_def_t *def) {
    return def->opacity >= WORLD_LIGHT_OPAQUE;
}

/* Whether an opaque block holds what it gives off, and not what its faces see. */
static inline bool world_light_keeps_own(const block_def_t *def, uint32_t channel) {
    return channel == WORLD_LIGHT_BLOCK && def->light != 0u;
}

/* ---- One column alone ------------------------------------------------------------------------------------------------- */

/* The light of a column of these states as if no column stood beside it: the one answer to the rules with nothing past its four
 * sides. states and out_packed are WORLD_LIGHT_COLUMN_VOXELS each, in the same order; out_packed gets section_light_pack bytes.
 * scratch is WORLD_LIGHT_COLUMN_SCRATCH_BYTES on 4, and means nothing after. Touches nothing else, so any thread may call it.
 * world_light_column_added_from takes what it gives, and adds what the columns beside bring. */
void world_light_column_alone(const uint16_t *states, uint8_t *out_packed, uint32_t *scratch);

#endif
