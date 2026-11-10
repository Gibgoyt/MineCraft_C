/* See README.md. */
#include "save/world/index.h"

#include "tables/blocks/index.h"
#include "world/tick/index.h"

#include <string.h>

static_assert((uint32_t)SAVE_WORLD_RECORD_LEVELS_BYTES == (uint32_t)WORLD_TICK_PAGE_BYTES);
static_assert((uint32_t)SECTION_BITS_DIRECT < (uint32_t)SAVE_WORLD_RECORD_HAS_LEVELS);
static_assert(BLOCK_COUNT <= 65536u);

static uint32_t save_world_bits_for(uint32_t palette_count) {
    if (palette_count <= 1u) {
        return 0u;
    }
    if (palette_count <= 2u) {
        return 1u;
    }
    if (palette_count <= 4u) {
        return 2u;
    }
    return palette_count <= 16u ? 4u : 8u;
}

static uint64_t save_world_record_bytes_for(uint32_t bits, uint32_t palette_count) {
    return (uint64_t)SAVE_WORLD_RECORD_HEAD_BYTES + (uint64_t)palette_count * sizeof(uint16_t) +
           (uint64_t)SECTION_VOXELS * bits / 8u;
}

static void save_world_record_head_write(uint8_t *out, uint32_t bits, uint32_t palette_count) {
    const uint16_t head[2] = {(uint16_t)bits, (uint16_t)palette_count};
    static_assert(sizeof(head) == SAVE_WORLD_RECORD_HEAD_BYTES);
    memcpy(out, head, sizeof(head));
}

uint32_t save_world_record_encode(const uint16_t *states, uint16_t *palette_slots, uint8_t *out) {
    uint8_t *const body = out + SAVE_WORLD_RECORD_HEAD_BYTES;
    uint16_t palette[SAVE_WORLD_PALETTE_MAX];
    uint32_t palette_count = 0u;
    bool direct = false;
    for (uint32_t i = 0u; i < SECTION_VOXELS && !direct; i++) {
        const uint16_t state = states[i];
        if (palette_slots[state] != 0u) {
            continue;
        }
        if (palette_count == SAVE_WORLD_PALETTE_MAX) {
            direct = true;
            continue;
        }
        palette[palette_count] = state;
        palette_count++;
        palette_slots[state] = (uint16_t)palette_count;
    }
    uint64_t bytes = 0u;
    if (direct) {
        bytes = save_world_record_bytes_for(SECTION_BITS_DIRECT, 0u);
        save_world_record_head_write(out, SECTION_BITS_DIRECT, 0u);
        memcpy(body, states, (uint64_t)SECTION_VOXELS * sizeof(uint16_t));
    } else {
        const uint32_t bits = save_world_bits_for(palette_count);
        bytes = save_world_record_bytes_for(bits, palette_count);
        save_world_record_head_write(out, bits, palette_count);
        memcpy(body, palette, (uint64_t)palette_count * sizeof(uint16_t));
        uint8_t *const indices = body + (uint64_t)palette_count * sizeof(uint16_t);
        if (bits != 0u) {
            memset(indices, 0, (uint64_t)SECTION_VOXELS * bits / 8u);
            for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                const uint32_t bit = i * bits;
                const uint32_t index = palette_slots[states[i]] - 1u;
                indices[bit >> 3] |= (uint8_t)(index << (bit & 7u));
            }
        }
    }
    for (uint32_t i = 0u; i < palette_count; i++) {
        palette_slots[palette[i]] = 0u;
    }
    return (uint32_t)bytes;
}

/* Every voxel's level is one a fluid can have. */
static bool save_world_levels_valid(const uint8_t *levels) {
    for (uint32_t i = 0u; i < SAVE_WORLD_RECORD_LEVELS_BYTES; i++) {
        if ((levels[i] & 15u) > WORLD_TICK_LEVEL_FALLING || (levels[i] >> 4) > WORLD_TICK_LEVEL_FALLING) {
            return false;
        }
    }
    return true;
}

