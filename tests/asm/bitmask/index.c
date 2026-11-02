/* ctest asm_bitmask. See README.md. */
#include "asm/support/index.h"

#include "simd/bitmask/index.h"
#include "simd/reference/index.h"

#include <string.h>

enum : uint64_t {
    MAX_WORDS = 400u, /* past several of the popcount's widening rounds, which are 124 words each */
    MAX_VALUES = 800u,
    WORD_BITS = 64u,
};
enum : uint64_t { FILLER = 0x5A5A5A5A5A5A5A5Aull };

typedef void (*binary_t)(uint64_t *dst, const uint64_t *a, const uint64_t *b, uint64_t count);

/* Kernighan's count, so the reference's use of the CPU's instruction is checked by something that does not use it. */
static uint64_t slow_popcount(const uint64_t *words, uint64_t count) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t word = words[i];
        while (word != 0u) {
            word &= word - 1u;
            total++;
        }
    }
    return total;
}

/* From a few bits set to many, since bytes that are all ones are what could overflow the kernel's per-byte sums. */
static void fill_words(uint64_t *state, uint64_t *words, uint64_t count) {
    const uint64_t kind = test_random(state) % 4u;
    for (uint64_t i = 0; i < count; i++) {
        const uint64_t value = test_random(state);
        words[i] = kind == 0u   ? value
                   : kind == 1u ? ~0ull
                   : kind == 2u ? value & test_random(state)
                                : value | test_random(state);
    }
}

static uint64_t words_for(uint64_t values) {
    return (values + WORD_BITS - 1u) / WORD_BITS;
}

TEST(popcount_is_right_for_every_count_and_density) {
    uint64_t random_state = 0x1234u;
    uint64_t *words = test_alloc(t, MAX_WORDS * sizeof(uint64_t));
    for (uint64_t count = 0; count <= MAX_WORDS; count++) {
        for (uint32_t round = 0; round < 8u; round++) {
            fill_words(&random_state, words, MAX_WORDS);
            const uint64_t want = slow_popcount(words, count);
            CHECK_EQ_U64(bitmask_popcount_batch(words, count), want);
            CHECK_EQ_U64(simd_reference_bitmask_popcount(words, count), want);
        }
    }
}

TEST(popcount_of_all_ones_does_not_overflow_at_the_widening_boundaries) {
    uint64_t *words = test_alloc(t, MAX_WORDS * sizeof(uint64_t));
    memset(words, 0xFF, MAX_WORDS * sizeof(uint64_t));
    const uint64_t counts[] = {4u, 120u, 124u, 128u, 248u, 252u, MAX_WORDS};
    for (uint64_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        CHECK_EQ_U64(bitmask_popcount_batch(words, counts[i]), counts[i] * WORD_BITS);
    }
}

TEST(popcount_stays_inside_its_buffer_and_can_start_anywhere) {
    guard_t guard;
    REQUIRE(guard_init(&guard));
    uint64_t random_state = 0x4321u;
    for (uint64_t count = 0; count <= MAX_WORDS; count++) {
        const uint64_t bytes = count * sizeof(uint64_t);
        uint64_t *high = (uint64_t *)(void *)(guard.borrowed_high - bytes);
        uint64_t *low = (uint64_t *)(void *)guard.borrowed_low;
        fill_words(&random_state, high, count);
        memcpy(low, high, bytes);
        const uint64_t want = slow_popcount(high, count);
        CHECK_EQ_U64(bitmask_popcount_batch(high, count), want);
        CHECK_EQ_U64(bitmask_popcount_batch(low, count), want);
    }
    guard_deinit(&guard);
}

