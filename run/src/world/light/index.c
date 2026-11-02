/* See README.md. */
#include "world/light/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <string.h>

enum : uint32_t {
    LIGHT_NONE = UINT32_MAX,
    LIGHT_SKY = 0u,
    LIGHT_BLOCK = 1u,
    LIGHT_CHANNELS = 2u,
    LIGHT_OPAQUE = 15u,
    LIGHT_LAYER_VOXELS = SECTION_SIZE * SECTION_SIZE,
    /* A voxel's number in a column: its section above section_voxel_index, so that z runs through all of it. */
    LIGHT_SECTION_SHIFT = 12u,
    LIGHT_INDEX_MASK = SECTION_VOXELS - 1u,
    LIGHT_X_STEP = 1u,
    LIGHT_Y_STEP = SECTION_SIZE,
    LIGHT_Z_STEP = SECTION_SIZE * SECTION_SIZE,
    LIGHT_Z_TOP = COLUMN_SECTIONS * SECTION_SIZE - 1u,
    /* In a queue's entry: a value in 4 bits, the voxel in 18, and the slot above them. */
    LIGHT_ENTRY_VOXEL_SHIFT = 4u,
    LIGHT_ENTRY_SLOT_SHIFT = 22u,
    LIGHT_ENTRY_VOXEL_MASK = (1u << 18) - 1u,
};
static_assert(COLUMN_SECTIONS <= 1u << (18u - 12u)); /* a voxel's number fits its 18 bits */
static_assert(WORLD_LIGHT_LEVELS == 16u);
static_assert((uint32_t)LIGHT_OPAQUE == (uint32_t)SECTION_LIGHT_MAX);

typedef enum light_direction_t : uint8_t {
    LIGHT_WEST = 0,  /* -x */
    LIGHT_EAST = 1,  /* +x */
    LIGHT_SOUTH = 2, /* -y */
    LIGHT_NORTH = 3, /* +y */
    LIGHT_DOWN = 4,  /* -z */
    LIGHT_UP = 5,    /* +z */
    LIGHT_DIRECTIONS = 6,
} light_direction_t;

/* ---- The rules ---------------------------------------------------------------------------------------------------- */

/* What a voxel of this opacity gets from a neighbour holding value. falls: this is sky going straight down. */
static inline uint32_t light_into(uint32_t value, uint32_t opacity, bool falls) {
    if (falls && value == SECTION_LIGHT_MAX && opacity == 0u) {
        return value;
    }
    const uint32_t cost = opacity == 0u ? 1u : opacity;
    return value > cost ? value - cost : 0u;
}

static inline const block_def_t *light_block_def(uint16_t state) {
    ASSERT_ALWAYS(state < BLOCK_COUNT);
    return &block_defs[state];
}

static inline uint32_t light_channel(uint8_t packed, uint32_t channel) {
    return channel == LIGHT_SKY ? section_light_sky(packed) : section_light_block(packed);
}

static inline uint8_t light_with_channel(uint8_t packed, uint32_t channel, uint32_t value) {
    return channel == LIGHT_SKY ? section_light_pack(value, section_light_block(packed))
                                : section_light_pack(section_light_sky(packed), value);
}

/* ---- Places ------------------------------------------------------------------------------------------------------- */

static inline column_t *light_column(const world_light_t *light, uint32_t slot) {
    return &light->borrowed_map->borrowed_columns[slot];
}

static inline const section_t *light_section_of(const world_light_t *light, uint32_t slot, uint32_t voxel) {
    return &light_column(light, slot)->sections[voxel >> LIGHT_SECTION_SHIFT];
}

static inline uint32_t light_read(const world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t channel) {
    return light_channel(section_light_get(light_section_of(light, slot, voxel), voxel & LIGHT_INDEX_MASK), channel);
}

static inline const block_def_t *light_def_at(const world_light_t *light, uint32_t slot, uint32_t voxel) {
    return light_block_def(section_block_get(light_section_of(light, slot, voxel), voxel & LIGHT_INDEX_MASK));
}

static bool light_slot_is_lit(const world_light_t *light, uint32_t slot) {
    const world_map_slot_t *const entry = &light->borrowed_map->borrowed_slots[slot];
    return entry->state == WORLD_MAP_STATE_READY && light->borrowed_lit[slot] == entry->generation;
}

