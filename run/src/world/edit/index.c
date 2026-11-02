/* See README.md. */
#include "world/edit/index.h"

#include "tables/blocks/index.h"

#include <string.h>

void world_edit_init(world_edit_t *edit, arena_t *arena, world_map_t *map, uint64_t log_capacity) {
    ASSERT_ALWAYS(log_capacity != 0u && (log_capacity & (log_capacity - 1u)) == 0u);
    memset(edit, 0, sizeof(*edit));
    edit->borrowed_map = map;
    edit->log_capacity = log_capacity;
    edit->borrowed_log =
        arena_alloc_array(arena, log_capacity, sizeof(world_edit_record_t), alignof(world_edit_record_t));
    edit->borrowed_columns = arena_alloc_zeroed(arena, (uint64_t)map->slot_count * sizeof(world_edit_column_t),
                                                alignof(world_edit_column_t));
    for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
        edit->borrowed_queues[i] = arena_alloc_array(arena, map->slot_count, sizeof(uint32_t), alignof(uint32_t));
    }
}

void world_edit_deinit(world_edit_t *edit) {
    memset(edit, 0, sizeof(*edit));
}

/* The map knows nothing of this module and a slot may have a new column by now: this is where that gets noticed, so that
 * each call below sees flags that belong to who is there. The queue bits stay, since the slot is still in those queues. */
static world_edit_column_t *edit_column(world_edit_t *edit, uint32_t slot) {
    ASSERT_ALWAYS(slot < edit->borrowed_map->slot_count);
    world_edit_column_t *const column = &edit->borrowed_columns[slot];
    const uint32_t generation = edit->borrowed_map->borrowed_slots[slot].generation;
    if (column->generation != generation) {
        memset(column->dirty, 0, sizeof(column->dirty));
        column->revision = edit->revision;
        column->generation = generation;
        column->top_z = WORLD_EDIT_TOP_Z_NONE;
    }
    return column;
}

static bool edit_slot_is_ready(const world_edit_t *edit, uint32_t slot) {
    ASSERT_ALWAYS(slot < edit->borrowed_map->slot_count);
    return edit->borrowed_map->borrowed_slots[slot].state == WORLD_MAP_STATE_READY;
}

/* A slot is queued at most once, so a queue as long as the table cannot overflow, however columns come and go. */
static void edit_queue_push(world_edit_t *edit, uint32_t slot, world_edit_column_t *column, uint32_t consumer) {
    const uint8_t bit = (uint8_t)(1u << consumer);
    if ((column->queued & bit) != 0u) {
        return;
    }
    const uint32_t capacity = edit->borrowed_map->slot_count;
    uint32_t place = edit->queue_head[consumer] + edit->queue_count[consumer];
    place = place >= capacity ? place - capacity : place;
    edit->borrowed_queues[consumer][place] = slot;
    edit->queue_count[consumer]++;
    column->queued |= bit;
}

static void edit_flag(world_edit_t *edit, uint32_t slot, world_edit_column_t *column, uint64_t section_mask,
                      uint32_t consumers) {
    for (uint32_t i = 0; i < WORLD_EDIT_CONSUMER_COUNT; i++) {
        if ((consumers & (1u << i)) != 0u) {
            column->dirty[i] |= section_mask;
            edit_queue_push(edit, slot, column, i);
        }
    }
}

static bool edit_section_has_blocks(const section_t *section) {
    return section->bits != SECTION_BITS_UNIFORM || section->block_uniform != BLOCK_ID_AIR;
}

static int16_t edit_section_top_z(uint32_t section_index) {
    return (int16_t)(COLUMN_Z_STORAGE_MIN + (int32_t)((section_index + 1u) * SECTION_SIZE) - 1);
}

static int16_t edit_top_scan(const column_t *live, uint32_t from_section_count) {
    for (uint32_t i = from_section_count; i > 0u; i--) {
        if (edit_section_has_blocks(&live->sections[i - 1u])) {
            return edit_section_top_z(i - 1u);
        }
    }
    return WORLD_EDIT_TOP_Z_NONE;
}

/* After one section changed. Looks further only if that was the top one, and it has just emptied. */
static void edit_top_update(world_edit_column_t *column, const column_t *live, uint32_t section_index) {
    const int16_t top = edit_section_top_z(section_index);
    if (edit_section_has_blocks(&live->sections[section_index])) {
        column->top_z = top > column->top_z ? top : column->top_z;
    } else if (top == column->top_z) {
        column->top_z = edit_top_scan(live, section_index);
    }
}

