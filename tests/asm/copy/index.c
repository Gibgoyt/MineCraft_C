/* ctest asm_copy. See README.md. */
#include "asm/support/index.h"

#include "simd/copy/index.h"
#include "simd/reference/index.h"

#include <string.h>

enum : uint64_t {
    MAX_SHORT_BYTES = 600u, /* several blocks past the largest head and tail */
    MAX_OFFSET = 64u,
    FILLER = 0xA5u,
    CANARY_BYTES = 64u,
    FRAME_WIDTH = 1920u,
    FRAME_HEIGHT = 1080u,
    PIXEL_BYTES = 4u,
    FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * PIXEL_BYTES,
};

/* One copy with a stretch of filler on each side, all of which must survive. */
static void check_one(test_context_t *t, uint8_t *arena_dst, const uint8_t *src, uint64_t offset, uint64_t bytes) {
    memset(arena_dst, FILLER, CANARY_BYTES + MAX_OFFSET + bytes + CANARY_BYTES);
    uint8_t *dst = arena_dst + CANARY_BYTES + offset;
    copy_stream_batch(dst, src, bytes);
    if (!CHECK_MEM_EQ(dst, src, bytes)) {
        return;
    }
    for (uint64_t i = 0; i < CANARY_BYTES + offset; i++) {
        CHECK_EQ_U64(arena_dst[i], FILLER);
    }
    for (uint64_t i = 0; i < MAX_OFFSET - offset + CANARY_BYTES; i++) {
        CHECK_EQ_U64(dst[bytes + i], FILLER);
    }
}

TEST(reference_copies_and_leaves_the_rest) {
    const uint8_t src[4] = {1u, 2u, 3u, 4u};
    uint8_t dst[6] = {9u, 9u, 9u, 9u, 9u, 9u};
    simd_reference_copy_stream(dst + 1, src, 4u);
    const uint8_t want[6] = {9u, 1u, 2u, 3u, 4u, 9u};
    CHECK_MEM_EQ(dst, want, 6u);
}

TEST(every_short_length_at_every_alignment_of_both_sides) {
    uint64_t random_state = 0x123456789ull;
    uint8_t *src_base = test_alloc(t, MAX_SHORT_BYTES + MAX_OFFSET);
    uint8_t *dst_base = test_alloc(t, CANARY_BYTES + MAX_OFFSET + MAX_SHORT_BYTES + CANARY_BYTES);
    test_random_fill(&random_state, src_base, MAX_SHORT_BYTES + MAX_OFFSET);
    for (uint64_t bytes = 0; bytes <= MAX_SHORT_BYTES; bytes++) {
        for (uint64_t offset = 0; offset < MAX_OFFSET; offset += 1u + offset / 16u) {
            check_one(t, dst_base, src_base + (bytes + offset) % MAX_OFFSET, offset, bytes);
        }
    }
}

TEST(touches_nothing_past_either_end_of_either_buffer) {
    guard_t src_guard;
    guard_t dst_guard;
    REQUIRE(guard_init(&src_guard));
    REQUIRE(guard_init(&dst_guard));
    uint64_t random_state = 0x55AA55AAull;
    uint8_t *want = test_alloc(t, MAX_SHORT_BYTES + MAX_OFFSET);
    test_random_fill(&random_state, want, MAX_SHORT_BYTES + MAX_OFFSET);
    for (uint64_t bytes = 1; bytes <= MAX_SHORT_BYTES; bytes += 1u + bytes / 64u) {
        /* Both buffers end on the page that faults. */
        uint8_t *src_high = src_guard.borrowed_high - bytes;
        uint8_t *dst_high = dst_guard.borrowed_high - bytes;
        memcpy(src_high, want, bytes);
        copy_stream_batch(dst_high, src_high, bytes);
        CHECK_MEM_EQ(dst_high, want, bytes);
        /* And both start right after one. */
        memcpy(src_guard.borrowed_low, want, bytes);
        copy_stream_batch(dst_guard.borrowed_low, src_guard.borrowed_low, bytes);
        CHECK_MEM_EQ(dst_guard.borrowed_low, want, bytes);
    }
    guard_deinit(&dst_guard);
    guard_deinit(&src_guard);
}

TEST(rows_step_by_each_pitch_and_leave_the_gaps_alone) {
    uint64_t random_state = 0x777ull;
    const uint64_t rows = 40u;
    const uint64_t row_bytes = 333u;
    const uint64_t src_pitch = 350u;
    const uint64_t dst_pitch = 512u;
    uint8_t *src = test_alloc(t, rows * src_pitch);
    uint8_t *got = test_alloc(t, rows * dst_pitch);
    uint8_t *want = test_alloc(t, rows * dst_pitch);
    test_random_fill(&random_state, src, rows * src_pitch);
    memset(got, FILLER, rows * dst_pitch);
    memset(want, FILLER, rows * dst_pitch);
    copy_stream_rows_batch(got + 3, dst_pitch, src + 1, src_pitch, row_bytes, rows);
    simd_reference_copy_stream_rows(want + 3, dst_pitch, src + 1, src_pitch, row_bytes, rows);
    CHECK_MEM_EQ(got, want, rows * dst_pitch);
}

TEST(no_rows_and_empty_rows_do_nothing) {
    uint8_t src[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    uint8_t dst[8] = {0};
    const uint8_t zero[8] = {0};
    copy_stream_rows_batch(dst, 8u, src, 8u, 8u, 0u);
    CHECK_MEM_EQ(dst, zero, 8u);
    copy_stream_rows_batch(dst, 1u, src, 1u, 0u, 8u);
    CHECK_MEM_EQ(dst, zero, 8u);
    copy_stream_batch(dst, src, 0u);
    CHECK_MEM_EQ(dst, zero, 8u);
}

TEST(a_whole_frame_comes_across_intact) {
    uint64_t random_state = 0x1080ull;
    /* Past the test arena's habit of being small: a real frame, in its own region. */
    heap_region_t owned_src;
    heap_region_t owned_dst;
    REQUIRE(heap_reserve(FRAME_BYTES, &owned_src));
    REQUIRE(heap_reserve(FRAME_BYTES, &owned_dst));
    for (uint64_t i = 0; i < FRAME_BYTES / sizeof(uint64_t); i++) {
        const uint64_t value = test_random(&random_state);
        memcpy(owned_src.owned_base + i * sizeof(uint64_t), &value, sizeof(uint64_t));
    }
    copy_stream_rows_batch(owned_dst.owned_base, FRAME_WIDTH * PIXEL_BYTES, owned_src.owned_base,
                           FRAME_WIDTH * PIXEL_BYTES, FRAME_WIDTH * PIXEL_BYTES, FRAME_HEIGHT);
    CHECK_MEM_EQ(owned_dst.owned_base, owned_src.owned_base, FRAME_BYTES);
    heap_release(&owned_dst);
    heap_release(&owned_src);
}

TEST_MAIN()
