/* See README.md. The octant store: each level's sections reduced to their eighth of the level above, kept where the level above is made. */
#include "render/octant/index.h"

#include "assert/index.h"
#include "world/section/index.h"

#include <string.h>

static_assert((uint32_t)WORLD_BUFFERS_COLUMN_SECTIONS % (1u << RENDER_OCTANT_LEVELS_MAX) == 0u);

static uint64_t octant_table_bytes(uint32_t slots, uint32_t sections) {
    return (uint64_t)slots * (sizeof(render_octant_column_t) + (uint64_t)sections * sizeof(uint32_t)) +
           2u * MC_CPU_CACHE_LINE_BYTES;
}

static uint32_t octant_level_slots(uint32_t level, uint32_t map_slots) {
    return level == 0u ? map_slots : RENDER_OCTANT_LEVEL_SIDE_MAX * RENDER_OCTANT_LEVEL_SIDE_MAX;
}

uint64_t render_octant_arena_bytes(uint32_t map_slots) {
    uint64_t bytes = (uint64_t)RENDER_OCTANT_BLOCK_CAPACITY * sizeof(render_encode_octant_t) +
                     2u * (uint64_t)map_slots * 2u * sizeof(int32_t) + 2u * MC_CPU_CACHE_LINE_BYTES;
    for (uint32_t level = 0u; level < RENDER_OCTANT_LEVELS_MAX; level++) {
        bytes += octant_table_bytes(octant_level_slots(level, map_slots), WORLD_BUFFERS_COLUMN_SECTIONS >> level);
    }
    return bytes;
}

void render_octant_init(render_octant_t *store, arena_t *arena, uint32_t map_slots) {
    memset(store, 0, sizeof(*store));
    for (uint32_t level = 0u; level < RENDER_OCTANT_LEVELS_MAX; level++) {
        render_octant_table_t *const table = &store->tables[level];
        table->slot_capacity = octant_level_slots(level, map_slots);
        table->sections = WORLD_BUFFERS_COLUMN_SECTIONS >> level;
        table->borrowed_columns = arena_alloc_array(arena, table->slot_capacity, sizeof(render_octant_column_t),
                                                    alignof(render_octant_column_t));
        memset(table->borrowed_columns, 0, (uint64_t)table->slot_capacity * sizeof(render_octant_column_t));
        /* Handles are read only once their column is valid, which sets them all: their pages are touched as columns come. */
        table->borrowed_handles = arena_alloc_array(arena, (uint64_t)table->slot_capacity * table->sections,
                                                    sizeof(uint32_t), MC_CPU_CACHE_LINE_BYTES);
    }
    /* Blocks are handed out from the front, so pages past what has been used are never touched. */
    store->block_capacity = RENDER_OCTANT_BLOCK_CAPACITY;
    store->borrowed_blocks =
        arena_alloc_array(arena, store->block_capacity, sizeof(render_encode_octant_t), MC_CPU_CACHE_LINE_BYTES);
    /* A slot can change twice between two looks, gone and come. */
    store->changed_capacity = 2u * map_slots;
    store->borrowed_changed =
        arena_alloc_array(arena, (uint64_t)store->changed_capacity * 2u, sizeof(int32_t), alignof(int32_t));
}

void render_octant_deinit(render_octant_t *store) {
    memset(store, 0, sizeof(*store));
}

uint64_t render_octant_bytes_used(const render_octant_t *store) {
    uint64_t bytes = (uint64_t)store->blocks_live * sizeof(render_encode_octant_t);
    for (uint32_t level = 0u; level < RENDER_OCTANT_LEVELS_MAX; level++) {
        const render_octant_table_t *const table = &store->tables[level];
        bytes += (uint64_t)table->slot_capacity * (sizeof(render_octant_column_t) + table->sections * sizeof(uint32_t));
    }
    return bytes;
}

/* ---- Blocks ------------------------------------------------------------------------------------------------------------ */

static uint32_t octant_block_take(render_octant_t *store) {
    uint32_t index = 0u;
    if (store->block_free != 0u) {
        index = store->block_free - 1u;
        uint32_t next = 0u;
        memcpy(&next, store->borrowed_blocks[index].material, sizeof(next));
        store->block_free = next;
    } else if (store->block_carved < store->block_capacity) {
        index = store->block_carved++;
    } else {
        return UINT32_MAX;
    }
    store->blocks_live++;
    return index;
}