world_edit_status_t world_edit_block_set(world_edit_t *edit, int32_t x, int32_t y, int32_t z, uint16_t state) {
    uint16_t before = 0;
    const world_map_t *const map = edit->borrowed_map;
    const world_map_status_t status = world_map_block_get(map, x, y, z, &before);
    if (status == WORLD_MAP_STATUS_Z_OUT_OF_RANGE) {
        return WORLD_EDIT_STATUS_Z_OUT_OF_RANGE;
    }
    if (status == WORLD_MAP_STATUS_NOT_LOADED) {
        return WORLD_EDIT_STATUS_NOT_LOADED;
    }
    if (before == state) {
        return WORLD_EDIT_STATUS_UNCHANGED;
    }
    const int32_t chunk_x = column_chunk_of(x);
    const int32_t chunk_y = column_chunk_of(y);
    column_t *const live = world_map_column_at(map, chunk_x, chunk_y);
    if (column_block_set(live, map->borrowed_pool, column_local_of(x), column_local_of(y), z, state) !=
        COLUMN_STATUS_OK) {
        return WORLD_EDIT_STATUS_NO_MEMORY;
    }
    uint32_t slot = 0;
    const bool in_window = world_map_slot_of(map, chunk_x, chunk_y, &slot);
    ASSERT_ALWAYS(in_window);
    const uint32_t section_index = column_section_of(z);
    world_edit_column_t *const column = edit_column(edit, slot);
    edit_flag(edit, slot, column, 1ull << section_index, WORLD_EDIT_FOR_ALL);
    edit_top_update(column, live, section_index);
    edit->revision++;
    column->revision = edit->revision;
    edit->borrowed_log[edit->log_next & (edit->log_capacity - 1u)] = (world_edit_record_t){x, y, z, before, state};
    edit->log_next++;
    return WORLD_EDIT_STATUS_CHANGED;
}

world_edit_status_t world_edit_light_set(world_edit_t *edit, int32_t x, int32_t y, int32_t z, uint8_t packed) {
    uint8_t before = 0;
    const world_map_t *const map = edit->borrowed_map;
    const world_map_status_t status = world_map_light_get(map, x, y, z, &before);
    if (status == WORLD_MAP_STATUS_Z_OUT_OF_RANGE) {
        return WORLD_EDIT_STATUS_Z_OUT_OF_RANGE;
    }
    if (status == WORLD_MAP_STATUS_NOT_LOADED) {
        return WORLD_EDIT_STATUS_NOT_LOADED;
    }
    if (before == packed) {
        return WORLD_EDIT_STATUS_UNCHANGED;
    }
    const int32_t chunk_x = column_chunk_of(x);
    const int32_t chunk_y = column_chunk_of(y);
    column_t *const live = world_map_column_at(map, chunk_x, chunk_y);
    if (column_light_set(live, map->borrowed_pool, column_local_of(x), column_local_of(y), z, packed) !=
        COLUMN_STATUS_OK) {
        return WORLD_EDIT_STATUS_NO_MEMORY;
    }
    uint32_t slot = 0;
    const bool in_window = world_map_slot_of(map, chunk_x, chunk_y, &slot);
    ASSERT_ALWAYS(in_window);
    world_edit_column_t *const column = edit_column(edit, slot);
    edit_flag(edit, slot, column, 1ull << column_section_of(z), WORLD_EDIT_FOR_RENDER);
    edit->revision++;
    column->revision = edit->revision;
    return WORLD_EDIT_STATUS_CHANGED;
}

void world_edit_sections_mark(world_edit_t *edit, uint32_t slot, uint64_t section_mask, uint32_t consumers) {
    ASSERT_ALWAYS(edit_slot_is_ready(edit, slot));
    ASSERT_ALWAYS((section_mask & ~WORLD_EDIT_SECTIONS_ALL) == 0u);
    if (section_mask == 0u) {
        return;
    }
    world_edit_column_t *const column = edit_column(edit, slot);
    edit_flag(edit, slot, column, section_mask, consumers);
    column->top_z = edit_top_scan(&edit->borrowed_map->borrowed_columns[slot], COLUMN_SECTIONS);
    edit->revision++;
    column->revision = edit->revision;
}