/* A chunk's slot, if it holds a lit column. */
static bool light_lit_slot(const world_light_t *light, int32_t chunk_x, int32_t chunk_y, uint32_t *out_slot) {
    const world_map_t *const map = light->borrowed_map;
    uint32_t slot = 0u;
    if (!world_map_slot_of(map, chunk_x, chunk_y, &slot)) {
        return false;
    }
    const world_map_slot_t *const entry = &map->borrowed_slots[slot];
    if (entry->chunk_x != chunk_x || entry->chunk_y != chunk_y || !light_slot_is_lit(light, slot)) {
        return false;
    }
    *out_slot = slot;
    return true;
}

/* The lit column beside this one, in a direction along the ground. */
static bool light_beside(const world_light_t *light, uint32_t slot, uint32_t direction, uint32_t *out_slot) {
    const world_map_slot_t *const entry = &light->borrowed_map->borrowed_slots[slot];
    switch (direction) {
    case LIGHT_WEST:
        return light_lit_slot(light, entry->chunk_x - 1, entry->chunk_y, out_slot);
    case LIGHT_EAST:
        return light_lit_slot(light, entry->chunk_x + 1, entry->chunk_y, out_slot);
    case LIGHT_SOUTH:
        return light_lit_slot(light, entry->chunk_x, entry->chunk_y - 1, out_slot);
    default:
        return light_lit_slot(light, entry->chunk_x, entry->chunk_y + 1, out_slot);
    }
}

/* The voxel next to this one, if there is one that counts. */
static inline bool light_neighbour(const world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t direction,
                                   uint32_t *out_slot, uint32_t *out_voxel) {
    const uint32_t x = voxel & (SECTION_SIZE - 1u);
    const uint32_t y = (voxel >> 4) & (SECTION_SIZE - 1u);
    const uint32_t z = voxel >> 8;
    *out_slot = slot;
    switch (direction) {
    case LIGHT_WEST:
        if (x != 0u) {
            *out_voxel = voxel - LIGHT_X_STEP;
            return true;
        }
        *out_voxel = voxel + (SECTION_SIZE - 1u) * LIGHT_X_STEP;
        return light_beside(light, slot, direction, out_slot);
    case LIGHT_EAST:
        if (x != SECTION_SIZE - 1u) {
            *out_voxel = voxel + LIGHT_X_STEP;
            return true;
        }
        *out_voxel = voxel - (SECTION_SIZE - 1u) * LIGHT_X_STEP;
        return light_beside(light, slot, direction, out_slot);
    case LIGHT_SOUTH:
        if (y != 0u) {
            *out_voxel = voxel - LIGHT_Y_STEP;
            return true;
        }
        *out_voxel = voxel + (SECTION_SIZE - 1u) * LIGHT_Y_STEP;
        return light_beside(light, slot, direction, out_slot);
    case LIGHT_NORTH:
        if (y != SECTION_SIZE - 1u) {
            *out_voxel = voxel + LIGHT_Y_STEP;
            return true;
        }
        *out_voxel = voxel - (SECTION_SIZE - 1u) * LIGHT_Y_STEP;
        return light_beside(light, slot, direction, out_slot);
    case LIGHT_DOWN:
        *out_voxel = voxel - LIGHT_Z_STEP;
        return z != 0u;
    default:
        *out_voxel = voxel + LIGHT_Z_STEP;
        return z != LIGHT_Z_TOP;
    }
}

/* ---- Writing ------------------------------------------------------------------------------------------------------ */

static void light_touch(world_light_t *light, uint32_t slot, uint64_t section_mask) {
    if (light->borrowed_touched[slot] == 0u) {
        light->borrowed_touched_slots[light->touched_count] = slot;
        light->touched_count++;
    }
    light->borrowed_touched[slot] |= section_mask;
}

static void light_note_no_memory(world_light_t *light) {
    light->stale = 1u;
    light->refused = 1u;
}

/* False if the pool had no room, and then nothing changed. */
static bool light_write(world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t channel, uint32_t value) {
    const uint32_t section_index = voxel >> LIGHT_SECTION_SHIFT;
    section_t *const section = &light_column(light, slot)->sections[section_index];
    const uint32_t index = voxel & LIGHT_INDEX_MASK;
    const uint8_t packed = light_with_channel(section_light_get(section, index), channel, value);
    if (!section_light_set(section, light->borrowed_map->borrowed_pool, index, packed)) {
        light_note_no_memory(light);
        return false;
    }
    light->voxels_written++;
    light_touch(light, slot, 1ull << section_index);
    return true;
}

