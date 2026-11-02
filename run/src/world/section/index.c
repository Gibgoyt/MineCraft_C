/* See README.md. */
#include "world/section/index.h"

#include <string.h>

/* The bytes of one block with bits index bits per voxel: the indices, then a palette and its counts, on cache lines. */
#define SECTION_ROUND_UP(bytes) (((bytes) + 63u) & ~63u)
#define SECTION_PALETTE_BLOCK_BYTES(bits) SECTION_ROUND_UP(SECTION_VOXELS / 8u * (bits) + (4u << (bits)))

enum : uint32_t {
    SECTION_BLOCK_ALIGNMENT = 64u,
    SECTION_CLASS_DIRECT = 4u,
    SECTION_CLASS_LIGHT = 5u,
    SECTION_PALETTE_MAX = 256u,
    /* A direct array goes back to a palette only with room left in that palette to grow. Half of the largest, so that a
     * section near the limit is not repacked by every few edits. */
    SECTION_COMPACT_PALETTE_MAX = SECTION_PALETTE_MAX / 2u,
    /* Writes between looks at what is not counted. Each look reads 4096 voxels, so a write pays for one read. */
    SECTION_SCAN_INTERVAL = SECTION_VOXELS,
    /* Twice the most distinct states it holds, so a probe ends soon. A power of two. */
    SECTION_DISTINCT_SLOTS = SECTION_PALETTE_MAX * 2u,
    SECTION_DISTINCT_SHIFT = 23u, /* leaves 9 bits of a 32-bit hash: SECTION_DISTINCT_SLOTS */
};
static_assert(SECTION_DISTINCT_SLOTS == 1u << (32u - SECTION_DISTINCT_SHIFT));
static_assert(SECTION_CLASS_LIGHT + 1u == SECTION_CLASS_COUNT);

static const uint32_t section_class_bytes[SECTION_CLASS_COUNT] = {
    SECTION_PALETTE_BLOCK_BYTES(1u),
    SECTION_PALETTE_BLOCK_BYTES(2u),
    SECTION_PALETTE_BLOCK_BYTES(4u),
    SECTION_PALETTE_BLOCK_BYTES(8u),
    SECTION_VOXELS * 2u,
    SECTION_VOXELS,
};

/* ---- The pool ---------------------------------------------------------------------------------------------------- */

void section_pool_init(section_pool_t *pool, arena_t *arena) {
    memset(pool, 0, sizeof(*pool));
    pool->borrowed_arena = arena;
}

void section_pool_deinit(section_pool_t *pool) {
    memset(pool, 0, sizeof(*pool));
}

void section_pool_reset(section_pool_t *pool) {
    arena_t *const arena = pool->borrowed_arena;
    section_pool_init(pool, arena);
}

/* Contents are not defined. nullptr if there is no room. */
static uint8_t *section_pool_take(section_pool_t *pool, uint32_t class_id) {
    const uint32_t bytes = section_class_bytes[class_id];
    uint8_t *block = pool->borrowed_free[class_id];
    if (block != nullptr) {
        memcpy(&pool->borrowed_free[class_id], block, sizeof(block));
    } else {
        void *fresh = nullptr;
        if (!arena_alloc_try(pool->borrowed_arena, bytes, SECTION_BLOCK_ALIGNMENT, &fresh)) {
            pool->failed_count++;
            return nullptr;
        }
        pool->carved_bytes += bytes;
        block = fresh;
    }
    pool->live_bytes += bytes;
    pool->live_blocks[class_id]++;
    return block;
}

static void section_pool_give(section_pool_t *pool, uint32_t class_id, uint8_t *block) {
    memcpy(block, &pool->borrowed_free[class_id], sizeof(block));
    pool->borrowed_free[class_id] = block;
    pool->live_bytes -= section_class_bytes[class_id];
    pool->live_blocks[class_id]--;
}

