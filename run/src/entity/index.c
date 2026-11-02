/* See README.md. The Owner: it holds the region its parts' memory comes from, and decides when that dies. */
#include "entity/index.h"

#include <string.h>

enum : uint64_t {
    ENTITY_REGION_BYTES = 2ull << 20,
};
static_assert((uint64_t)ITEM_DROP_STORE_BYTES <= (uint64_t)ENTITY_REGION_BYTES);

bool entity_init(entity_t *entity, physics_t *physics) {
    memset(entity, 0, sizeof(*entity));
    if (!heap_reserve(ENTITY_REGION_BYTES, &entity->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&entity->arena_world, "entity_world", entity->owned_region.owned_base, ENTITY_REGION_BYTES);
    entity->borrowed_physics = physics;
    return true;
}

void entity_deinit(entity_t *entity) {
    arena_deinit(&entity->arena_world);
    heap_release(&entity->owned_region);
    memset(entity, 0, sizeof(*entity));
}

void entity_open(entity_t *entity, uint64_t seed) {
    arena_reset(&entity->arena_world);
    item_drop_store_start(&entity->item_drops, &entity->arena_world, seed);
}

void entity_close(entity_t *entity) {
    arena_reset(&entity->arena_world);
    memset(&entity->item_drops, 0, sizeof(entity->item_drops));
}

void entity_tick(entity_t *entity, const world_map_t *map) {
    item_drop_tick(&entity->item_drops, entity->borrowed_physics, map);
}