static void check_binary(test_context_t *t, binary_t kernel, binary_t reference) {
    uint64_t random_state = 0x8888u;
    uint64_t *a = test_alloc(t, (MAX_WORDS + 8u) * sizeof(uint64_t));
    uint64_t *b = test_alloc(t, (MAX_WORDS + 8u) * sizeof(uint64_t));
    uint64_t *got = test_alloc(t, (MAX_WORDS + 8u) * sizeof(uint64_t));
    uint64_t *want = test_alloc(t, (MAX_WORDS + 8u) * sizeof(uint64_t));
    for (uint64_t count = 0; count <= MAX_WORDS; count++) {
        fill_words(&random_state, a, MAX_WORDS);
        fill_words(&random_state, b, MAX_WORDS);
        for (uint64_t i = 0; i < MAX_WORDS + 8u; i++) {
            got[i] = FILLER;
            want[i] = FILLER;
        }
        kernel(got, a, b, count);
        reference(want, a, b, count);
        CHECK_MEM_EQ(got, want, (MAX_WORDS + 8u) * sizeof(uint64_t));

        /* dst may be either input. */
        memcpy(got, a, MAX_WORDS * sizeof(uint64_t));
        kernel(got, got, b, count);
        CHECK_MEM_EQ(got, want, count * sizeof(uint64_t));
        memcpy(got, b, MAX_WORDS * sizeof(uint64_t));
        kernel(got, a, got, count);
        CHECK_MEM_EQ(got, want, count * sizeof(uint64_t));
    }
}

TEST(and_matches_its_reference) {
    check_binary(t, bitmask_and_batch, simd_reference_bitmask_and);
}

TEST(or_matches_its_reference) {
    check_binary(t, bitmask_or_batch, simd_reference_bitmask_or);
}

TEST(andnot_matches_its_reference) {
    check_binary(t, bitmask_andnot_batch, simd_reference_bitmask_andnot);
}

TEST(references_of_the_word_kernels_give_the_known_answers) {
    const uint64_t a = 0b1100u;
    const uint64_t b = 0b1010u;
    uint64_t out = 0;
    simd_reference_bitmask_and(&out, &a, &b, 1u);
    CHECK_EQ_U64(out, 0b1000u);
    simd_reference_bitmask_or(&out, &a, &b, 1u);
    CHECK_EQ_U64(out, 0b1110u);
    simd_reference_bitmask_andnot(&out, &a, &b, 1u);
    CHECK_EQ_U64(out, 0b0100u);
}

TEST(word_kernels_stay_inside_their_buffers) {
    guard_t guard_a;
    guard_t guard_b;
    guard_t guard_dst;
    REQUIRE(guard_init(&guard_a));
    REQUIRE(guard_init(&guard_b));
    REQUIRE(guard_init(&guard_dst));
    uint64_t random_state = 0x9999u;
    for (uint64_t count = 0; count <= 64u; count++) {
        const uint64_t bytes = count * sizeof(uint64_t);
        uint64_t *a = (uint64_t *)(void *)(guard_a.borrowed_high - bytes);
        uint64_t *b = (uint64_t *)(void *)(guard_b.borrowed_high - bytes);
        uint64_t *dst = (uint64_t *)(void *)(guard_dst.borrowed_high - bytes);
        fill_words(&random_state, a, count);
        fill_words(&random_state, b, count);
        bitmask_and_batch(dst, a, b, count);
        bitmask_or_batch(dst, a, b, count);
        bitmask_andnot_batch(dst, a, b, count);
        for (uint64_t i = 0; i < count; i++) {
            CHECK_EQ_U64(dst[i], a[i] & ~b[i]);
        }
    }
    guard_deinit(&guard_dst);
    guard_deinit(&guard_b);
    guard_deinit(&guard_a);
}

