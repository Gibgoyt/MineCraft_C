/* See README.md. The Owner: it holds the regions, and lends their arenas to the parts. */
#include "world/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <inttypes.h>
#include <string.h>

world_limits_t world_limits_default(void) {
    return (world_limits_t){
        .section_bytes = WORLD_SECTION_BYTES_DEFAULT,
        .column_count = WORLD_COLUMN_COUNT_DEFAULT,
    };
}

bool world_init(world_t *world, world_limits_t limits) {
    memset(world, 0, sizeof(*world));
    if (limits.column_count == 0u) {
        LOG_ERROR("world: a limit of no columns has no room for any world");
        return false;
    }
    if (limits.column_count > (UINT64_MAX - WORLD_COLUMN_BYTES_FIXED) / WORLD_COLUMN_BYTES_PER_SLOT) {
        LOG_ERROR("world: a limit of %" PRIu64 " columns does not fit in memory at all", limits.column_count);
        return false;
    }
    if (!heap_reserve(limits.section_bytes, &world->owned_sections)) {
        return false; /* it has said why */
    }
    if (!heap_reserve(limits.column_count * WORLD_COLUMN_BYTES_PER_SLOT + WORLD_COLUMN_BYTES_FIXED,
                      &world->owned_columns)) {
        heap_release(&world->owned_sections);
        return false; /* it has said why */
    }
    arena_init(&world->arena_sections, "world_sections", world->owned_sections.owned_base, world->owned_sections.bytes);
    arena_init(&world->arena_columns, "world_columns", world->owned_columns.owned_base, world->owned_columns.bytes);
    section_pool_init(&world->section_pool, &world->arena_sections);
    return true;
}

void world_deinit(world_t *world) {
    section_pool_deinit(&world->section_pool);
    arena_deinit(&world->arena_columns);
    arena_deinit(&world->arena_sections);
    heap_release(&world->owned_columns);
    heap_release(&world->owned_sections);
    memset(world, 0, sizeof(*world));
}

void world_reset(world_t *world) {
    ASSERT_ALWAYS(world->map.borrowed_slots == nullptr);
    arena_reset(&world->arena_columns);
    arena_reset(&world->arena_sections);
    section_pool_reset(&world->section_pool);
}

uint64_t world_open_bytes(uint32_t table_radius) {
    const uint64_t side = 2ull * table_radius + 1ull;
    return side * side * WORLD_COLUMN_BYTES_PER_SLOT + WORLD_COLUMN_BYTES_FIXED;
}

bool world_open(world_t *world, uint32_t table_radius, int32_t center_x, int32_t center_y) {
    ASSERT_ALWAYS(world->map.borrowed_slots == nullptr);
    if (table_radius > WORLD_MAP_RADIUS_MAX) {
        LOG_ERROR("world: a table radius of %" PRIu32 " is past the most a map can hold, %" PRIu32, table_radius,
                  (uint32_t)WORLD_MAP_RADIUS_MAX);
        return false;
    }
    const uint64_t need = world_open_bytes(table_radius);
    const uint64_t have = world->arena_columns.capacity - world->arena_columns.used;
    if (need > have) {
        LOG_ERROR("world: a table radius of %" PRIu32 " needs %" PRIu64 " bytes, and the limits leave %" PRIu64,
                  table_radius, need, have);
        return false;
    }
    world_map_init(&world->map, &world->arena_columns, &world->section_pool, table_radius, center_x, center_y);
    world_edit_init(&world->edit, &world->arena_columns, &world->map, WORLD_EDIT_LOG_CAPACITY_DEFAULT);
    world_edit_snapshot_init(&world->snapshot, &world->arena_columns, &world->edit,
                             WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT);
    return true;
}

void world_close(world_t *world) {
    if (world->map.borrowed_slots == nullptr) {
        return;
    }
    world_edit_snapshot_deinit(&world->snapshot);
    world_edit_deinit(&world->edit);
    world_map_deinit(&world->map);
    world_reset(world);
}