static void octant_handle_free(render_octant_t *store, uint32_t handle) {
    if (handle == RENDER_OCTANT_AIR || (handle & RENDER_OCTANT_UNIFORM) != 0u) {
        return;
    }
    const uint32_t index = handle - 1u;
    memcpy(store->borrowed_blocks[index].material, &store->block_free, sizeof(store->block_free));
    store->block_free = index + 1u;
    store->blocks_live--;
}

/* Every octant of a column back to air, its blocks given back. */
static void octant_column_empty(render_octant_t *store, render_octant_table_t *table, uint32_t slot) {
    uint32_t *const handles = &table->borrowed_handles[(uint64_t)slot * table->sections];
    for (uint32_t section = 0u; section < table->sections; section++) {
        octant_handle_free(store, handles[section]);
        handles[section] = RENDER_OCTANT_AIR;
    }
}

/* ---- Level 0 ---------------------------------------------------------------------------------------------------------- */

static void octant_changed_push(render_octant_t *store, int32_t chunk_x, int32_t chunk_y) {
    if (store->changed_count == store->changed_capacity) {
        if (store->changed_over == 0u) {
            store->stats.changed_lost++;
        }
        store->changed_over = 1u;
        return;
    }
    store->borrowed_changed[store->changed_count * 2u] = chunk_x;
    store->borrowed_changed[store->changed_count * 2u + 1u] = chunk_y;
    store->changed_count++;
}

void render_octant_column_set(render_octant_t *store, uint32_t slot, int32_t chunk_x, int32_t chunk_y,
                              uint32_t generation) {
    render_octant_table_t *const table = &store->tables[0];
    ASSERT_ALWAYS(slot < table->slot_capacity);
    render_octant_column_t *const column = &table->borrowed_columns[slot];
    if (column->valid != 0u && column->chunk_x == chunk_x && column->chunk_y == chunk_y &&
        column->generation == generation) {
        return;
    }
    if (column->valid != 0u) {
        octant_column_empty(store, table, slot);
        octant_changed_push(store, column->chunk_x, column->chunk_y);
    } else {
        /* Never set: its handles are whatever the arena held. */
        memset(&table->borrowed_handles[(uint64_t)slot * table->sections], 0, table->sections * sizeof(uint32_t));
    }
    *column = (render_octant_column_t){
        .chunk_x = chunk_x, .chunk_y = chunk_y, .generation = generation, .valid = 1u, .complete = 1u, ._pad0 = {0}};
    octant_changed_push(store, chunk_x, chunk_y);
}

void render_octant_column_clear(render_octant_t *store, uint32_t slot) {
    render_octant_table_t *const table = &store->tables[0];
    render_octant_column_t *const column = &table->borrowed_columns[slot];
    if (column->valid == 0u) {
        return;
    }
    octant_column_empty(store, table, slot);
    octant_changed_push(store, column->chunk_x, column->chunk_y);
    column->valid = 0u;
}

void render_octant_section_set(render_octant_t *store, uint32_t level, uint32_t slot, uint32_t section,
                               const render_encode_octant_t *octant) {
    render_octant_table_t *const table = &store->tables[level];
    render_octant_column_t *const column = &table->borrowed_columns[slot];
    ASSERT_ALWAYS(column->valid != 0u && section < table->sections);
    uint32_t *const handle = &table->borrowed_handles[(uint64_t)slot * table->sections + section];
    octant_handle_free(store, *handle);
    *handle = RENDER_OCTANT_AIR;
    bool uniform = true;
    for (uint32_t i = 1u; i < RENDER_ENCODE_OCTANT_VOXELS && uniform; i++) {
        uniform = octant->material[i] == octant->material[0] && octant->light[i] == octant->light[0];
    }
    if (uniform) {
        *handle = octant->material[0] == 0u
                      ? RENDER_OCTANT_AIR
                      : RENDER_OCTANT_UNIFORM | ((uint32_t)octant->light[0] << 16) | octant->material[0];
    } else {
        const uint32_t index = octant_block_take(store);
        if (index == UINT32_MAX) {
            /* Air in its place, and the level above is made from terrain here until the column loads again. */
            column->complete = 0u;
            store->stats.octants_refused++;
        } else {
            memcpy(&store->borrowed_blocks[index], octant, sizeof(*octant));
            *handle = index + 1u;
        }
    }
    store->stats.octants_stored++;
    if (level == 0u) {
        octant_changed_push(store, column->chunk_x, column->chunk_y);
    }
}

