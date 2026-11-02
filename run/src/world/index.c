/* See README.md. The Owner: it holds the regions, and lends their arenas to the parts. */
#include "world/index.h"

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
    if (limits.column_count > UINT64_MAX / sizeof(column_t)) {
        LOG_ERROR("world: a limit of %" PRIu64 " columns does not fit in memory at all", limits.column_count);
        return false;
    }
    if (!heap_reserve(limits.section_bytes, &world->owned_sections)) {
        return false; /* it has said why */
    }
    if (!heap_reserve(limits.column_count * sizeof(column_t), &world->owned_columns)) {
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
    arena_reset(&world->arena_columns);
    arena_reset(&world->arena_sections);
    section_pool_reset(&world->section_pool);
}
