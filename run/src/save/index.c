/* See README.md. The Owner: it holds the memory its parts borrow. */
#include "save/index.h"

#include <string.h>

bool save_init(save_t *save) {
    memset(save, 0, sizeof(*save));
    save->world.lock.fd = -1;
    if (!heap_reserve(SAVE_WORLD_ARENA_BYTES, &save->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&save->arena_world, "save_world", save->owned_region.owned_base, save->owned_region.bytes);
    return true;
}

void save_deinit(save_t *save) {
    save_close(save);
    if (save->owned_region.owned_base != nullptr) {
        arena_deinit(&save->arena_world);
        heap_release(&save->owned_region);
    }
    memset(save, 0, sizeof(*save));
}

save_world_status_t save_open(save_t *save, const char *world_directory, uint64_t seed) {
    arena_reset(&save->arena_world);
    return save_world_open(&save->world, &save->arena_world, world_directory, seed);
}

void save_close(save_t *save) {
    if (save->world.open != 0u) {
        save_world_close(&save->world);
        arena_reset(&save->arena_world);
    }
}
