/* See run/src/mob/path/README.md. */
#ifndef MC_RUN_MOB_PATH_INDEX_H
#define MC_RUN_MOB_PATH_INDEX_H

#include "arena/index.h"
#include "entity/store/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "types/index.h"
#include "world/edit/index.h"
#include "world/map/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* What one search may do. Each is a hard bound: past it the search gives its best so far. */
    MOB_PATH_NODES_MAX = 8192u,     /* places it may write down; a power of two */
    MOB_PATH_NODES_DEFAULT = 2048u, /* what an ask of 0 means, and what mob_path_ask_for_body asks */
    MOB_PATH_REACH_XY = 48u,        /* blocks from where it starts, along x and along y */
    MOB_PATH_REACH_Z = 32u,         /* and along z */
    MOB_PATH_POINTS_MAX = 64u,      /* a longer way comes back cut short, as a partial path */
    MOB_PATH_HEIGHT_MAX = 16u,      /* half blocks */
    MOB_PATH_DROP_MAX = 8u,         /* the most blocks any ask may fall in one move */
    MOB_PATH_DROP_DEFAULT = 3u,     /* what a fall that does not hurt comes to */
    MOB_PATH_RADIUS_MAX = 15u,
    MOB_PATH_SETTLE_BLOCKS = 16u, /* how far below a place in the air its ground is looked for */

    /* What one tick may do. Whatever is past it waits its turn at the front of the next. */
    MOB_PATH_ROUND_SEARCHES = 64u,
    MOB_PATH_ROUND_NODES = MOB_PATH_NODES_MAX, /* see the README for what this comes to in time */
    MOB_PATH_JOBS_MAX = 8u, /* jobs out at once. The thread calling mob_path_tick also searches, as one more */

    MOB_PATH_CACHE_ENTRIES = 256u,
    MOB_PATH_EDITS_MAX = 1024u, /* more changed blocks than this between ticks empties the cache unread */
    MOB_PATH_BODIES = ENTITY_STORE_CAPACITY,
    MOB_PATH_STUCK_TICKS = 80u,
};
static_assert((MOB_PATH_NODES_MAX & (MOB_PATH_NODES_MAX - 1u)) == 0u);
static_assert(MOB_PATH_ROUND_SEARCHES <= MOB_PATH_CACHE_ENTRIES); /* a round's answers all fit in the cache at once */

/* What moves cost, in hundredths of a block's walk. Every cost is a plain sum of these, so that a path can be worked out by hand. */
enum : uint32_t {
    MOB_PATH_COST_STRAIGHT = 100u,
    MOB_PATH_COST_DIAGONAL = 141u,
    MOB_PATH_COST_RISE_HALF = 30u, /* per half block up */
    MOB_PATH_COST_FALL_HALF = 5u,  /* per half block down */
    MOB_PATH_COST_WATER = 150u,    /* more, to move sideways into water */
    MOB_PATH_COST_CLIMB_UP = 100u, /* up a ladder or through water, with no move sideways */
    MOB_PATH_COST_CLIMB_DOWN = 30u,
};
/* However dear every move of a path, its points' costs fit the 16 bits a point holds them in. */
static_assert(MOB_PATH_POINTS_MAX * (MOB_PATH_COST_DIAGONAL + MOB_PATH_COST_WATER +
                                     (2u * MOB_PATH_DROP_MAX + 1u) * MOB_PATH_COST_RISE_HALF) <
              65536u);

/* What is asked: a plain description, so that the same ask of the same world always has the same answer. */
typedef struct mob_path_ask_t {
    int32_t start[3];    /* 0: the cell of its feet */
    int32_t goal[3];     /* 12: the same */
    uint16_t nodes;      /* 24: its budget; 0 means MOB_PATH_NODES_DEFAULT */
    uint8_t height;      /* 26: of the body, in half blocks, rounded up */
    uint8_t drop;        /* 27: the most blocks it will fall in one move */
    uint8_t goal_radius; /* 28: arriving this near along each axis will do */
    uint8_t _pad0[3];    /* 29 */
} mob_path_ask_t;
static_assert(sizeof(mob_path_ask_t) == 32);