/* ---- The queues --------------------------------------------------------------------------------------------------- */

static uint64_t light_entry(uint32_t slot, uint32_t voxel, uint32_t value) {
    return ((uint64_t)slot << LIGHT_ENTRY_SLOT_SHIFT) | ((uint64_t)voxel << LIGHT_ENTRY_VOXEL_SHIFT) | (uint64_t)value;
}
static inline uint32_t light_entry_slot(uint64_t entry) {
    return (uint32_t)(entry >> LIGHT_ENTRY_SLOT_SHIFT);
}
static inline uint32_t light_entry_voxel(uint64_t entry) {
    return (uint32_t)(entry >> LIGHT_ENTRY_VOXEL_SHIFT) & LIGHT_ENTRY_VOXEL_MASK;
}
static inline uint32_t light_entry_value(uint64_t entry) {
    return (uint32_t)entry & SECTION_LIGHT_MAX;
}

/* value is what the voxel held before it went dark, or 0 if it is only to be looked at. */
static void light_wave_push(world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t value) {
    if (light->wave_count == light->wave_capacity) {
        light->overflowed = 1u;
        return;
    }
    light->borrowed_wave[light->wave_count] = light_entry(slot, voxel, value);
    light->wave_count++;
}

static void light_bucket_push(world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t level) {
    uint32_t node = light->node_free;
    if (node != LIGHT_NONE) {
        light->node_free = light->borrowed_node_next[node];
    } else if (light->node_used < light->wave_capacity) {
        node = light->node_used;
        light->node_used++;
    } else {
        light->overflowed = 1u;
        return;
    }
    light->borrowed_nodes[node] = light_entry(slot, voxel, 0u);
    light->borrowed_node_next[node] = light->bucket[level];
    light->bucket[level] = node;
}

static void light_queues_clear(world_light_t *light) {
    light->wave_count = 0u;
    light->node_used = 0u;
    light->node_free = LIGHT_NONE;
    light->overflowed = 0u;
    for (uint32_t level = 0u; level < WORLD_LIGHT_LEVELS; level++) {
        light->bucket[level] = LIGHT_NONE;
    }
}

/* ---- A pass: see "A pass" in the README --------------------------------------------------------------------------- */

/* A voxel that may be wrong: it goes dark, and is looked at again. */
static void light_seed(world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t channel) {
    const uint32_t value = light_read(light, slot, voxel, channel);
    if (value == 0u) {
        light_wave_push(light, slot, voxel, 0u);
    } else if (light_write(light, slot, voxel, channel, 0u)) {
        light_wave_push(light, slot, voxel, value);
    }
}

/* What a voxel is owed, from what it gives off and from its neighbours as they now are. */
static uint32_t light_owed(const world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t channel) {
    const block_def_t *const def = light_def_at(light, slot, voxel);
    const uint32_t opacity = def->opacity;
    uint32_t best = 0u;
    if (channel == LIGHT_BLOCK) {
        best = def->light;
    } else if ((voxel >> 8) == LIGHT_Z_TOP) {
        best = light_into(SECTION_LIGHT_MAX, opacity, true);
    }
    if (opacity >= LIGHT_OPAQUE) {
        return best; /* nothing gets in */
    }
    for (uint32_t direction = 0u; direction < LIGHT_DIRECTIONS; direction++) {
        uint32_t near_slot = 0u;
        uint32_t near_voxel = 0u;
        if (light_neighbour(light, slot, voxel, direction, &near_slot, &near_voxel)) {
            const uint32_t offered = light_into(light_read(light, near_slot, near_voxel, channel), opacity,
                                                channel == LIGHT_SKY && direction == LIGHT_UP);
            best = offered > best ? offered : best;
        }
    }
    return best;
}

static void light_pass_dark(world_light_t *light, uint32_t channel) {
    for (uint32_t head = 0u; head < light->wave_count; head++) {
        const uint64_t entry = light->borrowed_wave[head];
        const uint32_t value = light_entry_value(entry);
        if (value == 0u) {
            continue;
        }
        const uint32_t slot = light_entry_slot(entry);
        const uint32_t voxel = light_entry_voxel(entry);
        for (uint32_t direction = 0u; direction < LIGHT_DIRECTIONS; direction++) {
            uint32_t near_slot = 0u;
            uint32_t near_voxel = 0u;
            if (!light_neighbour(light, slot, voxel, direction, &near_slot, &near_voxel)) {
                continue;
            }
            const uint32_t held = light_read(light, near_slot, near_voxel, channel);
            if (held == 0u || held != light_into(value, light_def_at(light, near_slot, near_voxel)->opacity,
                                                 channel == LIGHT_SKY && direction == LIGHT_DOWN)) {
                continue;
            }
            if (light_write(light, near_slot, near_voxel, channel, 0u)) {
                light_wave_push(light, near_slot, near_voxel, held);
            }
        }
    }
}

