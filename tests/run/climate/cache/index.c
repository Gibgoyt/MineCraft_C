/* ctest run_climate_cache. See README.md. */
#include "harness/index.h"

#include "climate/cache/index.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    CACHE_WORDS = 256u,
    CACHE_READERS = 8u,
    CACHE_SWAPS = 100000u,
};

static climate_slot_t *cache_slots(test_context_t *t, uint32_t count) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, (uint64_t)count * sizeof(climate_slot_t) + 64u);
    climate_slot_t *const slots = (climate_slot_t *)((raw + 63u) & ~(uintptr_t)63u);
    climate_cache_empty(slots, count);
    return slots;
}

TEST(slots_follow_the_coordinates_modulo_the_side) {
    CHECK_EQ_U64(climate_cache_slot_index(-1, 70, CLIMATE_TILE_SIDE), 447u);
    CHECK_EQ_U64(climate_cache_slot_index(63, 6, CLIMATE_TILE_SIDE), 447u);
    CHECK_EQ_U64(climate_cache_slot_index(0, 0, CLIMATE_TILE_SIDE), 0u);
    CHECK_EQ_U64(climate_cache_slot_index(-64, -64, CLIMATE_TILE_SIDE), 0u);
    CHECK_EQ_U64(climate_cache_slot_index(-1, -1, CLIMATE_MASK_SIDE), CLIMATE_MASK_SLOTS - 1u);
    CHECK(climate_cache_key(-1, 70) != climate_cache_key(63, 6));
    CHECK_EQ_U64(climate_cache_key(-1, 0), 0xFFFFFFFFull);
    CHECK(climate_cache_keeps(31, -31, 0, 0, CLIMATE_TILE_SIDE));
    CHECK(!climate_cache_keeps(32, 0, 0, 0, CLIMATE_TILE_SIDE));
    CHECK(!climate_cache_keeps(0, -32, 0, 0, CLIMATE_TILE_SIDE));
    CHECK(climate_cache_keeps(63, 0, 0, 0, CLIMATE_MASK_SIDE) && !climate_cache_keeps(64, 0, 0, 0, CLIMATE_MASK_SIDE));
}

TEST(a_tile_outside_the_keep_square_is_never_cached) {
    climate_slot_t *const slot = cache_slots(t, 1u);
    const bool keeps = climate_cache_keeps(40, 0, 0, 0, CLIMATE_TILE_SIDE);
    CHECK(!keeps);
    CHECK_EQ_U64(climate_cache_claim(slot, climate_cache_key(40, 0), keeps), CLIMATE_CLAIM_REFUSED);
    CHECK_EQ_U64(atomic_load(&slot->state), CLIMATE_SLOT_EMPTY);
    /* Nor does it push out a kept tile. */
    CHECK_EQ_U64(climate_cache_claim(slot, climate_cache_key(-24, 0), true), CLIMATE_CLAIM_OURS);
    climate_cache_publish(slot);
    CHECK_EQ_U64(climate_cache_claim(slot, climate_cache_key(40, 0), keeps), CLIMATE_CLAIM_REFUSED);
    CHECK(climate_cache_ready(slot, climate_cache_key(-24, 0)));
}

TEST(a_far_tile_is_replaced_by_a_near_one_in_its_slot) {
    climate_slot_t *const slot = cache_slots(t, 1u);
    const uint64_t far = climate_cache_key(0, 0), near = climate_cache_key(64, 0);
    CHECK_EQ_U64(climate_cache_claim(slot, far, true), CLIMATE_CLAIM_OURS);
    CHECK_EQ_U64(climate_cache_claim(slot, far, true), CLIMATE_CLAIM_BUSY);
    climate_cache_publish(slot);
    CHECK_EQ_U64(climate_cache_claim(slot, far, true), CLIMATE_CLAIM_HAVE);
    /* The player is now near (64, 0): (0, 0) is outside the keep square there, and goes. */
    CHECK(climate_cache_keeps(64, 0, 60, 0, CLIMATE_TILE_SIDE) && !climate_cache_keeps(0, 0, 60, 0, CLIMATE_TILE_SIDE));
    CHECK_EQ_U64(climate_cache_claim(slot, near, true), CLIMATE_CLAIM_OURS);
    CHECK(!climate_cache_read_begin(slot, far));
    CHECK(!climate_cache_read_begin(slot, near)); /* claimed, not yet published */
    climate_cache_publish(slot);
    CHECK(climate_cache_read_begin(slot, near));
    climate_cache_read_end(slot);
    CHECK(!climate_cache_read_begin(slot, far));
    CHECK_EQ_U64(atomic_load(&slot->readers), 0u);
    /* An unclaim leaves it empty. */
    climate_slot_t *const other = cache_slots(t, 1u);
    CHECK_EQ_U64(climate_cache_claim(other, far, true), CLIMATE_CLAIM_OURS);
    climate_cache_unclaim(other);
    CHECK_EQ_U64(atomic_load(&other->state), CLIMATE_SLOT_EMPTY);
}

