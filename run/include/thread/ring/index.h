/* See run/src/thread/ring/README.md. */
#ifndef MC_RUN_THREAD_RING_INDEX_H
#define MC_RUN_THREAD_RING_INDEX_H

#include "machine/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* A fixed-size queue from exactly one producer thread to exactly one consumer thread, with no locks and no syscalls.
 * Three cache lines, so that the two sides do not slow each other down by writing to the same one. */
typedef struct ring_t {
    /* Written by the producer. tail counts every element ever pushed. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint64_t tail;
    uint64_t producer_seen_head; /* the last head the producer read, so a push need not touch the consumer's line */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 16];

    /* Written by the consumer. head counts every element ever popped. */
    _Atomic uint64_t head;
    uint64_t consumer_seen_tail; /* the same, the other way round */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 16];

    /* Set by ring_init and only read after it. */
    uint8_t *borrowed_storage;
    uint64_t capacity;
    uint64_t element_bytes;
    uint8_t _pad2[MC_CPU_CACHE_LINE_BYTES - 24];
} ring_t;
static_assert(sizeof(ring_t) == 3 * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(ring_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(ring_t, head) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(ring_t, borrowed_storage) == 2 * MC_CPU_CACHE_LINE_BYTES);

/* The bytes of storage ring_init needs, or 0 if these are not valid: capacity must be a power of two, element_bytes
 * above zero, and their product must fit. */
uint64_t ring_storage_bytes(uint64_t capacity, uint64_t element_bytes);

/* Sets up a ring in storage the caller owns, which must outlive it and may have any alignment. Returns false if the
 * sizes are invalid or the storage is too small. Call it before either side's thread starts using the ring. */
bool ring_init(ring_t *ring, void *borrowed_storage, uint64_t storage_bytes, uint64_t capacity, uint64_t element_bytes);
void ring_deinit(ring_t *ring);

/* Producer thread only. Copies the element in, or returns false if the ring is full. A pointer pushed this way is a
 * handoff_: the consumer takes it over and the producer stops touching it. */
bool ring_try_push(ring_t *ring, const void *element);
/* Producer thread only. Copies in as many of count elements as fit, and returns how many. They appear to the consumer
 * all at once. */
uint64_t ring_push_many(ring_t *ring, const void *elements, uint64_t count);

/* Consumer thread only. Copies the oldest element out, or returns false if the ring is empty. */
bool ring_try_pop(ring_t *ring, void *out_element);
/* Consumer thread only. Copies out up to count elements, oldest first, and returns how many. */
uint64_t ring_pop_many(ring_t *ring, void *out_elements, uint64_t count);

/* Any thread. How many elements are in the ring; out of date as soon as it returns, unless both sides have stopped. */
uint64_t ring_count(const ring_t *ring);

#endif