static void light_pass_look(world_light_t *light, uint32_t channel) {
    for (uint32_t head = 0u; head < light->wave_count; head++) {
        const uint64_t entry = light->borrowed_wave[head];
        const uint32_t slot = light_entry_slot(entry);
        const uint32_t voxel = light_entry_voxel(entry);
        const uint32_t held = light_read(light, slot, voxel, channel);
        if (held == SECTION_LIGHT_MAX) {
            continue; /* nothing can be owed more */
        }
        const uint32_t owed = light_owed(light, slot, voxel, channel);
        if (owed > held && light_write(light, slot, voxel, channel, owed)) {
            light_bucket_push(light, slot, voxel, owed);
        }
    }
}

static void light_pass_bright(world_light_t *light, uint32_t channel) {
    for (uint32_t level = SECTION_LIGHT_MAX; level > 0u; level--) {
        while (light->bucket[level] != LIGHT_NONE) {
            const uint32_t node = light->bucket[level];
            const uint64_t entry = light->borrowed_nodes[node];
            light->bucket[level] = light->borrowed_node_next[node];
            light->borrowed_node_next[node] = light->node_free;
            light->node_free = node;
            const uint32_t slot = light_entry_slot(entry);
            const uint32_t voxel = light_entry_voxel(entry);
            if (light_read(light, slot, voxel, channel) != level) {
                continue; /* got brighter since this was queued, and is queued for that too */
            }
            for (uint32_t direction = 0u; direction < LIGHT_DIRECTIONS; direction++) {
                uint32_t near_slot = 0u;
                uint32_t near_voxel = 0u;
                if (!light_neighbour(light, slot, voxel, direction, &near_slot, &near_voxel)) {
                    continue;
                }
                const uint32_t offered = light_into(level, light_def_at(light, near_slot, near_voxel)->opacity,
                                                    channel == LIGHT_SKY && direction == LIGHT_DOWN);
                if (offered > light_read(light, near_slot, near_voxel, channel) &&
                    light_write(light, near_slot, near_voxel, channel, offered)) {
                    light_bucket_push(light, near_slot, near_voxel, offered);
                }
            }
        }
    }
}

/* Runs what the seeds asked for. If it runs out of room, overflowed stays set, and light_finish rebuilds and clears up. */
static void light_pass(world_light_t *light, uint32_t channel) {
    if (light->overflowed == 0u) {
        light_pass_dark(light, channel);
    }
    if (light->overflowed == 0u) {
        light_pass_look(light, channel);
    }
    if (light->overflowed == 0u) {
        light_pass_bright(light, channel);
    }
    if (light->overflowed == 0u) {
        light_queues_clear(light);
    }
}

/* ---- Sky falling, one column at a time ---------------------------------------------------------------------------- */

static void light_section_fill(world_light_t *light, uint32_t slot, uint32_t section_index, uint8_t packed) {
    section_t *const section = &light_column(light, slot)->sections[section_index];
    if (section->borrowed_light != nullptr || section->light_uniform != packed) {
        section_light_fill(section, light->borrowed_map->borrowed_pool, packed);
        light_touch(light, slot, 1ull << section_index);
    }
}

static bool light_section_holds(const section_t *section, const uint8_t *packed) {
    if (section->borrowed_light != nullptr) {
        return memcmp(section->borrowed_light, packed, SECTION_VOXELS) == 0;
    }
    return packed[0] == section->light_uniform && memcmp(packed, packed + 1, SECTION_VOXELS - 1u) == 0;
}

/* One section: the layer above it in, the layer below it out. Each voxel gets what falls on it and what it gives off. */
static void light_section_fall(world_light_t *light, uint32_t slot, uint32_t section_index, uint8_t *layer) {
    section_t *const section = &light_column(light, slot)->sections[section_index];
    uint8_t *const packed = light->borrowed_import;
    for (uint32_t z = SECTION_SIZE; z > 0u; z--) {
        for (uint32_t i = 0u; i < LIGHT_LAYER_VOXELS; i++) {
            const uint32_t index = ((z - 1u) << 8) | i;
            const block_def_t *const def = light_block_def(section_block_get(section, index));
            const uint32_t sky = light_into(layer[i], def->opacity, true);
            layer[i] = (uint8_t)sky;
            packed[index] = section_light_pack(sky, def->light);
        }
    }
    if (light_section_holds(section, packed)) {
        return;
    }
    if (!section_lights_import(section, light->borrowed_map->borrowed_pool, packed)) {
        light_note_no_memory(light);
        return;
    }
    light_touch(light, slot, 1ull << section_index);
}