static uint32_t section_class_of_bits(uint32_t bits) {
    switch (bits) {
    case 1u:
        return 0u;
    case 2u:
        return 1u;
    case 4u:
        return 2u;
    case 8u:
        return 3u;
    default:
        ASSERT_ALWAYS(bits == SECTION_BITS_DIRECT);
        return SECTION_CLASS_DIRECT;
    }
}

/* ---- Reading and writing a block ---------------------------------------------------------------------------------- */

static uint16_t *section_palette_in(uint8_t *storage, uint32_t bits) {
    return (uint16_t *)(void *)(storage + SECTION_VOXELS / 8u * bits);
}

static uint16_t *section_counts_in(uint8_t *storage, uint32_t bits) {
    return section_palette_in(storage, bits) + (1u << bits);
}

static uint32_t section_slot_read(const uint8_t *storage, uint32_t bits, uint32_t index) {
    const uint32_t bit = index * bits;
    const uint64_t word = ((const uint64_t *)(const void *)storage)[bit >> 6];
    return (uint32_t)(word >> (bit & 63u)) & ((1u << bits) - 1u);
}

static void section_slot_write(uint8_t *storage, uint32_t bits, uint32_t index, uint32_t slot) {
    const uint32_t bit = index * bits;
    uint64_t *const word = &((uint64_t *)(void *)storage)[bit >> 6];
    const uint64_t mask = (uint64_t)((1u << bits) - 1u) << (bit & 63u);
    *word = (*word & ~mask) | ((uint64_t)slot << (bit & 63u));
}

/* The fewest bits whose palette holds this many. */
static uint32_t section_bits_for(uint32_t distinct) {
    uint32_t bits = 1u;
    while ((1u << bits) < distinct) {
        bits <<= 1;
    }
    return bits;
}

static void section_blocks_release(section_t *section, section_pool_t *pool) {
    if (section->borrowed_storage != nullptr) {
        section_pool_give(pool, section_class_of_bits(section->bits), section->borrowed_storage);
    }
    section->borrowed_storage = nullptr;
    section->bits = SECTION_BITS_UNIFORM;
    section->palette_used = 0;
}

static void section_blocks_uniform(section_t *section, section_pool_t *pool, uint16_t state) {
    section_blocks_release(section, pool);
    section->block_uniform = state;
}

/* To a new block of new_bits, keeping only the slots in use. False, with nothing changed, if there is no room. */
static bool section_repack(section_t *section, section_pool_t *pool, uint32_t new_bits) {
    const uint32_t class_id = section_class_of_bits(new_bits);
    uint8_t *const fresh = section_pool_take(pool, class_id);
    if (fresh == nullptr) {
        return false;
    }
    uint8_t *const old = section->borrowed_storage;
    const uint32_t old_bits = section->bits;
    const uint16_t *const old_palette = section_palette_in(old, old_bits);
    const uint16_t *const old_counts = section_counts_in(old, old_bits);
    if (new_bits == SECTION_BITS_DIRECT) {
        uint16_t *const direct = (uint16_t *)(void *)fresh;
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            direct[i] = old_palette[section_slot_read(old, old_bits, i)];
        }
        section->palette_used = 0;
    } else {
        memset(fresh, 0, section_class_bytes[class_id]);
        uint16_t *const palette = section_palette_in(fresh, new_bits);
        uint16_t *const counts = section_counts_in(fresh, new_bits);
        uint8_t remap[SECTION_PALETTE_MAX];
        uint32_t used = 0;
        for (uint32_t slot = 0; slot < (1u << old_bits); slot++) {
            if (old_counts[slot] != 0u) {
                ASSERT(used < (1u << new_bits));
                remap[slot] = (uint8_t)used;
                palette[used] = old_palette[slot];
                counts[used] = old_counts[slot];
                used++;
            }
        }
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            section_slot_write(fresh, new_bits, i, remap[section_slot_read(old, old_bits, i)]);
        }
    }
    section_pool_give(pool, section_class_of_bits(old_bits), old);
    section->borrowed_storage = fresh;
    section->bits = (uint8_t)new_bits;
    return true;
}

