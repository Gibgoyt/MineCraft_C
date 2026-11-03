/* See README.md. The Owner: it holds the memory its parts borrow. */
#include "save/index.h"

#include <string.h>

bool save_init(save_t *save) {
    memset(save, 0, sizeof(*save));
    save->world.lock.fd = -1;
    if (!heap_reserve(SAVE_WORLD_ARENA_BYTES + SAVE_STATE_ARENA_BYTES, &save->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&save->arena_world, "save_world", save->owned_region.owned_base, SAVE_WORLD_ARENA_BYTES);
    arena_init(&save->arena_state, "save_state", save->owned_region.owned_base + SAVE_WORLD_ARENA_BYTES,
               SAVE_STATE_ARENA_BYTES);
    return true;
}

void save_deinit(save_t *save) {
    save_close(save);
    if (save->owned_region.owned_base != nullptr) {
        arena_deinit(&save->arena_state);
        arena_deinit(&save->arena_world);
        heap_release(&save->owned_region);
    }
    memset(save, 0, sizeof(*save));
}

save_world_status_t save_open(save_t *save, const char *world_directory, uint64_t seed) {
    arena_reset(&save->arena_world);
    arena_reset(&save->arena_state);
    const save_world_status_t blocks = save_world_open(&save->world, &save->arena_world, world_directory, seed);
    if (blocks != SAVE_WORLD_OK) {
        return blocks;
    }
    /* After the blocks, for their number, and with their lock held. */
    const save_world_status_t status =
        save_state_open(&save->state, &save->arena_state, world_directory, seed, save->world.generation);
    if (status != SAVE_WORLD_OK) {
        save_world_close(&save->world);
        arena_reset(&save->arena_world);
    }
    return status;
}

void save_close(save_t *save) {
    if (save->world.open != 0u) {
        /* A write under way is using all of this. */
        save_autosave_stop(&save->autosave);
        save_state_close(&save->state);
        save_world_close(&save->world);
        arena_reset(&save->arena_state);
        arena_reset(&save->arena_world);
    }
}