/* A whole column, with nothing from the side. */
static void light_column_fall(world_light_t *light, uint32_t slot) {
    uint8_t layer[LIGHT_LAYER_VOXELS];
    memset(layer, SECTION_LIGHT_MAX, sizeof(layer));
    bool all_sky = true;
    bool all_dark = false;
    const column_t *const column = light_column(light, slot);
    for (uint32_t i = COLUMN_SECTIONS; i > 0u; i--) {
        const uint32_t section_index = i - 1u;
        const section_t *const section = &column->sections[section_index];
        if (section->bits == SECTION_BITS_UNIFORM) {
            const block_def_t *const def = light_block_def(section->block_uniform);
            if (all_sky && def->opacity == 0u) {
                light_section_fill(light, slot, section_index, section_light_pack(SECTION_LIGHT_MAX, def->light));
                continue;
            }
            if (all_dark || def->opacity >= LIGHT_OPAQUE) {
                light_section_fill(light, slot, section_index, section_light_pack(0u, def->light));
                memset(layer, 0, sizeof(layer));
                all_sky = false;
                all_dark = true;
                continue;
            }
        }
        light_section_fall(light, slot, section_index, layer);
        all_sky = true;
        all_dark = true;
        for (uint32_t v = 0u; v < LIGHT_LAYER_VOXELS; v++) {
            all_sky = all_sky && layer[v] == SECTION_LIGHT_MAX;
            all_dark = all_dark && layer[v] == 0u;
        }
    }
}

/* If every voxel of a section holds one value in this channel, that value. An array is looked through: sky often has shape where
 * block light is all 0. */
static bool light_section_one_value(const section_t *section, uint32_t channel, uint32_t *out_value) {
    if (section->borrowed_light == nullptr) {
        *out_value = light_channel(section->light_uniform, channel);
        return true;
    }
    const uint32_t value = light_channel(section->borrowed_light[0], channel);
    for (uint32_t i = 1u; i < SECTION_VOXELS; i++) {
        if (light_channel(section->borrowed_light[i], channel) != value) {
            return false;
        }
    }
    *out_value = value;
    return true;
}

/* Whether nothing could raise this section once sky has fallen on it: it holds one value, and it is opaque, or that value is at
 * least what any section beside it could offer. Worth asking before 4096 looks. */
static bool light_section_settled(const world_light_t *light, uint32_t slot, uint32_t section_index, uint32_t channel) {
    const column_t *const column = light_column(light, slot);
    const section_t *const section = &column->sections[section_index];
    if (section->bits == SECTION_BITS_UNIFORM && light_block_def(section->block_uniform)->opacity >= LIGHT_OPAQUE &&
        section->borrowed_light == nullptr) {
        return true;
    }
    uint32_t value = 0u;
    if (!light_section_one_value(section, channel, &value)) {
        return false;
    }
    uint32_t near_value = 0u;
    if (section_index + 1u == COLUMN_SECTIONS) {
        if (channel == LIGHT_SKY && value != SECTION_LIGHT_MAX) {
            return false;
        }
    } else {
        if (!light_section_one_value(&column->sections[section_index + 1u], channel, &near_value)) {
            return false;
        }
        if (near_value > value + 1u ||
            (channel == LIGHT_SKY && near_value == SECTION_LIGHT_MAX && value != near_value)) {
            return false;
        }
    }
    if (section_index != 0u) {
        if (!light_section_one_value(&column->sections[section_index - 1u], channel, &near_value) ||
            near_value > value + 1u) {
            return false;
        }
    }
    for (uint32_t direction = 0u; direction < LIGHT_DOWN; direction++) {
        uint32_t beside = 0u;
        if (light_beside(light, slot, direction, &beside)) {
            if (!light_section_one_value(&light_column(light, beside)->sections[section_index], channel, &near_value) ||
                near_value > value + 1u) {
                return false;
            }
        }
    }
    return true;
}

