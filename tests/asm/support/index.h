/* What the tests under tests/asm/ share. See README.md. */
#ifndef MC_TESTS_ASM_SUPPORT_INDEX_H
#define MC_TESTS_ASM_SUPPORT_INDEX_H

#include "harness/index.h"

#include "heap/index.h"

#include <stdint.h>
#include <sys/mman.h>

enum : uint64_t {
    GUARD_PAGE_BYTES = 4096u,
    GUARD_REGION_BYTES = HEAP_REGION_ALIGNMENT,
};

/* A run of memory with an unmapped-in-effect page on each side, so a kernel that reads or writes even one byte past either
 * end dies of SIGSEGV instead of passing. */
typedef struct guard_t {
    heap_region_t owned_region;
    uint8_t *borrowed_low;  /* first usable byte */
    uint8_t *borrowed_high; /* one past the last usable byte */
} guard_t;
static_assert(sizeof(guard_t) == 32);

static inline bool guard_init(guard_t *guard) {
    if (!heap_reserve(GUARD_REGION_BYTES, &guard->owned_region)) {
        return false;
    }
    guard->borrowed_low = guard->owned_region.owned_base + GUARD_PAGE_BYTES;
    guard->borrowed_high = guard->owned_region.owned_base + GUARD_REGION_BYTES - GUARD_PAGE_BYTES;
    return mprotect(guard->owned_region.owned_base, GUARD_PAGE_BYTES, PROT_NONE) == 0 &&
           mprotect(guard->borrowed_high, GUARD_PAGE_BYTES, PROT_NONE) == 0;
}

static inline void guard_deinit(guard_t *guard) {
    (void)mprotect(guard->owned_region.owned_base, GUARD_PAGE_BYTES, PROT_READ | PROT_WRITE);
    (void)mprotect(guard->borrowed_high, GUARD_PAGE_BYTES, PROT_READ | PROT_WRITE);
    heap_release(&guard->owned_region);
}

/* Marsaglia's xorshift64. The state must not start at 0. */
static inline uint64_t test_random(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static inline void test_random_fill(uint64_t *state, uint8_t *bytes, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        bytes[i] = (uint8_t)(test_random(state) >> 32);
    }
}

#endif