/* All of a record but its states being written: so that a bad one leaves what it was to go over alone. */
bool save_world_record_valid_within(const uint8_t *record, uint64_t bytes, uint32_t state_count, uint32_t *out_bits,
                                    uint32_t *out_palette_count, uint16_t *out_palette) {
    if (bytes < SAVE_WORLD_RECORD_HEAD_BYTES || bytes > SAVE_WORLD_ENTRY_BYTES_MAX) {
        return false;
    }
    uint16_t head[2];
    memcpy(head, record, sizeof(head));
    const bool has_levels = (head[0] & SAVE_WORLD_RECORD_HAS_LEVELS) != 0u;
    const uint64_t levels_bytes = has_levels ? SAVE_WORLD_RECORD_LEVELS_BYTES : 0u;
    if (bytes < SAVE_WORLD_RECORD_HEAD_BYTES + levels_bytes ||
        (has_levels && !save_world_levels_valid(record + bytes - levels_bytes))) {
        return false;
    }
    const uint32_t bits = head[0] & ~(uint32_t)SAVE_WORLD_RECORD_HAS_LEVELS;
    const uint32_t palette_count = head[1];
    const uint64_t blocks_bytes = bytes - levels_bytes;
    const uint8_t *const body = record + SAVE_WORLD_RECORD_HEAD_BYTES;
    *out_bits = bits;
    *out_palette_count = palette_count;
    if (bits == SECTION_BITS_DIRECT) {
        if (palette_count != 0u || blocks_bytes != save_world_record_bytes_for(bits, 0u)) {
            return false;
        }
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            uint16_t state = 0u;
            memcpy(&state, body + (uint64_t)i * sizeof(state), sizeof(state));
            if (block_state_id(state) >= state_count) {
                return false;
            }
        }
        return true;
    }
    if ((bits != 0u && bits != 1u && bits != 2u && bits != 4u && bits != 8u) || palette_count == 0u ||
        palette_count > (1u << bits) || blocks_bytes != save_world_record_bytes_for(bits, palette_count)) {
        return false;
    }
    memcpy(out_palette, body, (uint64_t)palette_count * sizeof(uint16_t));
    for (uint32_t i = 0u; i < palette_count; i++) {
        if (block_state_id(out_palette[i]) >= state_count) {
            return false;
        }
    }
    if (palette_count == (1u << bits)) {
        return true; /* any index is in the palette */
    }
    const uint8_t *const indices = body + (uint64_t)palette_count * sizeof(uint16_t);
    const uint32_t mask = (1u << bits) - 1u;
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        const uint32_t bit = i * bits;
        if ((((uint32_t)indices[bit >> 3] >> (bit & 7u)) & mask) >= palette_count) {
            return false;
        }
    }
    return true;
}

bool save_world_record_decode(const uint8_t *record, uint64_t bytes, uint16_t *out) {
    return save_world_record_decode_within(record, bytes, BLOCK_COUNT, out);
}

bool save_world_record_decode_within(const uint8_t *record, uint64_t bytes, uint32_t state_count, uint16_t *out) {
    uint16_t palette[SAVE_WORLD_PALETTE_MAX];
    uint32_t bits = 0u;
    uint32_t palette_count = 0u;
    if (!save_world_record_valid_within(record, bytes, state_count, &bits, &palette_count, palette)) {
        return false;
    }
    const uint8_t *const body = record + SAVE_WORLD_RECORD_HEAD_BYTES;
    if (bits == SECTION_BITS_DIRECT) {
        memcpy(out, body, (uint64_t)SECTION_VOXELS * sizeof(uint16_t));
    } else if (bits == 0u) {
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            out[i] = palette[0];
        }
    } else {
        const uint8_t *const indices = body + (uint64_t)palette_count * sizeof(uint16_t);
        const uint32_t mask = (1u << bits) - 1u;
        for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
            const uint32_t bit = i * bits;
            out[i] = palette[((uint32_t)indices[bit >> 3] >> (bit & 7u)) & mask];
        }
    }
    return true;
}

const uint8_t *save_world_record_levels(const uint8_t *record, uint64_t bytes) {
    uint16_t bits = 0u;
    memcpy(&bits, record, sizeof(bits));
    return (bits & SAVE_WORLD_RECORD_HAS_LEVELS) != 0u ? record + bytes - SAVE_WORLD_RECORD_LEVELS_BYTES : nullptr;
}
