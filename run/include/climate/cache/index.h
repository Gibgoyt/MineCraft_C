/* See run/src/climate/cache/README.md. */
#ifndef MC_RUN_CLIMATE_CACHE_INDEX_H
#define MC_RUN_CLIMATE_CACHE_INDEX_H

#include "machine/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static_assert(MC_CPU_CACHE_LINE_BYTES == 64);

typedef enum climate_slot_state_t : uint32_t {
    CLIMATE_SLOT_EMPTY = 0,
    CLIMATE_SLOT_CLAIMED = 1,  /* someone is building it; key is set */
    CLIMATE_SLOT_READY = 2,    /* its data is complete and matches key */
    CLIMATE_SLOT_EVICTING = 3, /* being taken for another key */
} climate_slot_state_t;

/* One slot of a toroidal table. Its own cache line: workers and the sim write different slots at once. The job fields are
 * written by whoever claimed the slot, before it queues the job that builds it, and read only by that job. */
typedef struct climate_slot_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint64_t key; /* 0: (uint32_t)tile_x | (uint64_t)(uint32_t)tile_y << 32 */
    _Atomic uint32_t state;                                /* 8: climate_slot_state_t */
    _Atomic uint32_t readers;                              /* 12: reads in progress */
    void *borrowed_job_owner;                              /* 16: what the building job works for */
    int32_t job_tile_x;                                    /* 24 */
    int32_t job_tile_y;                                    /* 28 */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 32];           /* 32 */
} climate_slot_t;
static_assert(sizeof(climate_slot_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(climate_slot_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(climate_slot_t, state) == 8);
static_assert(offsetof(climate_slot_t, readers) == 12);
static_assert(offsetof(climate_slot_t, borrowed_job_owner) == 16);

enum : uint32_t {
    CLIMATE_TILE_SIDE = 64u,                                    /* the cells table: 64 km square */
    CLIMATE_TILE_SLOTS = CLIMATE_TILE_SIDE * CLIMATE_TILE_SIDE, /* 4096 */
    CLIMATE_MASK_SIDE = 128u,                                   /* the mask table: 128 km square */
    CLIMATE_MASK_SLOTS = CLIMATE_MASK_SIDE * CLIMATE_MASK_SIDE, /* 16384 */
};
static_assert((CLIMATE_TILE_SIDE & (CLIMATE_TILE_SIDE - 1u)) == 0 &&
              (CLIMATE_MASK_SIDE & (CLIMATE_MASK_SIDE - 1u)) == 0);

/* What claiming a slot for a key came to. */
typedef enum climate_claim_t : uint32_t {
    CLIMATE_CLAIM_HAVE = 0,    /* it is READY with this key already */
    CLIMATE_CLAIM_BUSY = 1,    /* someone else has it claimed, or is evicting it */
    CLIMATE_CLAIM_OURS = 2,    /* claimed: build it, then climate_cache_publish */
    CLIMATE_CLAIM_REFUSED = 3, /* outside the keep square, or a reader holds what is there: build into scratch */
} climate_claim_t;

/* A tile's key: both coordinates, so two tiles that share a slot are told apart. */
uint64_t climate_cache_key(int32_t tile_x, int32_t tile_y);

/* (tile_y mod side) * side + (tile_x mod side), for negative coordinates too. side is a power of two. */
uint32_t climate_cache_slot_index(int32_t tile_x, int32_t tile_y, uint32_t side);

/* Whether a tile is inside the keep square of a table side across, centred on (keep_x, keep_y): |t - keep| < side / 2
 * on both axes. Two tiles in one slot differ by a multiple of side on some axis, so at most one of them is kept. */
bool climate_cache_keeps(int32_t tile_x, int32_t tile_y, int32_t keep_x, int32_t keep_y, uint32_t side);

/* Every slot EMPTY, with no readers. Only while nothing else can touch the table. */
void climate_cache_empty(climate_slot_t *slots, uint32_t count);

/* The read protocol. True if the slot holds key and stays so until climate_cache_read_end, which must then follow; the
 * data may be copied out meanwhile. False if it does not, and then nothing more is to be done. Any thread. */
[[nodiscard]] bool climate_cache_read_begin(climate_slot_t *slot, uint64_t key);
void climate_cache_read_end(climate_slot_t *slot);

/* A look without holding it: whether it was READY with key at that moment. For "is it built", never for reading data. */
bool climate_cache_ready(const climate_slot_t *slot, uint64_t key);

/* Claims a slot for key, taking it from another key if keeps says key is inside the keep square and no reader holds it.
 * Any thread. See the README for each case. */
climate_claim_t climate_cache_claim(climate_slot_t *slot, uint64_t key, bool keeps);

/* After CLIMATE_CLAIM_OURS: the data is complete, so readers may have it. Release. */
void climate_cache_publish(climate_slot_t *slot);

/* After CLIMATE_CLAIM_OURS, for a build that will not happen: back to EMPTY. */
void climate_cache_unclaim(climate_slot_t *slot);

#endif
