/* See README.md. */
#include "world/map/index.h"

#include <string.h>

static int64_t map_floor_mod(int64_t value, int64_t modulus) {
    const int64_t remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

uint32_t world_map_radius_for(uint64_t column_count) {
    ASSERT_ALWAYS(column_count >= 1u);
    uint32_t radius = 0;
    while (radius < WORLD_MAP_RADIUS_MAX) {
        const uint64_t side = 2ull * radius + 3ull;
        if (side * side > column_count) {
            break;
        }
        radius++;
    }
    return radius;
}

/* Every offset within the radius, the nearest first. A counting sort, because each distance is a small whole number. */
static void map_offsets_build(world_map_t *map, arena_t *arena) {
    const int32_t radius = (int32_t)map->table_radius;
    const int64_t limit = (int64_t)radius * radius;
    uint32_t *const starts = arena_alloc_zeroed(arena, ((uint64_t)limit + 2u) * sizeof(uint32_t), alignof(uint32_t));
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            const int64_t distance = (int64_t)dx * dx + (int64_t)dy * dy;
            if (distance <= limit) {
                starts[distance + 1]++;
            }
        }
    }
    for (int64_t i = 0; i <= limit; i++) {
        starts[i + 1] += starts[i];
    }
    map->offset_count = starts[limit + 1];
    map->borrowed_offsets =
        arena_alloc_array(arena, map->offset_count, sizeof(world_map_offset_t), alignof(world_map_offset_t));
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            const int64_t distance = (int64_t)dx * dx + (int64_t)dy * dy;
            if (distance <= limit) {
                map->borrowed_offsets[starts[distance]] = (world_map_offset_t){(int16_t)dx, (int16_t)dy};
                starts[distance]++;
            }
        }
    }
}

