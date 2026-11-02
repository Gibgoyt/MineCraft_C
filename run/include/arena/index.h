/* See run/src/arena/README.md. */
#ifndef MC_RUN_ARENA_INDEX_H
#define MC_RUN_ARENA_INDEX_H

#include <stdint.h>

/* A bump allocator over memory someone else holds. One thread at a time: there are no atomics here, which is the point,
 * so each thread has its own. Aligned so that an array of per-thread arenas shares no cache line. The fields may be
 * read directly; only these functions write them. */
typedef struct arena_t {
    alignas(64) uint8_t *borrowed_base;
    const char *borrowed_name; /* for messages; must outlive the arena */
    uint64_t capacity;
    uint64_t used;
    uint64_t peak;         /* highest used since arena_init; a reset does not lower it */
    uint64_t alloc_count;  /* successful allocations since arena_init, to see what a hot path costs */
    uint64_t failed_count; /* refused for lack of room */
    uint64_t reset_count;
} arena_t;
static_assert(sizeof(arena_t) == 64);
static_assert(alignof(arena_t) == 64);

/* Starts an empty arena over memory: usually the whole of a heap_region_t, or a slice of one to split it between arenas.
 * It takes no ownership. Whoever called heap_reserve still calls heap_release, after arena_deinit. */
void arena_init(arena_t *arena, const char *name, uint8_t *memory, uint64_t capacity);

/* Empties the handle and hands the memory back whole, so it may be used again. An empty handle does nothing. */
void arena_deinit(arena_t *arena);

/* Returns bytes of memory at a power-of-two alignment. Contents are not defined, since a reset does not clear them. Zero
 * bytes gives an aligned pointer that must not be read. Each arena has a fixed budget for one lifetime, so running out is
 * a bug in that budget: this logs the arena's name and aborts, and so never returns nullptr. */
void *arena_alloc(arena_t *arena, uint64_t bytes, uint64_t alignment);

/* The same, for a caller with a plan for running out. On failure returns false, sets *out to nullptr and takes nothing. */
bool arena_alloc_try(arena_t *arena, uint64_t bytes, uint64_t alignment, void **out);

/* arena_alloc, filled with zeros. */
void *arena_alloc_zeroed(arena_t *arena, uint64_t bytes, uint64_t alignment);

/* arena_alloc for count elements. Aborts as for running out if the size does not fit in 64 bits. */
void *arena_alloc_array(arena_t *arena, uint64_t count, uint64_t element_bytes, uint64_t alignment);

/* Takes back everything at once, at the arena's lifetime boundary. Every earlier pointer from it is dead. */
void arena_reset(arena_t *arena);

#endif