/* After a slot emptied. A repack costs 4096 voxels, so this asks for twice the room needed, which means at least half as
 * many states again must come and go before the next one. Failing to shrink changes nothing that matters. */
static void section_shrink_if_worthwhile(section_t *section, section_pool_t *pool) {
    const uint32_t wanted = section_bits_for((uint32_t)section->palette_used * 2u);
    if (wanted < section->bits) {
        (void)section_repack(section, pool, wanted);
    }
}

static void section_note_scan_write(section_t *section, section_pool_t *pool) {
    section->scan_writes++;
    if (section->scan_writes >= SECTION_SCAN_INTERVAL) {
        section_compact(section, pool);
    }
}

static bool section_direct_set(section_t *section, section_pool_t *pool, uint32_t index, uint16_t state) {
    uint16_t *const direct = (uint16_t *)(void *)section->borrowed_storage;
    if (direct[index] != state) {
        direct[index] = state;
        section_note_scan_write(section, pool);
    }
    return true;
}

/* A uniform section's first different voxel. */
static bool section_promote(section_t *section, section_pool_t *pool, uint32_t index, uint16_t state) {
    uint8_t *const fresh = section_pool_take(pool, section_class_of_bits(1u));
    if (fresh == nullptr) {
        return false;
    }
    memset(fresh, 0, section_class_bytes[section_class_of_bits(1u)]);
    uint16_t *const palette = section_palette_in(fresh, 1u);
    uint16_t *const counts = section_counts_in(fresh, 1u);
    palette[0] = section->block_uniform;
    palette[1] = state;
    counts[0] = SECTION_VOXELS - 1u;
    counts[1] = 1u;
    section_slot_write(fresh, 1u, index, 1u);
    section->borrowed_storage = fresh;
    section->bits = 1u;
    section->palette_used = 2u;
    return true;
}

bool section_block_set(section_t *section, section_pool_t *pool, uint32_t index, uint16_t state) {
    ASSERT(index < SECTION_VOXELS);
    if (section->bits == SECTION_BITS_UNIFORM) {
        return state == section->block_uniform || section_promote(section, pool, index, state);
    }
    if (section->bits == SECTION_BITS_DIRECT) {
        return section_direct_set(section, pool, index, state);
    }

    const uint32_t old_slot = section_slot_read(section->borrowed_storage, section->bits, index);
    const uint16_t *palette_now = section_palette_in(section->borrowed_storage, section->bits);
    if (palette_now[old_slot] == state) {
        return true;
    }

    /* Where the state is already, or the first empty slot for it. */
    const uint16_t *counts_now = section_counts_in(section->borrowed_storage, section->bits);
    const uint32_t capacity = 1u << section->bits;
    uint32_t found = capacity;
    uint32_t empty = capacity;
    for (uint32_t slot = 0; slot < capacity; slot++) {
        if (counts_now[slot] == 0u) {
            empty = slot < empty ? slot : empty;
        } else if (palette_now[slot] == state) {
            found = slot;
            break;
        }
    }

    if (found == capacity && empty == capacity) {
        if (counts_now[old_slot] == 1u) {
            /* The voxel was its slot's last, so the slot becomes the new state, and nothing else moves. */
            section_palette_in(section->borrowed_storage, section->bits)[old_slot] = state;
            return true;
        }
        /* Twice 8 bits is SECTION_BITS_DIRECT. */
        if (!section_repack(section, pool, section->bits * 2u)) {
            return false;
        }
        if (section->bits == SECTION_BITS_DIRECT) {
            return section_direct_set(section, pool, index, state);
        }
        /* The repack kept the slots in use in order, and the palette was full, so they all kept their numbers. */
        empty = capacity;
    }

    uint16_t *const palette = section_palette_in(section->borrowed_storage, section->bits);
    uint16_t *const counts = section_counts_in(section->borrowed_storage, section->bits);
    uint32_t new_slot = found;
    if (found == capacity) {
        new_slot = empty;
        palette[new_slot] = state;
        section->palette_used++;
    }
    counts[new_slot]++;
    counts[old_slot]--;
    section_slot_write(section->borrowed_storage, section->bits, index, new_slot);
    if (counts[old_slot] == 0u) {
        section->palette_used--;
        if (section->palette_used == 1u) {
            section_blocks_uniform(section, pool, state);
        } else {
            section_shrink_if_worthwhile(section, pool);
        }
    }
    return true;
}

