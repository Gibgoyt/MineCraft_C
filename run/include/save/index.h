/* See run/src/save/README.md. */
#ifndef MC_RUN_SAVE_INDEX_H
#define MC_RUN_SAVE_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "save/autosave/index.h"
#include "save/state/index.h"
#include "save/world/index.h"

#include <stddef.h>
#include <stdint.h>

/* Holds what a save's parts borrow. There is one, under app_t. Everything past owned_region is zero bytes outside save_open. */
typedef struct save_t {
    arena_t arena_world;        /* 0: over owned_region, for world; World: reset at save_close */
    arena_t arena_state;        /* 64: over owned_region, for state; World: reset at save_close */
    heap_region_t owned_region; /* 128 */
    save_world_t world;         /* 144 */
    save_state_t state;         /* 448 */
    uint8_t _pad0[8];           /* 504 */
    save_autosave_t autosave;   /* 512: started by game_open, since it runs with a game */
} save_t;
static_assert(sizeof(save_t) == 704);
static_assert(alignof(save_t) == 64);
static_assert(offsetof(save_t, arena_world) == 0);
static_assert(offsetof(save_t, arena_state) == 64);
static_assert(offsetof(save_t, owned_region) == 128);
static_assert(offsetof(save_t, world) == 144);
static_assert(offsetof(save_t, state) == 448);
static_assert(offsetof(save_t, _pad0) == 504);
static_assert(offsetof(save_t, autosave) == 512);

/* Reserves the region. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool save_init(save_t *save);

/* Releases it, with any world still open. Safe on a zeroed save_t. */
void save_deinit(save_t *save);

/* From here on the world in this directory, of this seed, is what is read and written, blocks and state alike. Anything but SAVE_WORLD_OK has
 * logged why, and no world is open. See save_world_open and save_state_open. */
[[nodiscard]] save_world_status_t save_open(save_t *save, const char *world_directory, uint64_t seed);

/* Ends it, once any autosave's write has finished, which is why no worker may call it. What has not been saved is lost. Does nothing if none is
 * open. */
void save_close(save_t *save);

#endif