/* ---- Rebuilding: no queue, and so no way to run out --------------------------------------------------------------- */

static bool light_rebuild_voxel(world_light_t *light, uint32_t slot, uint32_t voxel, uint32_t channel) {
    const uint32_t owed = light_owed(light, slot, voxel, channel);
    return owed > light_read(light, slot, voxel, channel) && light_write(light, slot, voxel, channel, owed);
}

/* One column's sections, up and back down, each voxel taking what it is owed. True if any did. */
static bool light_rebuild_sweep(world_light_t *light, uint32_t slot, uint32_t channel) {
    bool changed = false;
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        if (!light_section_settled(light, slot, s, channel)) {
            for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                changed = light_rebuild_voxel(light, slot, (s << LIGHT_SECTION_SHIFT) | i, channel) || changed;
            }
        }
    }
    for (uint32_t s = COLUMN_SECTIONS; s > 0u; s--) {
        if (!light_section_settled(light, slot, s - 1u, channel)) {
            for (uint32_t i = SECTION_VOXELS; i > 0u; i--) {
                changed =
                    light_rebuild_voxel(light, slot, ((s - 1u) << LIGHT_SECTION_SHIFT) | (i - 1u), channel) || changed;
            }
        }
    }
    return changed;
}

static void light_rebuild_all(world_light_t *light) {
    const world_map_t *const map = light->borrowed_map;
    light->rebuild_count++;
    light->stale = 0u;
    light_queues_clear(light);
    for (uint32_t i = 0u; i < map->active_count; i++) {
        const uint32_t slot = map->borrowed_active[i];
        if (light_slot_is_lit(light, slot)) {
            light_column_fall(light, slot);
            light_touch(light, slot, WORLD_EDIT_SECTIONS_ALL);
        }
    }
    for (uint32_t channel = 0u; channel < LIGHT_CHANNELS; channel++) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (uint32_t i = 0u; i < map->active_count; i++) {
                const uint32_t slot = map->borrowed_active[i];
                if (light_slot_is_lit(light, slot)) {
                    changed = light_rebuild_sweep(light, slot, channel) || changed;
                }
            }
        }
    }
}

/* ---- The end of every call ---------------------------------------------------------------------------------------- */

static world_light_status_t light_status(const world_light_t *light) {
    return light->stale != 0u ? WORLD_LIGHT_STATUS_NO_MEMORY : WORLD_LIGHT_STATUS_OK;
}

static world_light_status_t light_finish(world_light_t *light) {
    if (light->overflowed != 0u) {
        light->overflow_count++;
        light_rebuild_all(light);
    }
    section_pool_t *const pool = light->borrowed_map->borrowed_pool;
    for (uint32_t i = 0u; i < light->touched_count; i++) {
        const uint32_t slot = light->borrowed_touched_slots[i];
        const uint64_t mask = light->borrowed_touched[slot];
        light->borrowed_touched[slot] = 0u;
        column_t *const column = light_column(light, slot);
        for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
            if ((mask & (1ull << s)) != 0u) {
                section_compact(&column->sections[s], pool);
            }
        }
        world_edit_sections_mark(light->borrowed_edit, slot, mask, WORLD_EDIT_FOR_RENDER);
    }
    light->touched_count = 0u;
    /* After compacting, so that only what comes back later counts as room, and a full pool is not rebuilt every tick. */
    if (light->refused != 0u) {
        light->refused = 0u;
        light->failed_live_bytes = pool->live_bytes;
    }
    return light_status(light);
}

/* ---- The API ------------------------------------------------------------------------------------------------------ */

void world_light_init(world_light_t *light, arena_t *arena, world_edit_t *edit, uint64_t wave_capacity) {
    ASSERT_ALWAYS(wave_capacity != 0u && wave_capacity < LIGHT_NONE);
    memset(light, 0, sizeof(*light));
    world_map_t *const map = edit->borrowed_map;
    light->borrowed_edit = edit;
    light->borrowed_map = map;
    light->wave_capacity = wave_capacity;
    light->log_cursor = world_edit_log_position(edit);
    light->borrowed_lit = arena_alloc_zeroed(arena, (uint64_t)map->slot_count * sizeof(uint32_t), alignof(uint32_t));
    light->borrowed_touched =
        arena_alloc_zeroed(arena, (uint64_t)map->slot_count * sizeof(uint64_t), alignof(uint64_t));
    light->borrowed_touched_slots = arena_alloc_array(arena, map->slot_count, sizeof(uint32_t), alignof(uint32_t));
    light->borrowed_wave = arena_alloc_array(arena, wave_capacity, sizeof(uint64_t), alignof(uint64_t));
    light->borrowed_nodes = arena_alloc_array(arena, wave_capacity, sizeof(uint64_t), alignof(uint64_t));
    light->borrowed_node_next = arena_alloc_array(arena, wave_capacity, sizeof(uint32_t), alignof(uint32_t));
    light->borrowed_import = arena_alloc(arena, SECTION_VOXELS, alignof(uint8_t));
    light->borrowed_log =
        arena_alloc_array(arena, WORLD_LIGHT_RECORDS, sizeof(world_edit_record_t), alignof(world_edit_record_t));
    light_queues_clear(light);
}

