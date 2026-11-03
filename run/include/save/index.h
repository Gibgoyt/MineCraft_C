/* See run/src/save/README.md. */
#ifndef MC_RUN_SAVE_INDEX_H
#define MC_RUN_SAVE_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "save/world/index.h"

#include <stddef.h>
#include <stdint.h>

/* Holds what a save's parts borrow. There is one, under app_t. world is zero bytes outside save_open. */
typedef struct save_t {
    arena_t arena_world;        /* 0: over owned_region; World: reset at save_close */
    heap_region_t owned_region; /* 64 */
    uint8_t _pad0[48];          /* 80 */
    save_world_t world;         /* 128 */
    uint8_t _pad1[16];          /* 432 */
} save_t;
static_assert(sizeof(save_t) == 448);
static_assert(alignof(save_t) == 64);
static_assert(offsetof(save_t, arena_world) == 0);
static_assert(offsetof(save_t, owned_region) == 64);
static_assert(offsetof(save_t, _pad0) == 80);
static_assert(offsetof(save_t, world) == 128);
static_assert(offsetof(save_t, _pad1) == 432);

/* Reserves the region. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool save_init(save_t *save);

/* Releases it, with any world still open. Safe on a zeroed save_t. */
void save_deinit(save_t *save);

/* From here on the world in this directory, of this seed, is what is read and written. Anything but SAVE_WORLD_OK has logged why, and no world
 * is open. See save_world_open. */
[[nodiscard]] save_world_status_t save_open(save_t *save, const char *world_directory, uint64_t seed);

/* Ends it. What has not been flushed is lost, and it says so. Does nothing if none is open. */
void save_close(save_t *save);

#endif