/* One place on a path. A body stands in the middle of its cell, with its feet at z + half / 2. */
typedef struct mob_path_point_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    int32_t z;        /* 8 */
    uint16_t cost;    /* 12: of getting here from the path's first point */
    uint8_t half;     /* 14: 1 on top of a slab, otherwise 0 */
    uint8_t _pad0[1]; /* 15 */
} mob_path_point_t;
static_assert(sizeof(mob_path_point_t) == 16);

typedef enum mob_path_status_t : uint8_t {
    MOB_PATH_STATUS_NONE = 0,          /* it can stand where it is, and nothing it can reach is nearer */
    MOB_PATH_STATUS_COMPLETE = 1,      /* the way there, and no way the search looked at is cheaper */
    MOB_PATH_STATUS_PARTIAL = 2,       /* the way to the nearest place it found: a bound stopped it, or no way exists */
    MOB_PATH_STATUS_START_BLOCKED = 3, /* it cannot stand where it is, or anywhere just under */
} mob_path_status_t;

/* What was found. Every cell a search read is inside its box, which is what lets a cached answer outlast edits elsewhere. */
typedef struct mob_path_found_t {
    uint64_t serial;                              /* 0: counts answers given out; 0 from mob_path_search */
    int32_t box_min[3];                           /* 8 */
    int32_t box_max[3];                           /* 20 */
    uint32_t nodes;                               /* 32: places written down */
    uint16_t point_count;                         /* 36 */
    uint8_t status;                               /* 38: mob_path_status_t */
    uint8_t from_cache;                           /* 39: no search ran for this answer */
    uint8_t _pad0[8];                             /* 40 */
    mob_path_point_t points[MOB_PATH_POINTS_MAX]; /* 48: the first is where it starts */
} mob_path_found_t;
static_assert(sizeof(mob_path_found_t) == 48u + MOB_PATH_POINTS_MAX * 16u);

/* One place a search has written down. Internal to the search, and public only for its size. */
typedef struct mob_path_node_t {
    int16_t dx;       /* 0: from where it started */
    int16_t dy;       /* 2 */
    int16_t dz;       /* 4 */
    uint8_t half;     /* 6 */
    uint8_t closed;   /* 7 */
    uint32_t g;       /* 8: the cheapest way here so far */
    uint32_t f;       /* 12: g and the least it can still cost */
    uint32_t parent;  /* 16 */
    uint32_t heap_at; /* 20 */
} mob_path_node_t;
static_assert(sizeof(mob_path_node_t) == 24);

/* What one search works in. One per thread searching: the Owner gives the sim's, and a worker takes one from its Job arena. */
typedef struct mob_path_scratch_t {
    mob_path_node_t nodes[MOB_PATH_NODES_MAX];
    uint32_t heap[MOB_PATH_NODES_MAX];
    uint32_t table[2u * MOB_PATH_NODES_MAX];
} mob_path_scratch_t;
static_assert(sizeof(mob_path_scratch_t) == MOB_PATH_NODES_MAX * (24u + 4u + 8u));

/* An answer kept, with the ask it answers. */
typedef struct mob_path_entry_t {
    mob_path_ask_t ask;     /* 0 */
    uint64_t last_used;     /* 32: 0 means this entry is empty */
    uint64_t map_change;    /* 40: the map's change_count when the answer was last known good */
    mob_path_found_t found; /* 48 */
} mob_path_entry_t;
static_assert(sizeof(mob_path_entry_t) == 48u + sizeof(mob_path_found_t));

/* One search of a round: the ask until it is claimed, then what the thread that claimed it wrote. */
typedef struct mob_path_item_t {
    mob_path_ask_t ask;     /* 0 */
    mob_path_found_t found; /* 32 */
} mob_path_item_t;
static_assert(sizeof(mob_path_item_t) == 32u + sizeof(mob_path_found_t));