void world_light_deinit(world_light_t *light) {
    memset(light, 0, sizeof(*light));
}

/* After a new column's sky has fallen: asks about every voxel that could be owed more, there and over the edge. */
static void light_column_added_seed(world_light_t *light, uint32_t slot, uint32_t channel) {
    const column_t *const column = light_column(light, slot);
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        if (!light_section_settled(light, slot, s, channel)) {
            for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                light_wave_push(light, slot, (s << LIGHT_SECTION_SHIFT) | i, 0u);
            }
        }
    }
    for (uint32_t direction = 0u; direction < LIGHT_DOWN; direction++) {
        uint32_t beside = 0u;
        if (!light_beside(light, slot, direction, &beside)) {
            continue;
        }
        /* The neighbour's face that looks at this column. */
        const uint32_t edge = (direction == LIGHT_WEST || direction == LIGHT_SOUTH) ? SECTION_SIZE - 1u : 0u;
        const column_t *const near = light_column(light, beside);
        for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
            const section_t *const ours = &column->sections[s];
            const section_t *const theirs = &near->sections[s];
            if (ours->borrowed_light == nullptr) {
                const uint32_t gives = light_channel(ours->light_uniform, channel);
                if (gives <= 1u || (theirs->borrowed_light == nullptr &&
                                    gives <= light_channel(theirs->light_uniform, channel) + 1u)) {
                    continue;
                }
            }
            for (uint32_t z = 0u; z < SECTION_SIZE; z++) {
                for (uint32_t along = 0u; along < SECTION_SIZE; along++) {
                    const uint32_t index = direction <= LIGHT_EAST ? section_voxel_index(edge, along, z)
                                                                   : section_voxel_index(along, edge, z);
                    light_wave_push(light, beside, (s << LIGHT_SECTION_SHIFT) | index, 0u);
                }
            }
        }
    }
}

world_light_status_t world_light_column_added(world_light_t *light, uint32_t slot) {
    const world_map_t *const map = light->borrowed_map;
    ASSERT_ALWAYS(slot < map->slot_count && map->borrowed_slots[slot].state == WORLD_MAP_STATE_READY);
    if (light_slot_is_lit(light, slot)) {
        return light_status(light);
    }
    light_column_fall(light, slot);
    light->borrowed_lit[slot] = map->borrowed_slots[slot].generation;
    for (uint32_t channel = 0u; channel < LIGHT_CHANNELS; channel++) {
        light_column_added_seed(light, slot, channel);
        light_pass(light, channel);
    }
    return light_finish(light);
}

/* The neighbour's voxels that hold exactly what the leaving column gave them go dark. */
static void light_column_leaving_seed(world_light_t *light, uint32_t slot, uint32_t direction, uint32_t beside,
                                      uint32_t channel) {
    const uint32_t edge = (direction == LIGHT_WEST || direction == LIGHT_SOUTH) ? 0u : SECTION_SIZE - 1u;
    const uint32_t edge_near = SECTION_SIZE - 1u - edge;
    const column_t *const column = light_column(light, slot);
    const column_t *const near = light_column(light, beside);
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        const section_t *const ours = &column->sections[s];
        const section_t *const theirs = &near->sections[s];
        if (ours->borrowed_light == nullptr && theirs->borrowed_light == nullptr) {
            const uint32_t gave = light_channel(ours->light_uniform, channel);
            const uint32_t held = light_channel(theirs->light_uniform, channel);
            if (gave == 0u || held == 0u || held >= gave) {
                continue; /* nothing can have come over this face: going sideways always costs */
            }
        }
        for (uint32_t z = 0u; z < SECTION_SIZE; z++) {
            for (uint32_t along = 0u; along < SECTION_SIZE; along++) {
                const uint32_t index_ours =
                    direction <= LIGHT_EAST ? section_voxel_index(edge, along, z) : section_voxel_index(along, edge, z);
                const uint32_t index_near = direction <= LIGHT_EAST ? section_voxel_index(edge_near, along, z)
                                                                    : section_voxel_index(along, edge_near, z);
                const uint32_t voxel = (s << LIGHT_SECTION_SHIFT) | index_near;
                const uint32_t held = light_channel(section_light_get(theirs, index_near), channel);
                if (held != 0u && held == light_into(light_channel(section_light_get(ours, index_ours), channel),
                                                     light_def_at(light, beside, voxel)->opacity, false)) {
                    if (light_write(light, beside, voxel, channel, 0u)) {
                        light_wave_push(light, beside, voxel, held);
                    }
                }
            }
        }
    }
}