/* How many of the offsets are within the view. They are sorted, so this searches for the first that is not. */
static uint32_t map_view_offset_count(const world_map_t *map) {
    const int64_t limit = (int64_t)map->view_radius * map->view_radius;
    uint32_t low = 0;
    uint32_t high = map->offset_count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const world_map_offset_t offset = map->borrowed_offsets[middle];
        const int64_t distance = (int64_t)offset.dx * offset.dx + (int64_t)offset.dy * offset.dy;
        if (distance <= limit) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

static void map_window_set(world_map_t *map, int32_t center_x, int32_t center_y) {
    map->center_x = center_x;
    map->center_y = center_y;
    map->min_x = center_x - (int32_t)map->table_radius;
    map->min_y = center_y - (int32_t)map->table_radius;
    map->min_slot_x = (uint32_t)map_floor_mod(map->min_x, map->side);
    map->min_slot_y = (uint32_t)map_floor_mod(map->min_y, map->side);
}

void world_map_init(world_map_t *map, arena_t *arena, section_pool_t *pool, uint32_t table_radius, int32_t center_x,
                    int32_t center_y) {
    ASSERT_ALWAYS(table_radius <= WORLD_MAP_RADIUS_MAX);
    ASSERT_ALWAYS(center_x >= column_chunk_of(INT32_MIN) && center_x <= column_chunk_of(INT32_MAX));
    ASSERT_ALWAYS(center_y >= column_chunk_of(INT32_MIN) && center_y <= column_chunk_of(INT32_MAX));
    memset(map, 0, sizeof(*map));
    map->borrowed_pool = pool;
    map->table_radius = table_radius;
    map->side = 2u * table_radius + 1u;
    map->slot_count = map->side * map->side;
    map->view_radius = table_radius;
    map->borrowed_columns = column_array_alloc(arena, map->slot_count);
    map->borrowed_slots =
        arena_alloc_zeroed(arena, (uint64_t)map->slot_count * sizeof(world_map_slot_t), alignof(world_map_slot_t));
    map->borrowed_active = arena_alloc_array(arena, map->slot_count, sizeof(uint32_t), alignof(uint32_t));
    map_offsets_build(map, arena);
    map->view_offset_count = map_view_offset_count(map);
    map_window_set(map, center_x, center_y);
}

void world_map_deinit(world_map_t *map) {
    for (uint32_t i = 0; i < map->active_count; i++) {
        const uint32_t slot = map->borrowed_active[i];
        if (map->borrowed_slots[slot].state == WORLD_MAP_STATE_READY) {
            column_deinit(&map->borrowed_columns[slot], map->borrowed_pool);
        }
    }
    memset(map, 0, sizeof(*map));
}

/* Inside the window, every coordinate has its own slot, so what is kept can never be in the way of what is wanted. */
static bool map_keeps(const world_map_t *map, int32_t chunk_x, int32_t chunk_y) {
    const int64_t dx = (int64_t)chunk_x - map->center_x;
    const int64_t dy = (int64_t)chunk_y - map->center_y;
    const int64_t table_radius = map->table_radius;
    if (dx < -table_radius || dx > table_radius || dy < -table_radius || dy > table_radius) {
        return false;
    }
    const int64_t keep = (int64_t)map->view_radius + WORLD_MAP_KEEP_EXTRA;
    return dx * dx + dy * dy <= keep * keep;
}

static void map_active_add(world_map_t *map, uint32_t slot) {
    map->borrowed_slots[slot].active_index = map->active_count;
    map->borrowed_active[map->active_count] = slot;
    map->active_count++;
}

static void map_active_remove(world_map_t *map, uint32_t slot) {
    const uint32_t index = map->borrowed_slots[slot].active_index;
    const uint32_t last = map->borrowed_active[map->active_count - 1u];
    map->borrowed_active[index] = last;
    map->borrowed_slots[last].active_index = index;
    map->active_count--;
}

static void map_pending_drop(world_map_t *map, uint32_t slot) {
    map_active_remove(map, slot);
    map->borrowed_slots[slot].state = WORLD_MAP_STATE_EMPTY;
    map->pending_count--;
}

/* After either the center or the view has changed: goes over every column there is, whichever way each has gone. */
static void map_refresh(world_map_t *map) {
    uint32_t leaving = 0;
    uint32_t i = 0;
    while (i < map->active_count) {
        const uint32_t slot = map->borrowed_active[i];
        world_map_slot_t *const entry = &map->borrowed_slots[slot];
        const bool keep = map_keeps(map, entry->chunk_x, entry->chunk_y);
        if (entry->state == WORLD_MAP_STATE_PENDING) {
            if (!keep) {
                map_pending_drop(map, slot); /* puts another in place i, so i stays */
                continue;
            }
        } else {
            entry->leaving = (uint8_t)(keep ? 0u : 1u);
            leaving += entry->leaving;
        }
        i++;
    }
    map->leaving_count = leaving;
    map->stream_cursor = 0u;
    map->change_count++;
}

void world_map_set_center(world_map_t *map, int32_t center_x, int32_t center_y) {
    ASSERT_ALWAYS(center_x >= column_chunk_of(INT32_MIN) && center_x <= column_chunk_of(INT32_MAX));
    ASSERT_ALWAYS(center_y >= column_chunk_of(INT32_MIN) && center_y <= column_chunk_of(INT32_MAX));
    if (center_x == map->center_x && center_y == map->center_y) {
        return;
    }
    map_window_set(map, center_x, center_y);
    map_refresh(map);
}

void world_map_set_view_radius(world_map_t *map, uint32_t view_radius) {
    ASSERT_ALWAYS(view_radius <= map->table_radius);
    if (view_radius == map->view_radius) {
        return;
    }
    map->view_radius = view_radius;
    map->view_offset_count = map_view_offset_count(map);
    map_refresh(map);
}

uint32_t world_map_stream_take(world_map_t *map, world_map_request_t *out, uint32_t max) {
    uint32_t taken = 0;
    while (taken < max && map->stream_cursor < map->view_offset_count) {
        const world_map_offset_t offset = map->borrowed_offsets[map->stream_cursor];
        map->stream_cursor++;
        const int32_t chunk_x = map->center_x + offset.dx;
        const int32_t chunk_y = map->center_y + offset.dy;
        uint32_t slot = 0;
        const bool in_window = world_map_slot_of(map, chunk_x, chunk_y, &slot);
        ASSERT_ALWAYS(in_window);
        world_map_slot_t *const entry = &map->borrowed_slots[slot];
        if (entry->state != WORLD_MAP_STATE_EMPTY) {
            continue; /* it is this column, or one on its way out, whose unload rewinds the cursor */
        }
        entry->state = WORLD_MAP_STATE_PENDING;
        entry->leaving = 0u;
        entry->chunk_x = chunk_x;
        entry->chunk_y = chunk_y;
        entry->generation++;
        map_active_add(map, slot);
        map->pending_count++;
        out[taken] = (world_map_request_t){chunk_x, chunk_y, slot, entry->generation};
        taken++;
    }
    return taken;
}

static bool map_request_is_current(const world_map_t *map, const world_map_request_t *request) {
    ASSERT_ALWAYS(request->slot < map->slot_count);
    const world_map_slot_t *const entry = &map->borrowed_slots[request->slot];
    return entry->state == WORLD_MAP_STATE_PENDING && entry->generation == request->generation;
}

column_t *world_map_stream_commit(world_map_t *map, const world_map_request_t *request) {
    if (!map_request_is_current(map, request)) {
        return nullptr;
    }
    world_map_slot_t *const entry = &map->borrowed_slots[request->slot];
    column_t *const column = &map->borrowed_columns[request->slot];
    column_init(column, entry->chunk_x, entry->chunk_y);
    entry->state = WORLD_MAP_STATE_READY;
    map->pending_count--;
    map->ready_count++;
    map->change_count++;
    return column;
}

bool world_map_stream_cancel(world_map_t *map, const world_map_request_t *request) {
    if (!map_request_is_current(map, request)) {
        return false;
    }
    map_pending_drop(map, request->slot);
    map->stream_cursor = 0u;
    return true;
}

bool world_map_stream_leaving(world_map_t *map, world_map_leaving_t *out) {
    if (map->leaving_count == 0u) {
        return false;
    }
    /* The count says there is one, so one walk round finds it. Removals may have moved things past the cursor: this is
     * why it goes all the way round and not only to the end. */
    for (uint32_t step = 0; step < map->active_count; step++) {
        if (map->leaving_cursor >= map->active_count) {
            map->leaving_cursor = 0u;
        }
        const uint32_t slot = map->borrowed_active[map->leaving_cursor];
        map->leaving_cursor++;
        const world_map_slot_t *const entry = &map->borrowed_slots[slot];
        if (entry->leaving != 0u) {
            *out = (world_map_leaving_t){entry->chunk_x, entry->chunk_y, slot};
            return true;
        }
    }
    ASSERT_ALWAYS(false);
    return false;
}

void world_map_unload(world_map_t *map, uint32_t slot) {
    ASSERT_ALWAYS(slot < map->slot_count);
    world_map_slot_t *const entry = &map->borrowed_slots[slot];
    ASSERT_ALWAYS(entry->state == WORLD_MAP_STATE_READY);
    column_deinit(&map->borrowed_columns[slot], map->borrowed_pool);
    map_active_remove(map, slot);
    map->leaving_count -= entry->leaving;
    entry->state = WORLD_MAP_STATE_EMPTY;
    map->ready_count--;
    map->stream_cursor = 0u;
    map->change_count++;
}

world_map_status_t world_map_block_get(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint16_t *out) {
    if (!column_z_in_gameplay(z)) {
        return WORLD_MAP_STATUS_Z_OUT_OF_RANGE;
    }
    const column_t *const column = world_map_column_at(map, column_chunk_of(x), column_chunk_of(y));
    if (column == nullptr) {
        return WORLD_MAP_STATUS_NOT_LOADED;
    }
    *out = section_block_get(&column->sections[column_section_of(z)],
                             section_voxel_index(column_local_of(x), column_local_of(y), column_section_local_of(z)));
    return WORLD_MAP_STATUS_OK;
}

world_map_status_t world_map_light_get(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint8_t *out) {
    if (!column_z_in_gameplay(z)) {
        return WORLD_MAP_STATUS_Z_OUT_OF_RANGE;
    }
    const column_t *const column = world_map_column_at(map, column_chunk_of(x), column_chunk_of(y));
    if (column == nullptr) {
        return WORLD_MAP_STATUS_NOT_LOADED;
    }
    *out = section_light_get(&column->sections[column_section_of(z)],
                             section_voxel_index(column_local_of(x), column_local_of(y), column_section_local_of(z)));
    return WORLD_MAP_STATUS_OK;
}