bool render_octant_changed_next(render_octant_t *store, int32_t *out_chunk_x, int32_t *out_chunk_y) {
    if (store->changed_count == 0u) {
        return false;
    }
    /* Newest first is as good as oldest: each is only marked. */
    store->changed_count--;
    *out_chunk_x = store->borrowed_changed[store->changed_count * 2u];
    *out_chunk_y = store->borrowed_changed[store->changed_count * 2u + 1u];
    return true;
}

bool render_octant_changed_lost_take(render_octant_t *store) {
    const bool lost = store->changed_over != 0u;
    store->changed_over = 0u;
    return lost;
}

/* ---- The levels above ---------------------------------------------------------------------------------------------------- */

void render_octant_levels_forget(render_octant_t *store) {
    for (uint32_t level = 1u; level < RENDER_OCTANT_LEVELS_MAX; level++) {
        render_octant_table_t *const table = &store->tables[level];
        for (uint32_t slot = 0u; slot < table->slot_capacity; slot++) {
            if (table->borrowed_columns[slot].valid != 0u) {
                octant_column_empty(store, table, slot);
                table->borrowed_columns[slot].valid = 0u;
            }
        }
    }
}

bool render_octant_column_ready(const render_octant_t *store, uint32_t level, const world_buffers_t *buffers,
                                int32_t column_x, int32_t column_y, uint32_t out_slots[4]) {
    if (level >= RENDER_OCTANT_LEVELS_MAX || buffers == nullptr || buffers->borrowed_shadow == nullptr) {
        return false;
    }
    const render_octant_table_t *const table = &store->tables[level];
    for (uint32_t i = 0u; i < 4u; i++) {
        const int32_t x = column_x * 2 + (int32_t)(i & 1u), y = column_y * 2 + (int32_t)(i >> 1);
        uint32_t slot = 0u;
        if (!world_buffers_slot_of(&buffers->window, x, y, &slot) || slot >= table->slot_capacity) {
            return false;
        }
        const world_buffers_column_t *const there = &buffers->borrowed_shadow[slot];
        const render_octant_column_t *const own = &table->borrowed_columns[slot];
        if (there->ready == 0u || there->chunk_x != x || there->chunk_y != y || own->valid == 0u ||
            own->complete == 0u || own->chunk_x != x || own->chunk_y != y || own->generation != there->generation) {
            return false;
        }
        out_slots[i] = slot;
    }
    return true;
}

/* One octant into its eighth of an assembled section. */
static void octant_place(const render_octant_t *store, uint32_t handle, uint32_t ox, uint32_t oy, uint32_t oz,
                         uint16_t *cells, uint8_t *light) {
    const render_encode_octant_t *const block = handle != RENDER_OCTANT_AIR && (handle & RENDER_OCTANT_UNIFORM) == 0u
                                                    ? &store->borrowed_blocks[handle - 1u]
                                                    : nullptr;
    const uint16_t material = (uint16_t)(handle & 0xFFFFu);
    const uint8_t packed = (uint8_t)((handle >> 16) & 0xFFu);
    for (uint32_t z = 0u; z < RENDER_ENCODE_OCTANT_SIDE; z++) {
        for (uint32_t y = 0u; y < RENDER_ENCODE_OCTANT_SIDE; y++) {
            const uint32_t row = section_voxel_index(ox, oy + y, oz + z);
            const uint32_t from = RENDER_ENCODE_OCTANT_SIDE * (y + RENDER_ENCODE_OCTANT_SIDE * z);
            if (block != nullptr) {
                memcpy(&cells[row], &block->material[from], RENDER_ENCODE_OCTANT_SIDE * sizeof(uint16_t));
                memcpy(&light[row], &block->light[from], RENDER_ENCODE_OCTANT_SIDE);
            } else {
                for (uint32_t x = 0u; x < RENDER_ENCODE_OCTANT_SIDE; x++) {
                    cells[row + x] = handle == RENDER_OCTANT_AIR ? (uint16_t)0u : material;
                    light[row + x] = handle == RENDER_OCTANT_AIR ? (uint8_t)0u : packed;
                }
            }
        }
    }
}