void section_block_fill(section_t *section, section_pool_t *pool, uint16_t state) {
    section_blocks_uniform(section, pool, state);
}

/* The distinct states in states, in the order first seen, each voxel's slot in out_slots. Returns how many, or
 * max_distinct + 1 if there are more than that, and then the outputs mean nothing. Linear in voxels, whatever the states. */
static uint32_t section_distinct_read(const uint16_t *states, uint32_t max_distinct, uint16_t *out_palette,
                                      uint16_t *out_counts, uint8_t *out_slots) {
    uint16_t table[SECTION_DISTINCT_SLOTS]; /* a slot of the palette + 1, or 0 for empty */
    memset(table, 0, sizeof(table));
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        const uint16_t state = states[i];
        uint32_t probe = ((uint32_t)state * 2654435761u) >> SECTION_DISTINCT_SHIFT;
        while (table[probe] != 0u && out_palette[table[probe] - 1u] != state) {
            probe = (probe + 1u) & (SECTION_DISTINCT_SLOTS - 1u);
        }
        if (table[probe] == 0u) {
            if (distinct == max_distinct) {
                return max_distinct + 1u;
            }
            out_palette[distinct] = state;
            out_counts[distinct] = 0;
            distinct++;
            table[probe] = (uint16_t)distinct;
        }
        const uint32_t slot = table[probe] - 1u;
        out_counts[slot]++;
        out_slots[i] = (uint8_t)slot;
    }
    return distinct;
}

/* states may be the section's own direct array. */
static bool section_blocks_store(section_t *section, section_pool_t *pool, const uint16_t *states,
                                 uint32_t max_palette) {
    uint16_t palette[SECTION_PALETTE_MAX];
    uint16_t counts[SECTION_PALETTE_MAX];
    uint8_t slots[SECTION_VOXELS];
    const uint32_t distinct = section_distinct_read(states, max_palette, palette, counts, slots);

    if (distinct == 1u) {
        section_blocks_uniform(section, pool, palette[0]);
        return true;
    }
    if (distinct > max_palette) {
        if (section->bits == SECTION_BITS_DIRECT) {
            memmove(section->borrowed_storage, states, SECTION_VOXELS * sizeof(uint16_t));
            return true;
        }
        uint8_t *const fresh = section_pool_take(pool, SECTION_CLASS_DIRECT);
        if (fresh == nullptr) {
            return false;
        }
        memcpy(fresh, states, SECTION_VOXELS * sizeof(uint16_t));
        section_blocks_release(section, pool);
        section->borrowed_storage = fresh;
        section->bits = SECTION_BITS_DIRECT;
        return true;
    }

    const uint32_t bits = section_bits_for(distinct);
    const uint32_t class_id = section_class_of_bits(bits);
    uint8_t *const fresh = section_pool_take(pool, class_id);
    if (fresh == nullptr) {
        return false;
    }
    memset(fresh, 0, section_class_bytes[class_id]);
    memcpy(section_palette_in(fresh, bits), palette, distinct * sizeof(uint16_t));
    memcpy(section_counts_in(fresh, bits), counts, distinct * sizeof(uint16_t));
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_slot_write(fresh, bits, i, slots[i]);
    }
    section_blocks_release(section, pool);
    section->borrowed_storage = fresh;
    section->bits = (uint8_t)bits;
    section->palette_used = (uint16_t)distinct;
    return true;
}

bool section_blocks_import(section_t *section, section_pool_t *pool, const uint16_t *states) {
    return section_blocks_store(section, pool, states, SECTION_PALETTE_MAX);
}