void world_edit_column_filled(world_edit_t *edit, uint32_t slot) {
    ASSERT_ALWAYS(edit_slot_is_ready(edit, slot));
    const column_t *const live = &edit->borrowed_map->borrowed_columns[slot];
    uint64_t mask = 0;
    for (uint32_t i = 0; i < COLUMN_SECTIONS; i++) {
        const section_t *const section = &live->sections[i];
        if (edit_section_has_blocks(section) || section->borrowed_light != nullptr ||
            section->light_uniform != SECTION_LIGHT_OPEN_SKY) {
            mask |= 1ull << i;
        }
    }
    world_edit_sections_mark(edit, slot, mask, WORLD_EDIT_FOR_RENDER | WORLD_EDIT_FOR_LIGHT);
}

uint64_t world_edit_log_read(const world_edit_t *edit, uint64_t *cursor, world_edit_record_t *out, uint64_t max,
                             uint64_t *out_lost) {
    ASSERT_ALWAYS(*cursor <= edit->log_next);
    const uint64_t oldest = edit->log_next > edit->log_capacity ? edit->log_next - edit->log_capacity : 0u;
    *out_lost = 0u;
    if (*cursor < oldest) {
        *out_lost = oldest - *cursor;
        *cursor = oldest;
    }
    const uint64_t waiting = edit->log_next - *cursor;
    const uint64_t count = waiting < max ? waiting : max;
    for (uint64_t i = 0; i < count; i++) {
        out[i] = edit->borrowed_log[(*cursor + i) & (edit->log_capacity - 1u)];
    }
    *cursor += count;
    return count;
}

bool world_edit_dirty_next(world_edit_t *edit, world_edit_consumer_t consumer, world_edit_dirty_t *out) {
    const uint32_t capacity = edit->borrowed_map->slot_count;
    while (edit->queue_count[consumer] != 0u) {
        const uint32_t slot = edit->borrowed_queues[consumer][edit->queue_head[consumer]];
        edit->queue_head[consumer] = edit->queue_head[consumer] + 1u == capacity ? 0u : edit->queue_head[consumer] + 1u;
        edit->queue_count[consumer]--;
        world_edit_column_t *const column = edit_column(edit, slot);
        column->queued &= (uint8_t)~(1u << consumer);
        const uint64_t mask = column->dirty[consumer];
        column->dirty[consumer] = 0u;
        const world_map_slot_t *const entry = &edit->borrowed_map->borrowed_slots[slot];
        if (mask == 0u || entry->state != WORLD_MAP_STATE_READY) {
            continue; /* cleared by hand, or the column it was for has gone */
        }
        *out = (world_edit_dirty_t){mask, entry->chunk_x, entry->chunk_y, slot, entry->generation};
        return true;
    }
    return false;
}

void world_edit_dirty_restore(world_edit_t *edit, const world_edit_dirty_t *dirty, world_edit_consumer_t consumer) {
    ASSERT_ALWAYS(dirty->slot < edit->borrowed_map->slot_count);
    const world_map_slot_t *const entry = &edit->borrowed_map->borrowed_slots[dirty->slot];
    if (entry->state != WORLD_MAP_STATE_READY || entry->generation != dirty->generation) {
        return;
    }
    world_edit_column_t *const column = edit_column(edit, dirty->slot);
    edit_flag(edit, dirty->slot, column, dirty->section_mask, 1u << consumer);
}

uint64_t world_edit_dirty_peek(world_edit_t *edit, uint32_t slot, world_edit_consumer_t consumer) {
    if (!edit_slot_is_ready(edit, slot)) {
        return 0u;
    }
    return edit_column(edit, slot)->dirty[consumer];
}

void world_edit_dirty_clear(world_edit_t *edit, uint32_t slot, world_edit_consumer_t consumer, uint64_t section_mask) {
    if (edit_slot_is_ready(edit, slot)) {
        edit_column(edit, slot)->dirty[consumer] &= ~section_mask;
    }
}

uint64_t world_edit_column_revision(world_edit_t *edit, uint32_t slot) {
    ASSERT_ALWAYS(edit_slot_is_ready(edit, slot));
    return edit_column(edit, slot)->revision;
}

int16_t world_edit_column_top_z(world_edit_t *edit, uint32_t slot) {
    ASSERT_ALWAYS(edit_slot_is_ready(edit, slot));
    return edit_column(edit, slot)->top_z;
}

/* ---- Snapshots ---------------------------------------------------------------------------------------------------- */