/* What one body asked and got. Indexed by the slot of its handle. */
typedef struct mob_path_body_t {
    mob_path_ask_t ask;     /* 0: its latest */
    mob_path_found_t found; /* 32: its last answer */
    entity_handle_t asker;  /* 1104: whose ask is waiting; meaningful while queued */
    entity_handle_t owner;  /* 1108: whose answer found is; ENTITY_HANDLE_NONE if nobody's */
    uint16_t cursor;        /* 1112: the point it is walking to */
    uint16_t stuck;         /* 1114: ticks since it last reached one */
    uint8_t queued;         /* 1116 */
    uint8_t _pad0[3];       /* 1117 */
} mob_path_body_t;
static_assert(sizeof(mob_path_body_t) == 1120);

/* What it has done since mob_path_start. */
typedef struct mob_path_stats_t {
    uint64_t requests;        /* mob_path_request calls */
    uint64_t searches;        /* searches run, anywhere */
    uint64_t worker_searches; /* of those, on a worker */
    uint64_t exact_hits;      /* answered from the cache: this start and this goal */
    uint64_t suffix_hits;     /* answered from the cache: the rest of a way through this start */
    uint64_t shared;          /* answered by a search another body's same ask ran in the same round */
    uint64_t dropped;         /* cache entries an edit or a map change made useless */
    uint64_t emptied;         /* times the whole cache went, for too many edits */
    uint64_t rounds;          /* ticks that ran a search */
    uint64_t nodes;           /* places written down, over all searches */
} mob_path_stats_t;
static_assert(sizeof(mob_path_stats_t) == 80);