TEST(a_reader_holding_a_slot_stops_its_eviction) {
    climate_slot_t *const slot = cache_slots(t, 1u);
    const uint64_t old_key = climate_cache_key(1, 2), new_key = climate_cache_key(65, 2);
    REQUIRE(climate_cache_claim(slot, old_key, true) == CLIMATE_CLAIM_OURS);
    climate_cache_publish(slot);
    REQUIRE(climate_cache_read_begin(slot, old_key));
    CHECK_EQ_U64(climate_cache_claim(slot, new_key, true), CLIMATE_CLAIM_REFUSED);
    CHECK_EQ_U64(atomic_load(&slot->state), CLIMATE_SLOT_READY);
    CHECK_EQ_U64(atomic_load(&slot->key), old_key);
    climate_cache_read_end(slot);
    CHECK_EQ_U64(climate_cache_claim(slot, new_key, true), CLIMATE_CLAIM_OURS);
    CHECK_EQ_U64(atomic_load(&slot->key), new_key);
}

/* ---- Real threads ------------------------------------------------------------------------------------------------ */

typedef struct cache_race_t {
    climate_slot_t *slot;  /* 0 */
    uint32_t *data;        /* 8: CACHE_WORDS, the slot's tile */
    _Atomic uint32_t done; /* 16: the replacer has finished */
    uint8_t _pad0[4];      /* 20 */
} cache_race_t;
static_assert(sizeof(cache_race_t) == 24);

typedef struct cache_reader_t {
    cache_race_t *race; /* 0 */
    uint64_t hits;      /* 8 */
    uint64_t torn;      /* 16 */
    uint32_t index;     /* 24 */
    uint8_t _pad0[4];   /* 28 */
} cache_reader_t;
static_assert(sizeof(cache_reader_t) == 32);

static uint32_t cache_pattern(uint64_t key, uint32_t word) {
    return (uint32_t)(key ^ (key >> 32)) * 2654435761u + word * 40503u;
}

static void *cache_replacer(void *argument) {
    cache_race_t *const race = argument;
    const uint64_t keys[2] = {climate_cache_key(3, 4), climate_cache_key(3 + 64, 4)};
    for (uint32_t swap = 0u; swap < CACHE_SWAPS; swap++) {
        const uint64_t key = keys[swap & 1u];
        while (climate_cache_claim(race->slot, key, true) != CLIMATE_CLAIM_OURS) {
            /* A reader holds the other one: try again, as the sim does in a later step. */
        }
        for (uint32_t w = 0u; w < CACHE_WORDS; w++) {
            race->data[w] = cache_pattern(key, w);
        }
        climate_cache_publish(race->slot);
    }
    atomic_store_explicit(&race->done, 1u, memory_order_release);
    return nullptr;
}

static void *cache_reader(void *argument) {
    cache_reader_t *const reader = argument;
    cache_race_t *const race = reader->race;
    const uint64_t keys[2] = {climate_cache_key(3, 4), climate_cache_key(3 + 64, 4)};
    uint32_t turn = reader->index;
    while (atomic_load_explicit(&race->done, memory_order_acquire) == 0u) {
        const uint64_t key = keys[turn++ & 1u];
        if (!climate_cache_read_begin(race->slot, key)) {
            continue;
        }
        bool whole = true;
        for (uint32_t w = 0u; w < CACHE_WORDS; w++) {
            whole = whole && race->data[w] == cache_pattern(key, w);
        }
        climate_cache_read_end(race->slot);
        reader->hits++;
        reader->torn += whole ? 0u : 1u;
        /* A pause between reads, as real readers have: eight back to back would hold the slot nearly always, and the
         * protocol gives an evictor no turn of its own, by design (it tries again in a later step). */
        for (uint32_t pause = 0u; pause < 1024u; pause++) {
            if (atomic_load_explicit(&race->done, memory_order_relaxed) != 0u) {
                break;
            }
        }
    }
    return nullptr;
}

TEST(readers_and_evictors_on_real_threads_never_see_torn_tiles) {
    cache_race_t race = {.slot = cache_slots(t, 1u), .data = test_alloc(t, CACHE_WORDS * sizeof(uint32_t))};
    atomic_store(&race.done, 0u);
    cache_reader_t readers[CACHE_READERS];
    pthread_t threads[CACHE_READERS];
    for (uint32_t r = 0u; r < CACHE_READERS; r++) {
        readers[r] = (cache_reader_t){.race = &race, .hits = 0u, .torn = 0u, .index = r, ._pad0 = {0}};
        REQUIRE(pthread_create(&threads[r], nullptr, cache_reader, &readers[r]) == 0);
    }
    pthread_t replacer;
    REQUIRE(pthread_create(&replacer, nullptr, cache_replacer, &race) == 0);
    REQUIRE(pthread_join(replacer, nullptr) == 0);
    uint64_t hits = 0u, torn = 0u;
    for (uint32_t r = 0u; r < CACHE_READERS; r++) {
        REQUIRE(pthread_join(threads[r], nullptr) == 0);
        hits += readers[r].hits;
        torn += readers[r].torn;
    }
    printf("%u swaps; %" PRIu64 " reads held a whole tile, %" PRIu64 " were torn\n", CACHE_SWAPS, hits, torn);
    CHECK_EQ_U64(torn, 0u);
    CHECK(hits > 0u);
    CHECK_EQ_U64(atomic_load(&race.slot->readers), 0u);
}

TEST_MAIN()