enum : uint32_t {
    SNAPSHOT_EXCHANGE_INDEX_MASK = 3u,
    SNAPSHOT_EXCHANGE_UNREAD = 4u,
};
static_assert(WORLD_EDIT_SNAPSHOT_VIEWS - 1u <= SNAPSHOT_EXCHANGE_INDEX_MASK);

void world_edit_snapshot_init(world_edit_snapshot_t *snapshot, arena_t *arena, world_edit_t *edit,
                              uint32_t section_capacity) {
    ASSERT_ALWAYS(section_capacity >= 1u);
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->borrowed_edit = edit;
    snapshot->section_capacity = section_capacity;
    snapshot->back = 0u;
    atomic_init(&snapshot->exchange, 1u);
    snapshot->front = 2u;
    /* No first publish could match this, so the reader always gets a first view, and learns the window from it. */
    snapshot->last_map_change = UINT64_MAX;
    const uint32_t slot_count = edit->borrowed_map->slot_count;
    for (uint32_t i = 0; i < WORLD_EDIT_SNAPSHOT_VIEWS; i++) {
        world_edit_snapshot_view_t *const view = &snapshot->views[i];
        view->borrowed_columns = arena_alloc_array(arena, slot_count, sizeof(world_edit_snapshot_column_t),
                                                   alignof(world_edit_snapshot_column_t));
        view->borrowed_sections = arena_alloc_array(arena, section_capacity, sizeof(world_edit_snapshot_section_t),
                                                    alignof(world_edit_snapshot_section_t));
        view->borrowed_voxels = arena_alloc_array(arena, section_capacity, sizeof(world_edit_snapshot_voxels_t),
                                                  alignof(world_edit_snapshot_voxels_t));
    }
}

void world_edit_snapshot_deinit(world_edit_snapshot_t *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
}

/* Starts from nothing, not from what this view held two publishes ago, and touches only columns that exist: so it costs one
 * clear of the table, and one look at each column. */
static void snapshot_table_fill(world_edit_t *edit, world_edit_snapshot_view_t *view) {
    const world_map_t *const map = edit->borrowed_map;
    world_edit_snapshot_column_t *const table = (world_edit_snapshot_column_t *)(uintptr_t)view->borrowed_columns;
    memset(table, 0, (uint64_t)map->slot_count * sizeof(*table));
    for (uint32_t i = 0; i < map->active_count; i++) {
        const uint32_t slot = map->borrowed_active[i];
        const world_map_slot_t *const entry = &map->borrowed_slots[slot];
        if (entry->state != WORLD_MAP_STATE_READY) {
            continue;
        }
        const world_edit_column_t *const column = edit_column(edit, slot);
        table[slot] = (world_edit_snapshot_column_t){
            .revision = column->revision,
            .chunk_x = entry->chunk_x,
            .chunk_y = entry->chunk_y,
            .generation = entry->generation,
            .top_z = column->top_z,
            .state = WORLD_MAP_STATE_READY,
        };
    }
}

static void snapshot_section_copy(world_edit_snapshot_view_t *view, const world_edit_dirty_t *dirty,
                                  const section_t *section, uint32_t section_index) {
    const uint32_t place = view->section_count;
    world_edit_snapshot_section_t *const entry =
        (world_edit_snapshot_section_t *)(uintptr_t)&view->borrowed_sections[place];
    world_edit_snapshot_voxels_t *const voxels =
        (world_edit_snapshot_voxels_t *)(uintptr_t)&view->borrowed_voxels[place];
    const bool has_blocks = section->bits != SECTION_BITS_UNIFORM;
    const bool has_light = section->borrowed_light != nullptr;
    *entry = (world_edit_snapshot_section_t){
        .chunk_x = dirty->chunk_x,
        .chunk_y = dirty->chunk_y,
        .slot = dirty->slot,
        .generation = dirty->generation,
        .block_uniform = has_blocks ? 0u : section->block_uniform,
        .section_index = (uint8_t)section_index,
        .light_uniform = has_light ? 0u : section->light_uniform,
        .has_blocks = has_blocks ? 1u : 0u,
        .has_light = has_light ? 1u : 0u,
    };
    if (has_blocks) {
        section_blocks_export(section, voxels->blocks);
    }
    if (has_light) {
        section_lights_export(section, voxels->light);
    }
    view->section_count = place + 1u;
}

/* Takes whole columns from the queue. One that does not fit gives the rest of its mask back, and so waits behind the others:
 * a column with many dirty sections cannot hold up the rest. */