/* Pathfinding for every body in a world. The sim's, apart from what its own jobs do during mob_path_tick. Zero bytes mean not running. */
typedef struct mob_path_t {
    job_group_t group; /* 0: every job ever sent; a job may outlast its round, so only mob_path_stop waits */

    /* A line of its own: workers write it. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint64_t round_cursor; /* 64: count << 32 | next item to claim */
    _Atomic uint64_t worker_searches;                               /* 72: relaxed */
    _Atomic uint32_t round_done;                                    /* 80: items finished */
    _Atomic uint32_t jobs_out;                                      /* 84: sent and not yet back; relaxed */
    uint8_t _pad0[40];                                              /* 88 */

    /* The rest is the sim's. Workers read the map, the items and nothing else, and only inside a round. */
    const world_map_t *borrowed_map;      /* 128 */
    const world_edit_t *borrowed_edit;    /* 136 */
    job_system_t *borrowed_job;           /* 144: nullptr runs every search on the calling thread */
    mob_path_body_t *borrowed_bodies;     /* 152: MOB_PATH_BODIES */
    uint32_t *borrowed_queue;             /* 160: MOB_PATH_BODIES: slots waiting, the longest first */
    mob_path_entry_t *borrowed_entries;   /* 168: MOB_PATH_CACHE_ENTRIES */
    mob_path_item_t *borrowed_items;      /* 176: MOB_PATH_ROUND_SEARCHES */
    mob_path_scratch_t *borrowed_scratch; /* 184: the calling thread's */
    uint64_t edit_cursor;                 /* 192: how far it has read world_edit's log */
    uint64_t use_clock;                   /* 200 */
    uint64_t serial;                      /* 208 */
    uint64_t requests;                    /* 216 */
    uint64_t searches;                    /* 224 */
    uint64_t exact_hits;                  /* 232 */
    uint64_t suffix_hits;                 /* 240 */
    uint64_t shared;                      /* 248 */
    uint64_t dropped;                     /* 256 */
    uint64_t emptied;                     /* 264 */
    uint64_t rounds;                      /* 272 */
    uint64_t nodes;                       /* 280 */
    uint32_t queue_count;                 /* 288 */
    uint8_t _pad1[28];                    /* 292 */
} mob_path_t;
static_assert(sizeof(mob_path_t) == 320);
static_assert(alignof(mob_path_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(mob_path_t, round_cursor) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(mob_path_t, borrowed_map) == 2 * MC_CPU_CACHE_LINE_BYTES);

/* What mob_path_start takes from its arena: each array, with an alignment's slack. */
enum : uint64_t {
    MOB_PATH_BYTES = (uint64_t)MOB_PATH_BODIES * (sizeof(mob_path_body_t) + sizeof(uint32_t)) +
                     (uint64_t)MOB_PATH_CACHE_ENTRIES * sizeof(mob_path_entry_t) +
                     (uint64_t)MOB_PATH_ROUND_SEARCHES * sizeof(mob_path_item_t) + sizeof(mob_path_scratch_t) +
                     5ull * MC_CPU_CACHE_LINE_BYTES,
};

/* Takes from the arena, which must outlive it, and starts with nothing asked and nothing cached. The map and the edit log must be
 * open for as long as this runs. With no job system every search runs on the thread calling mob_path_tick. */
void mob_path_start(mob_path_t *path, arena_t *arena, const world_map_t *map, const world_edit_t *edit,
                    job_system_t *job);

/* Waits for every job still out, which by then have nothing left to do. Not on a worker. Safe on zero bytes. */
void mob_path_stop(mob_path_t *path);

/* An ask for the body at this index of the store, from where it is now to a place, with the usual budget and fall. */
void mob_path_ask_for_body(const entity_store_t *store, uint32_t index, const float64_t goal[3], uint8_t goal_radius,
                           mob_path_ask_t *out);

/* Asks for a path. It takes the place of whatever this body asked and has not been answered, keeping that one's turn. The answer
 * arrives at a later mob_path_tick, which may be the next. */
void mob_path_request(mob_path_t *path, entity_handle_t handle, const mob_path_ask_t *ask);

/* Answers what has been asked, as far as this tick's budget goes, and forgets what edits have spoiled. The thread holding the map
 * calls it, and nothing may write the map until it returns. Answers do not depend on how many workers help, or how fast. It waits
 * only for searches already running, never for a job not yet started: with every worker busy it runs them all itself. Never
 * touches the heap. */
void mob_path_tick(mob_path_t *path);

/* This body's last answer, or nullptr if it has none. Good until the next mob_path_tick. */
const mob_path_found_t *mob_path_found(const mob_path_t *path, entity_handle_t handle);

/* How mob_path_follow went. */
typedef enum mob_path_follow_t : uint8_t {
    MOB_PATH_FOLLOW_NONE = 0,    /* it has no way to walk, or is not alive: nothing was set */
    MOB_PATH_FOLLOW_WALKING = 1, /* on its way */
    MOB_PATH_FOLLOW_ARRIVED = 2, /* at the end of a complete path: wish is zero */
    MOB_PATH_FOLLOW_ENDED = 3,   /* at the end of a partial path: wish is zero; time to ask again */
    MOB_PATH_FOLLOW_STUCK = 4,   /* MOB_PATH_STUCK_TICKS with no point reached: wish is zero; time to ask again */
} mob_path_follow_t;

/* Sets this body's wish, jump and yaw for one tick of walking its last answer, at this many blocks per tick. Call once a tick,
 * before entity_tick moves it. */
mob_path_follow_t mob_path_follow(mob_path_t *path, entity_store_t *store, entity_handle_t handle, float64_t speed);

/* The search itself, on any thread, with no state but scratch, so that the same ask of the same cells gives the same bytes. The map
 * must not change during it. Never allocates. */
void mob_path_search(const world_map_t *map, const mob_path_ask_t *ask, mob_path_scratch_t *scratch,
                     mob_path_found_t *out);

/* Between rounds, which is whenever the caller of mob_path_tick is not in it. */
void mob_path_stats_read(const mob_path_t *path, mob_path_stats_t *out);

#endif
