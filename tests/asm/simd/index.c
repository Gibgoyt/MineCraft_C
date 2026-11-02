/* ctest asm_simd. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "simd/index.h"

#include <string.h>

TEST(init_reserves_one_region_and_deinit_gives_it_back) {
    heap_counters_t before;
    heap_counters_t during;
    heap_counters_t after;
    heap_counters_read(&before);
    simd_t simd;
    REQUIRE(simd_init(&simd));
    heap_counters_read(&during);
    CHECK_EQ_U64(during.live_regions, before.live_regions + 1u);
    CHECK_EQ_U64(simd.owned_region.bytes, HEAP_REGION_ALIGNMENT);
    CHECK_EQ_U64(simd.arena_scratch.capacity, simd.owned_region.bytes);
    simd_deinit(&simd);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK(simd.owned_region.owned_base == nullptr);
}

TEST(self_check_passes_on_these_kernels_and_leaves_the_arena_empty) {
    simd_t simd;
    REQUIRE(simd_init(&simd));
    CHECK_EQ_U64(simd.arena_scratch.used, 0u);
    CHECK(simd_self_check(&simd));
    CHECK_EQ_U64(simd.arena_scratch.used, 0u);
    CHECK(simd.arena_scratch.alloc_count > 0u);
    simd_deinit(&simd);
}

TEST(deinit_of_a_zeroed_simd_does_nothing) {
    simd_t simd;
    memset(&simd, 0, sizeof(simd));
    simd_deinit(&simd);
    CHECK(simd.owned_region.owned_base == nullptr);
}

TEST(init_that_cannot_reserve_says_false_and_leaves_nothing) {
    heap_counters_t before;
    heap_counters_read(&before);
    /* Fill the budget, so the next reservation has no room. */
    heap_region_t owned_filler;
    REQUIRE(heap_reserve(HEAP_BUDGET_BYTES - before.live_bytes, &owned_filler));
    simd_t simd;
    CHECK(!simd_init(&simd));
    CHECK(simd.owned_region.owned_base == nullptr);
    simd_deinit(&simd);
    heap_release(&owned_filler);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST_MAIN()
