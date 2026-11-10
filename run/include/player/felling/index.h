/* See run/src/player/felling/README.md. */
#ifndef MC_RUN_PLAYER_FELLING_INDEX_H
#define MC_RUN_PLAYER_FELLING_INDEX_H

#include "player/interaction/index.h"
#include "types/index.h"
#include "world/edit/index.h"
#include "world/map/index.h"
#include "world/tick/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The most logs waiting at once. ctest run_player_felling measures the biggest tree the generator makes and holds this well above it,
     * with room for giants grown into each other. Past it, the rest stay. */
    PLAYER_FELLING_LOGS_MAX = 256u,
    /* Which cells are in the queue: a power of two, and far enough over the logs that a look never goes far. */
    PLAYER_FELLING_SEEN = 1024u,
    /* Logs a tick. At 20 ticks a second that is 160 a second, and it leaves the hands' ring of 32 room between two looks. */
    PLAYER_FELLING_PER_TICK = 8u,
};
static_assert((PLAYER_FELLING_SEEN & (PLAYER_FELLING_SEEN - 1u)) == 0u);
static_assert(PLAYER_FELLING_SEEN >= 4u * PLAYER_FELLING_LOGS_MAX);

/* A log waiting to go, and what it was when the walk found it. */
typedef struct player_felling_cell_t {
    int32_t x;      /* 0 */
    int32_t y;      /* 4 */
    int16_t z;      /* 8 */
    uint16_t block; /* 10 */
} player_felling_cell_t;
static_assert(sizeof(player_felling_cell_t) == 12);

/* A tree on its way down. Plain numbers, and the sim's. Kept by whoever opens a world. It is not saved, as it lives about a second: to quit in that second leaves the
 * rest of that tree standing. */
typedef struct player_felling_t {
    uint64_t felled_count;                                /* 0: logs that have gone, not counting the one cut */
    uint64_t turned_away_count;                           /* 8: logs the walk found with the queue full, which stay */
    player_felling_cell_t cells[PLAYER_FELLING_LOGS_MAX]; /* 16: nearest the cut first */
    uint16_t seen[PLAYER_FELLING_SEEN];                   /* 3088: a cell's number in cells plus 1, or 0 */
    int32_t box_low[COLLISION_AXES];                      /* 5136: what the removed logs fill, for the leaves */
    int32_t box_high[COLLISION_AXES];                     /* 5148 */
    uint32_t head;                                        /* 5160: the next to go */
    uint32_t tail;                                        /* 5164: one past the last */
    uint16_t last_block;                                  /* 5168: what went last, whose wear is the caller's */
    uint8_t _pad0[14];                                    /* 5170 */
} player_felling_t;
static_assert(sizeof(player_felling_t) == 5184);
static_assert(alignof(player_felling_t) == 8);

/* Nothing under way. */
void player_felling_start(player_felling_t *felling);

/* A log has just been cut at this cell, and is gone. Walks to every touching log of the same wood at or above it, including diagonally, and
 * queues them if any of them touches leaves. So a log wall stays, and a column not loaded stops the walk. A tree already coming down goes on,
 * with these behind it. Whether the block is a log is the caller's. Returns how many were queued, the cut among them. */
uint32_t player_felling_cut(player_felling_t *felling, const world_map_t *map, uint16_t block, int32_t x, int32_t y,
                            int32_t z);

/* One tick, on the thread holding the map: up to PLAYER_FELLING_PER_TICK logs break and drop, nearest the cut first, whatever is in hand.
 * When the queue ends, the leaves round what went are put down for world_tick to look at. Holds back, with nothing lost, until the hands'
 * ring has room. Returns how many went, for the sound; they wear nothing, as only the log the hands cut is a use. Never waits and never
 * touches the heap. */
uint32_t player_felling_tick(player_felling_t *felling, player_interaction_t *hands, world_edit_t *edit,
                             world_tick_t *tick);

/* Logs waiting. */
static inline uint32_t player_felling_pending(const player_felling_t *felling) {
    return felling->tail - felling->head;
}

#endif