int32_t render_octant_column_assemble(const render_octant_t *store, uint32_t level, const uint32_t slots[4],
                                      uint16_t *cells, uint8_t *light, render_octant_section_t *sections) {
    const render_octant_table_t *const table = &store->tables[level];
    const uint32_t below = table->sections;
    const uint32_t count = below / 2u;
    /* octant_place copies a row of 8 along x at once. */
    ASSERT(section_voxel_index(1u, 0u, 0u) == 1u);
    int32_t top = -1;
    for (uint32_t section = 0u; section < count; section++) {
        uint32_t handles[8];
        for (uint32_t i = 0u; i < 8u; i++) {
            handles[i] = table->borrowed_handles[(uint64_t)slots[i & 3u] * below + 2u * section + (i >> 2)];
        }
        bool same = handles[0] == RENDER_OCTANT_AIR || (handles[0] & RENDER_OCTANT_UNIFORM) != 0u;
        for (uint32_t i = 1u; i < 8u && same; i++) {
            same = handles[i] == handles[0];
        }
        render_octant_section_t *const out = &sections[section];
        if (same) {
            /* All air, or the one block and light all through: nothing to put in the cells. */
            out->block_uniform = (uint16_t)(handles[0] & 0xFFFFu);
            out->light_uniform = (uint8_t)((handles[0] >> 16) & 0xFFu);
            out->assembled = 0u;
            if (handles[0] != RENDER_OCTANT_AIR) {
                top = (int32_t)(section * SECTION_SIZE + SECTION_SIZE - 1u);
            }
            continue;
        }
        uint16_t *const section_cells = cells + (uint64_t)section * SECTION_VOXELS;
        uint8_t *const section_light = light + (uint64_t)section * SECTION_VOXELS;
        for (uint32_t i = 0u; i < 8u; i++) {
            octant_place(store, handles[i], (i & 1u) * RENDER_ENCODE_OCTANT_SIDE,
                         ((i >> 1) & 1u) * RENDER_ENCODE_OCTANT_SIDE, (i >> 2) * RENDER_ENCODE_OCTANT_SIDE,
                         section_cells, section_light);
        }
        *out = (render_octant_section_t){.block_uniform = 0u, .light_uniform = 0u, .assembled = 1u};
        for (uint32_t index = SECTION_VOXELS; index-- > 0u;) {
            if (section_cells[index] != 0u) {
                top = (int32_t)(section * SECTION_SIZE + (index >> 8));
                break;
            }
        }
    }
    return top;
}

void render_octant_level_column_set(render_octant_t *store, uint32_t level, uint32_t slot, int32_t column_x,
                                    int32_t column_y, uint32_t generation) {
    ASSERT_ALWAYS(level > 0u && level < RENDER_OCTANT_LEVELS_MAX);
    render_octant_table_t *const table = &store->tables[level];
    ASSERT_ALWAYS(slot < table->slot_capacity);
    render_octant_column_t *const column = &table->borrowed_columns[slot];
    if (column->valid != 0u) {
        octant_column_empty(store, table, slot);
    } else {
        memset(&table->borrowed_handles[(uint64_t)slot * table->sections], 0, table->sections * sizeof(uint32_t));
    }
    *column = (render_octant_column_t){
        .chunk_x = column_x, .chunk_y = column_y, .generation = generation, .valid = 1u, .complete = 1u, ._pad0 = {0}};
}

/* ---- The watch --------------------------------------------------------------------------------------------------------- */

void render_octant_watch_arm(render_octant_t *store, int32_t x, int32_t y, int32_t z) {
    store->watch.x = x;
    store->watch.y = y;
    store->watch.z = z;
    for (uint32_t i = 0u; i <= RENDER_OCTANT_LEVELS_MAX; i++) {
        atomic_store_explicit(&store->watch.seen_ns[i], 0, memory_order_relaxed);
        atomic_store_explicit(&store->watch.material[i], 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&store->watch.armed, 1u, memory_order_release);
}

void render_octant_watch_note(render_octant_t *store, uint32_t shift, uint16_t material, int64_t now_ns) {
    if (shift > RENDER_OCTANT_LEVELS_MAX) {
        return;
    }
    atomic_store_explicit(&store->watch.material[shift], material, memory_order_relaxed);
    /* Release: whoever sees the time sees the block. */
    atomic_store_explicit(&store->watch.seen_ns[shift], now_ns, memory_order_release);
}