world_light_status_t world_light_column_leaving(world_light_t *light, uint32_t slot) {
    ASSERT_ALWAYS(slot < light->borrowed_map->slot_count);
    if (!light_slot_is_lit(light, slot)) {
        return light_status(light);
    }
    /* Stops counting before anything else runs, so that what goes dark cannot lean on it again. */
    light->borrowed_lit[slot] = 0u;
    for (uint32_t channel = 0u; channel < LIGHT_CHANNELS; channel++) {
        for (uint32_t direction = 0u; direction < LIGHT_DOWN; direction++) {
            uint32_t beside = 0u;
            if (light_beside(light, slot, direction, &beside)) {
                light_column_leaving_seed(light, slot, direction, beside, channel);
            }
        }
        light_pass(light, channel);
    }
    return light_finish(light);
}

static void light_records_seed(world_light_t *light, uint64_t count, uint32_t channel) {
    for (uint64_t i = 0u; i < count; i++) {
        const world_edit_record_t *const record = &light->borrowed_log[i];
        uint32_t slot = 0u;
        if (light_lit_slot(light, column_chunk_of(record->x), column_chunk_of(record->y), &slot)) {
            const uint32_t index = section_voxel_index(column_local_of(record->x), column_local_of(record->y),
                                                       column_section_local_of(record->z));
            light_seed(light, slot, (column_section_of(record->z) << LIGHT_SECTION_SHIFT) | index, channel);
        }
    }
}

world_light_status_t world_light_update(world_light_t *light) {
    world_edit_t *const edit = light->borrowed_edit;
    /* Room has come back since a pool refused. A rebuild sees every record too, so those are only read past. */
    const bool retry = light->stale != 0u && light->borrowed_map->borrowed_pool->live_bytes < light->failed_live_bytes;
    bool lost_any = false;
    for (;;) {
        uint64_t lost = 0u;
        const uint64_t count =
            world_edit_log_read(edit, &light->log_cursor, light->borrowed_log, WORLD_LIGHT_RECORDS, &lost);
        lost_any = lost_any || lost != 0u;
        if (count == 0u) {
            break;
        }
        /* The same after a loss or an overflow. */
        if (!retry && !lost_any && light->overflowed == 0u) {
            for (uint32_t channel = 0u; channel < LIGHT_CHANNELS; channel++) {
                light_records_seed(light, count, channel);
                light_pass(light, channel);
            }
        }
    }
    /* Only what the log told of, or a new column's own: both are done with. */
    world_edit_dirty_t dirty;
    while (world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_LIGHT, &dirty)) {
    }
    if (lost_any) {
        light->lost_count++;
        light_rebuild_all(light);
    } else if (retry) {
        light_rebuild_all(light);
    }
    return light_finish(light);
}

world_light_status_t world_light_sections_replaced(world_light_t *light, uint32_t slot, uint64_t section_mask) {
    ASSERT_ALWAYS(slot < light->borrowed_map->slot_count && (section_mask & ~WORLD_EDIT_SECTIONS_ALL) == 0u);
    if (light_slot_is_lit(light, slot)) {
        for (uint32_t channel = 0u; channel < LIGHT_CHANNELS; channel++) {
            for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
                if ((section_mask & (1ull << s)) != 0u) {
                    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
                        light_seed(light, slot, (s << LIGHT_SECTION_SHIFT) | i, channel);
                    }
                }
            }
            light_pass(light, channel);
        }
    }
    return light_finish(light);
}

world_light_status_t world_light_rebuild(world_light_t *light) {
    light_rebuild_all(light);
    return light_finish(light);
}