TEST(from_nonzero_u8_matches_its_reference_at_every_count) {
    uint64_t random_state = 0xABCDu;
    uint8_t *values = test_alloc(t, MAX_VALUES);
    uint64_t *got = test_alloc(t, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
    uint64_t *want = test_alloc(t, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
    for (uint64_t count = 0; count <= MAX_VALUES; count++) {
        for (uint64_t i = 0; i < MAX_VALUES; i++) {
            /* The top bit alone, the low bit alone, and nothing, so a sign-based test would show up. */
            const uint64_t pick = test_random(&random_state) % 4u;
            values[i] = pick == 0u ? 0x80u : pick == 1u ? 0x01u : 0u;
        }
        for (uint64_t i = 0; i < words_for(MAX_VALUES) + 2u; i++) {
            got[i] = FILLER;
            want[i] = FILLER;
        }
        bitmask_from_nonzero_u8_batch(values, count, got);
        simd_reference_bitmask_from_nonzero_u8(values, count, want);
        CHECK_MEM_EQ(got, want, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
        /* Against the words as defined, not just the reference. */
        for (uint64_t i = 0; i < count; i++) {
            CHECK_EQ_U64((got[i / WORD_BITS] >> (i % WORD_BITS)) & 1u, values[i] != 0u);
        }
    }
}

TEST(from_u16_kernels_match_their_references_at_every_count) {
    uint64_t random_state = 0xDCBAu;
    uint16_t *values = test_alloc(t, MAX_VALUES * sizeof(uint16_t));
    uint64_t *got = test_alloc(t, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
    uint64_t *want = test_alloc(t, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
    const uint16_t looking_for = 0x8001u; /* both of whose bytes are set, and whose sign would catch signed compares */
    for (uint64_t count = 0; count <= MAX_VALUES; count++) {
        for (uint64_t i = 0; i < MAX_VALUES; i++) {
            const uint64_t pick = test_random(&random_state) % 6u;
            values[i] = pick == 0u   ? 0x0100u
                        : pick == 1u ? 0x0001u
                        : pick == 2u ? 0x8000u
                        : pick == 3u ? looking_for
                                     : 0u;
        }
        for (uint64_t i = 0; i < words_for(MAX_VALUES) + 2u; i++) {
            got[i] = FILLER;
            want[i] = FILLER;
        }
        bitmask_from_nonzero_u16_batch(values, count, got);
        simd_reference_bitmask_from_nonzero_u16(values, count, want);
        CHECK_MEM_EQ(got, want, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
        for (uint64_t i = 0; i < count; i++) {
            CHECK_EQ_U64((got[i / WORD_BITS] >> (i % WORD_BITS)) & 1u, values[i] != 0u);
        }
        bitmask_from_equal_u16_batch(values, count, looking_for, got);
        simd_reference_bitmask_from_equal_u16(values, count, looking_for, want);
        CHECK_MEM_EQ(got, want, (words_for(MAX_VALUES) + 2u) * sizeof(uint64_t));
        for (uint64_t i = 0; i < count; i++) {
            CHECK_EQ_U64((got[i / WORD_BITS] >> (i % WORD_BITS)) & 1u, values[i] == looking_for);
        }
    }
}

TEST(from_kernels_stay_inside_their_buffers) {
    guard_t guard_values;
    guard_t guard_out;
    REQUIRE(guard_init(&guard_values));
    REQUIRE(guard_init(&guard_out));
    for (uint64_t count = 0; count <= 300u; count++) {
        const uint64_t out_bytes = words_for(count) * sizeof(uint64_t);
        uint64_t *out = (uint64_t *)(void *)(guard_out.borrowed_high - out_bytes);
        uint8_t *bytes = guard_values.borrowed_high - count;
        uint16_t *states = (uint16_t *)(void *)(guard_values.borrowed_high - count * sizeof(uint16_t));
        memset(states, 0x11, count * sizeof(uint16_t));
        bitmask_from_nonzero_u16_batch(states, count, out);
        bitmask_from_equal_u16_batch(states, count, 0x1111u, out);
        if (count > 0u) {
            CHECK_EQ_U64(simd_reference_bitmask_popcount(out, words_for(count)), count);
        }
        bitmask_from_nonzero_u8_batch(bytes, count, out);
        if (count > 0u) {
            CHECK_EQ_U64(simd_reference_bitmask_popcount(out, words_for(count)), count);
        }
    }
    guard_deinit(&guard_out);
    guard_deinit(&guard_values);
}

TEST_MAIN()
