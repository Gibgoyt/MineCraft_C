/* See run/src/heap/README.md. */
#ifndef MC_RUN_HEAP_INDEX_H
#define MC_RUN_HEAP_INDEX_H

#include "machine/index.h"

#include <stdint.h>

enum : uint64_t {
    /* Every region starts on this boundary and is a whole number of these long. */
    HEAP_REGION_ALIGNMENT = (uint64_t)MC_MEMORY_HUGE_PAGE_BYTES,
    /* The most that may be live at once: memory.process_budget_mib in config/machine.yaml. */
    HEAP_BUDGET_BYTES = (uint64_t)MC_MEMORY_PROCESS_BUDGET_MIB << 20,
};

/* One reserved block. The struct field holding this is its one owner, and must pass it to heap_release. */
typedef struct heap_region_t {
    uint8_t *owned_base;
    uint64_t bytes; /* what was asked for, rounded up to HEAP_REGION_ALIGNMENT */
} heap_region_t;
static_assert(sizeof(heap_region_t) == 16);

typedef struct heap_counters_t {
    uint64_t live_bytes;
    uint64_t live_regions;
    uint64_t peak_bytes;           /* highest live_bytes so far */
    uint64_t reserve_calls;        /* including those that failed */
    uint64_t release_calls;        /* not counting a release of an empty region, which does nothing */
    uint64_t failed_reserves;      /* a bad size, over budget, or the OS said no */
    uint64_t huge_advice_failures; /* regions that work but may not get huge pages */
} heap_counters_t;
static_assert(sizeof(heap_counters_t) == 56);

/* Maps zeroed memory, aligned and advised MADV_HUGEPAGE, and fills out. Pages cost nothing until touched. On failure
 * returns false, logs why and leaves out empty. A hot path must not call this: the counters show it if one does. */
bool heap_reserve(uint64_t bytes, heap_region_t *out);

/* Unmaps a region and empties the handle. An empty handle does nothing, so _deinit can call this on a partial _init.
 * Aborts on a handle no heap_reserve could have made, or one whose memory is already gone. */
void heap_release(heap_region_t *region);

/* Each field is exact, but they are read one at a time: only with one thread left do they add up to a single moment. */
void heap_counters_read(heap_counters_t *out);

/* The leak check at exit: logs what is still live and returns false, or returns true if nothing is. */
bool heap_exit_check(void);

#endif