static void snapshot_sections_fill(world_edit_snapshot_t *snapshot, world_edit_snapshot_view_t *view) {
    world_edit_t *const edit = snapshot->borrowed_edit;
    const column_t *const columns = edit->borrowed_map->borrowed_columns;
    view->section_count = 0u;
    world_edit_dirty_t dirty;
    while (view->section_count < snapshot->section_capacity &&
           world_edit_dirty_next(edit, WORLD_EDIT_CONSUMER_RENDER, &dirty)) {
        uint64_t remaining = dirty.section_mask;
        for (uint32_t i = 0; i < COLUMN_SECTIONS && remaining != 0u; i++) {
            const uint64_t bit = 1ull << i;
            if ((remaining & bit) == 0u) {
                continue;
            }
            if (view->section_count == snapshot->section_capacity) {
                break;
            }
            snapshot_section_copy(view, &dirty, &columns[dirty.slot].sections[i], i);
            remaining &= ~bit;
        }
        if (remaining != 0u) {
            dirty.section_mask = remaining;
            world_edit_dirty_restore(edit, &dirty, WORLD_EDIT_CONSUMER_RENDER);
        }
    }
}

/* What was in a view nobody read goes back to being dirty. A section of a column that has since gone stays gone. */
static void snapshot_sections_return(world_edit_t *edit, const world_edit_snapshot_view_t *view) {
    for (uint32_t i = 0; i < view->section_count; i++) {
        const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[i];
        const world_edit_dirty_t dirty = {
            .section_mask = 1ull << entry->section_index,
            .chunk_x = entry->chunk_x,
            .chunk_y = entry->chunk_y,
            .slot = entry->slot,
            .generation = entry->generation,
        };
        world_edit_dirty_restore(edit, &dirty, WORLD_EDIT_CONSUMER_RENDER);
    }
}

bool world_edit_snapshot_publish(world_edit_snapshot_t *snapshot) {
    world_edit_t *const edit = snapshot->borrowed_edit;
    const world_map_t *const map = edit->borrowed_map;
    if (edit->revision == snapshot->last_revision && map->change_count == snapshot->last_map_change &&
        edit->queue_count[WORLD_EDIT_CONSUMER_RENDER] == 0u) {
        return false;
    }
    world_edit_snapshot_view_t *const view = &snapshot->views[snapshot->back];
    snapshot->published_count++;
    view->sequence = snapshot->published_count;
    view->revision = edit->revision;
    view->center_x = map->center_x;
    view->center_y = map->center_y;
    view->min_x = map->min_x;
    view->min_y = map->min_y;
    view->min_slot_x = map->min_slot_x;
    view->min_slot_y = map->min_slot_y;
    view->side = map->side;
    view->slot_count = map->slot_count;
    view->view_radius = map->view_radius;
    view->ready_count = map->ready_count;
    snapshot_table_fill(edit, view);
    snapshot_sections_fill(snapshot, view);
    snapshot->last_revision = edit->revision;
    snapshot->last_map_change = map->change_count;

    /* Release gives the reader everything written above. Acquire is for the view that comes back, which the reader may have
     * just been reading. */
    const uint32_t previous =
        atomic_exchange_explicit(&snapshot->exchange, snapshot->back | SNAPSHOT_EXCHANGE_UNREAD, memory_order_acq_rel);
    snapshot->back = previous & SNAPSHOT_EXCHANGE_INDEX_MASK;
    if ((previous & SNAPSHOT_EXCHANGE_UNREAD) != 0u) {
        snapshot->overwritten_count++;
        snapshot_sections_return(edit, &snapshot->views[snapshot->back]);
    }
    return true;
}

const world_edit_snapshot_view_t *world_edit_snapshot_acquire(world_edit_snapshot_t *snapshot) {
    /* Relaxed: this only looks, and takes nothing until the exchange, whose acquire pairs with the publisher's release. */
    if ((atomic_load_explicit(&snapshot->exchange, memory_order_relaxed) & SNAPSHOT_EXCHANGE_UNREAD) == 0u) {
        return nullptr;
    }
    const uint32_t previous = atomic_exchange_explicit(&snapshot->exchange, snapshot->front, memory_order_acq_rel);
    snapshot->front = previous & SNAPSHOT_EXCHANGE_INDEX_MASK;
    snapshot->acquired_count++;
    return &snapshot->views[snapshot->front];
}