void section_blocks_export(const section_t *section, uint16_t *out_states) {
    const uint32_t bits = section->bits;
    if (bits == SECTION_BITS_DIRECT) {
        memcpy(out_states, section->borrowed_storage, SECTION_VOXELS * sizeof(uint16_t));
    } else if (bits == SECTION_BITS_UNIFORM) {
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            out_states[i] = section->block_uniform;
        }
    } else {
        const uint16_t *const palette = section_palette(section);
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            out_states[i] = palette[section_slot_read(section->borrowed_storage, bits, i)];
        }
    }
}

/* ---- Light ------------------------------------------------------------------------------------------------------- */

static void section_light_release(section_t *section, section_pool_t *pool) {
    if (section->borrowed_light != nullptr) {
        section_pool_give(pool, SECTION_CLASS_LIGHT, section->borrowed_light);
    }
    section->borrowed_light = nullptr;
}

bool section_light_set(section_t *section, section_pool_t *pool, uint32_t index, uint8_t packed) {
    ASSERT(index < SECTION_VOXELS);
    if (section->borrowed_light == nullptr) {
        if (packed == section->light_uniform) {
            return true;
        }
        uint8_t *const fresh = section_pool_take(pool, SECTION_CLASS_LIGHT);
        if (fresh == nullptr) {
            return false;
        }
        memset(fresh, section->light_uniform, SECTION_VOXELS);
        section->borrowed_light = fresh;
    }
    if (section->borrowed_light[index] != packed) {
        section->borrowed_light[index] = packed;
        section_note_scan_write(section, pool);
    }
    return true;
}

void section_light_fill(section_t *section, section_pool_t *pool, uint8_t packed) {
    section_light_release(section, pool);
    section->light_uniform = packed;
}

bool section_lights_import(section_t *section, section_pool_t *pool, const uint8_t *packed) {
    if (memcmp(packed, packed + 1, SECTION_VOXELS - 1u) == 0) {
        section_light_fill(section, pool, packed[0]);
        return true;
    }
    if (section->borrowed_light == nullptr) {
        uint8_t *const fresh = section_pool_take(pool, SECTION_CLASS_LIGHT);
        if (fresh == nullptr) {
            return false;
        }
        section->borrowed_light = fresh;
    }
    memmove(section->borrowed_light, packed, SECTION_VOXELS);
    return true;
}

void section_lights_export(const section_t *section, uint8_t *out_packed) {
    if (section->borrowed_light != nullptr) {
        memcpy(out_packed, section->borrowed_light, SECTION_VOXELS);
    } else {
        memset(out_packed, section->light_uniform, SECTION_VOXELS);
    }
}

/* ---- The whole section -------------------------------------------------------------------------------------------- */

void section_init(section_t *section, uint16_t state, uint8_t light) {
    memset(section, 0, sizeof(*section));
    section->block_uniform = state;
    section->light_uniform = light;
}

void section_deinit(section_t *section, section_pool_t *pool) {
    section_blocks_release(section, pool);
    section_light_release(section, pool);
    memset(section, 0, sizeof(*section));
}

void section_compact(section_t *section, section_pool_t *pool) {
    section->scan_writes = 0;
    const uint8_t *const light = section->borrowed_light;
    if (light != nullptr && memcmp(light, light + 1, SECTION_VOXELS - 1u) == 0) {
        section_light_fill(section, pool, light[0]);
    }
    if (section->bits == SECTION_BITS_DIRECT) {
        /* No room for the palette only means that it stays as it is, which is correct and larger. */
        (void)section_blocks_store(section, pool, (const uint16_t *)(const void *)section->borrowed_storage,
                                   SECTION_COMPACT_PALETTE_MAX);
    }
}

uint64_t section_pool_bytes(const section_t *section) {
    uint64_t bytes = 0;
    if (section->borrowed_storage != nullptr) {
        bytes += section_class_bytes[section_class_of_bits(section->bits)];
    }
    if (section->borrowed_light != nullptr) {
        bytes += section_class_bytes[SECTION_CLASS_LIGHT];
    }
    return bytes;
}
