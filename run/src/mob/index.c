/* See README.md. The Owner: it holds the region its parts' memory comes from, and decides when that dies. */
#include "mob/index.h"

#include <string.h>

enum : uint64_t {
    MOB_REGION_BYTES = 4ull << 20,
};
static_assert((uint64_t)MOB_PATH_BYTES + (uint64_t)MOB_ZOMBIE_BYTES + (uint64_t)MOB_SKELETON_BYTES +
                  (uint64_t)MOB_CREEPER_BYTES <=
              (uint64_t)MOB_REGION_BYTES);

bool mob_init(mob_t *mob, job_system_t *job) {
    memset(mob, 0, sizeof(*mob));
    if (!heap_reserve(MOB_REGION_BYTES, &mob->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&mob->arena_world, "mob_world", mob->owned_region.owned_base, MOB_REGION_BYTES);
    mob->borrowed_job = job;
    return true;
}

void mob_deinit(mob_t *mob) {
    arena_deinit(&mob->arena_world);
    heap_release(&mob->owned_region);
    memset(mob, 0, sizeof(*mob));
}

void mob_open(mob_t *mob, const world_t *world, uint64_t seed) {
    arena_reset(&mob->arena_world);
    mob_path_start(&mob->path, &mob->arena_world, &world->map, &world->edit, mob->borrowed_job);
    mob_zombie_start(&mob->zombie, &mob->arena_world, terrain_rng_derive(seed, terrain_rng_salt("mob/zombie")));
    mob_skeleton_start(&mob->skeleton, &mob->arena_world, terrain_rng_derive(seed, terrain_rng_salt("mob/skeleton")));
    mob_creeper_start(&mob->creeper, &mob->arena_world, terrain_rng_derive(seed, terrain_rng_salt("mob/creeper")));
}

void mob_close(mob_t *mob) {
    mob_path_stop(&mob->path);
    memset(&mob->zombie, 0, sizeof(mob->zombie));
    memset(&mob->creeper, 0, sizeof(mob->creeper));
    arena_reset(&mob->arena_world);
}

void mob_tick(mob_t *mob) {
    mob_path_tick(&mob->path);
}

float32_t mob_minds_tick(mob_t *mob, entity_t *entity, const mob_zombie_view_t *view) {
    return mob_zombie_tick(&mob->zombie, &mob->path, &entity->store, &entity->item_drops, mob->path.borrowed_map, view);
}
