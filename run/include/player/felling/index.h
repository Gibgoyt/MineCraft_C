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
    /* The most logs the find looks at for one cut: four times the queue, and more than any tree the generator draws, with two grown into
     * each other. Past it the find is unsure, and nothing falls. */
    PLAYER_FELLING_FIND_MAX = 1024u,
    /* Which cells the find has: a power of two, four times over the logs. */
    PLAYER_FELLING_FIND_SEEN = 4096u,
    /* The most columns holding a root log in one find. A mangrove has 5 and a wide trunk 4; more is a build set into the ground, and
     * the find is unsure. */
    PLAYER_FELLING_TRUNK_COLUMNS = 64u,
};
static_assert((PLAYER_FELLING_SEEN & (PLAYER_FELLING_SEEN - 1u)) == 0u);
static_assert(PLAYER_FELLING_SEEN >= 4u * PLAYER_FELLING_LOGS_MAX);
static_assert((PLAYER_FELLING_FIND_SEEN & (PLAYER_FELLING_FIND_SEEN - 1u)) == 0u);
static_assert(PLAYER_FELLING_FIND_SEEN >= 4u * PLAYER_FELLING_FIND_MAX);
static_assert(PLAYER_FELLING_FIND_MAX <= UINT16_MAX);
static_assert(PLAYER_FELLING_TRUNK_COLUMNS < UINT8_MAX);

/* A log waiting to go, and what it was when the walk found it. */
typedef struct player_felling_cell_t {
    int32_t x;      /* 0 */
    int32_t y;      /* 4 */
    int16_t z;      /* 8 */
    uint16_t block; /* 10 */
} player_felling_cell_t;
static_assert(sizeof(player_felling_cell_t) == 12);

/* A tree on its way down, and the find of the last cut. Plain numbers, and the sim's. Kept by whoever opens a world. It is not saved, as
 * it lives about a second: to quit in that second leaves the rest of that tree standing. The find's lists are only scratch between two
 * cuts, cleared by each. */
typedef struct player_felling_t {
    uint64_t felled_count;      /* 0: logs that have gone, not counting the one cut */
    uint64_t turned_away_count; /* 8: falling logs with no room in the queue: then none of that cut fall */
    player_felling_cell_t cells[PLAYER_FELLING_LOGS_MAX]; /* 16: nearest the cut first */
    uint16_t seen[PLAYER_FELLING_SEEN];                   /* 3088: a cell's number in cells plus 1, or 0 */
    int32_t box_low[COLLISION_AXES];                      /* 5136: what the removed logs fill, for the leaves */
    int32_t box_high[COLLISION_AXES];                     /* 5148 */
    uint32_t head;                                        /* 5160: the next to go */
    uint32_t tail;                                        /* 5164: one past the last */
    uint16_t last_block;                                  /* 5168: what went last, whose wear is the caller's */
    uint8_t _pad0[14];                                    /* 5170 */
    uint64_t find_overflow_count; /* 5184: finds past PLAYER_FELLING_FIND_MAX or the trunk columns */
    uint64_t find_unloaded_count; /* 5192: finds that met a column not loaded */
    uint64_t not_a_tree_count;    /* 5200: finds with no leaves or no root log */
    uint64_t stood_count;         /* 5208: trees that kept standing after a cut */
    player_felling_cell_t found[PLAYER_FELLING_FIND_MAX]; /* 5216: the tree, from the cut, in walk order */
    uint16_t found_seen[PLAYER_FELLING_FIND_SEEN];        /* 17504: a log's number in found plus 1, or 0 */
    uint16_t found_queue[PLAYER_FELLING_FIND_MAX];        /* 25696: the floods' own list */
    uint8_t found_flags[PLAYER_FELLING_FIND_MAX];         /* 27744: FELLING_FOUND_ bits */
    uint8_t found_column[PLAYER_FELLING_FIND_MAX];        /* 28768: its trunk column plus 1, or 0 */
    int32_t trunk_x[PLAYER_FELLING_TRUNK_COLUMNS];        /* 29792: the columns holding a root log */
    int32_t trunk_y[PLAYER_FELLING_TRUNK_COLUMNS];        /* 30048 */
    uint8_t trunk_in[PLAYER_FELLING_TRUNK_COLUMNS];       /* 30304: 1 for a column of the cut's trunk */
    uint32_t found_count;                                 /* 30368 */
    uint32_t trunk_count;                                 /* 30372 */
    uint32_t last_layer; /* 30376: logs left in the cut's layer, or UINT32_MAX for a branch */
    uint32_t last_stump; /* 30380: logs that stayed joined to a root, for a report */
} player_felling_t;
static_assert(offsetof(player_felling_t, find_overflow_count) == 5184);
static_assert(offsetof(player_felling_t, found) == 5216);
static_assert(offsetof(player_felling_t, found_seen) == 17504);
static_assert(offsetof(player_felling_t, trunk_in) == 30304);
static_assert(offsetof(player_felling_t, found_count) == 30368);
static_assert(sizeof(player_felling_t) == 30384);
static_assert(alignof(player_felling_t) == 8);

/* Nothing under way. */
void player_felling_start(player_felling_t *felling);

/* A log has just been cut at this cell, and is gone. Finds the tree: every touching log of the same wood, up and down, including at a
 * corner. It is a tree when one of them touches leaves by a face and one is a root log, set in the ground. Then, if the cut emptied a
 * layer of the tree's trunk, everything above that layer is queued to fall, and so is anything no longer joined to a root log. A tree
 * already coming down goes on, with these behind it. Whether the block is a log is the caller's. Returns how many were queued, the cut
 * among them: 0 when it was no tree or the find was unsure (a column not loaded, too many logs), and 1 when the tree stands. */
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
