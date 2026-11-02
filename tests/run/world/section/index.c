/* ctest run_world_section. See README.md. */
#include "harness/index.h"

#include "world/section/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    SECTION_TEST_ARENA_BYTES = 64ull << 20,
    SECTION_TEST_STRUCT_BYTES = sizeof(section_t),
    /* Worked out by hand: indices, then 2 * 2^bits of palette and counts, up to a multiple of 64. */
    SECTION_TEST_BLOCK_1_BIT = 576u,  /* 512 + 8 */
    SECTION_TEST_BLOCK_2_BIT = 1088u, /* 1024 + 16 */
    SECTION_TEST_BLOCK_4_BIT = 2112u, /* 2048 + 64 */
    SECTION_TEST_BLOCK_8_BIT = 5120u, /* 4096 + 1024 */
    SECTION_TEST_BLOCK_DIRECT = 8192u,
    SECTION_TEST_LIGHT = 4096u,
};

/* A section, what it should hold, and where its memory comes from. */
typedef struct section_test_t {
    arena_t arena;
    section_pool_t pool;
    section_t section;
    uint16_t expected_blocks[SECTION_VOXELS];
    uint8_t expected_light[SECTION_VOXELS];
    uint16_t exported_blocks[SECTION_VOXELS];
    uint8_t exported_light[SECTION_VOXELS];
    uint64_t rng;
    uint8_t _pad0[56];
} section_test_t;
static_assert(sizeof(section_test_t) == 24832);

/* A section's form for a number of distinct states. */
typedef struct section_test_form_t {
    uint32_t distinct;
    uint32_t bits;
    uint32_t block_bytes;
} section_test_form_t;
static_assert(sizeof(section_test_form_t) == 12);

/* A row of the table of what sections cost. */
typedef struct section_test_cost_t {
    const char *what;
    uint64_t pool_bytes;
    uint32_t distinct;
    uint8_t varied_light;
    uint8_t _pad0[3];
} section_test_cost_t;
static_assert(sizeof(section_test_cost_t) == 24);

static section_test_t *section_test_begin(test_context_t *t, uint64_t arena_bytes) {
    section_test_t *const test = test_alloc(t, sizeof(section_test_t) + 64u);
    section_test_t *const aligned = (section_test_t *)(((uintptr_t)test + 63u) & ~(uintptr_t)63u);
    /* On a line of its own, so that an arena of exactly n bytes holds a block of exactly n. */
    const uintptr_t memory = ((uintptr_t)test_alloc(t, arena_bytes + 64u) + 63u) & ~(uintptr_t)63u;
    arena_init(&aligned->arena, "section_test", (uint8_t *)memory, arena_bytes);
    section_pool_init(&aligned->pool, &aligned->arena);
    aligned->rng = 0x9E3779B97F4A7C15u;
    return aligned;
}

/* Hands the arena's memory back to the harness: under ASan the arena has poisoned what it has not given out. */
static void section_test_end(section_test_t *test) {
    arena_deinit(&test->arena);
}

/* xorshift64. Fixed seeds, so a failure happens again. */
static uint64_t section_test_random(section_test_t *test) {
    uint64_t x = test->rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    test->rng = x;
    return x;
}

/* What a section claims must be what its bytes say, for every form. */
static void section_test_check_structure(test_context_t *t, const section_t *section) {
    const uint32_t bits = section->bits;
    CHECK(bits == 0u || bits == 1u || bits == 2u || bits == 4u || bits == 8u || bits == 16u);
    CHECK_EQ_U64(section->borrowed_storage == nullptr, bits == SECTION_BITS_UNIFORM);
    if (bits == SECTION_BITS_UNIFORM || bits == SECTION_BITS_DIRECT) {
        return;
    }
    const uint16_t *const palette = section_palette(section);
    const uint16_t *const counts = palette + section_palette_capacity(section);
    uint32_t real_counts[256] = {0};
    const uint64_t *const words = (const uint64_t *)(const void *)section->borrowed_storage;
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        const uint32_t bit = i * bits;
        real_counts[(words[bit >> 6] >> (bit & 63u)) & ((1u << bits) - 1u)]++;
    }
    uint32_t used = 0;
    for (uint32_t slot = 0; slot < section_palette_capacity(section); slot++) {
        CHECK_EQ_U64(counts[slot], real_counts[slot]);
        used += counts[slot] != 0u ? 1u : 0u;
        for (uint32_t other = 0; other < slot; other++) {
            /* A state in use is in one slot, or a voxel of it could not be found by the search that keeps it counted. */
            CHECK(counts[slot] == 0u || counts[other] == 0u || palette[slot] != palette[other]);
        }
    }
    CHECK_EQ_U64(used, section->palette_used);
    CHECK(used >= 2u); /* one is uniform, which has no block */
}

static void section_test_check_all(test_context_t *t, section_test_t *test) {
    section_blocks_export(&test->section, test->exported_blocks);
    section_lights_export(&test->section, test->exported_light);
    CHECK_MEM_EQ(test->exported_blocks, test->expected_blocks, sizeof(test->exported_blocks));
    CHECK_MEM_EQ(test->exported_light, test->expected_light, sizeof(test->exported_light));
    bool same = true;
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        same = same && section_block_get(&test->section, i) == test->expected_blocks[i];
        same = same && section_light_get(&test->section, i) == test->expected_light[i];
    }
    CHECK(same);
    section_test_check_structure(t, &test->section);
    CHECK_EQ_U64(section_pool_bytes(&test->section), test->pool.live_bytes);
}

static void section_test_set(test_context_t *t, section_test_t *test, uint32_t index, uint16_t state) {
    CHECK(section_block_set(&test->section, &test->pool, index, state));
    test->expected_blocks[index] = state;
}

/* ---- Forms ------------------------------------------------------------------------------------------------------- */

TEST(zero_bytes_are_an_empty_section_with_nothing_to_release) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_check_all(t, test);
    CHECK_EQ_U64(section_pool_bytes(&test->section), 0u);
    section_deinit(&test->section, &test->pool);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

TEST(a_voxel_has_one_place_and_every_place_is_a_voxel) {
    CHECK_EQ_U64(SECTION_VOXELS, 4096u);
    CHECK_EQ_U64(section_voxel_index(0, 0, 0), 0u);
    CHECK_EQ_U64(section_voxel_index(1, 0, 0), 1u);
    CHECK_EQ_U64(section_voxel_index(0, 1, 0), 16u);
    CHECK_EQ_U64(section_voxel_index(0, 0, 1), 256u);
    CHECK_EQ_U64(section_voxel_index(15, 15, 15), 4095u);
    uint8_t seen[SECTION_VOXELS] = {0};
    for (uint32_t z = 0; z < SECTION_SIZE; z++) {
        for (uint32_t y = 0; y < SECTION_SIZE; y++) {
            for (uint32_t x = 0; x < SECTION_SIZE; x++) {
                seen[section_voxel_index(x, y, z)]++;
            }
        }
    }
    bool once = true;
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        once = once && seen[i] == 1u;
    }
    CHECK(once);
}

TEST(a_uniform_section_has_no_arrays_and_writing_its_own_state_makes_none) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_init(&test->section, 77u, section_light_pack(3u, 9u));
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_blocks[i] = 77u;
        test->expected_light[i] = section_light_pack(3u, 9u);
        CHECK(section_block_set(&test->section, &test->pool, i, 77u));
        CHECK(section_light_set(&test->section, &test->pool, i, section_light_pack(3u, 9u)));
    }
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_storage == nullptr);
    CHECK(test->section.borrowed_light == nullptr);
    CHECK_EQ_U64(test->pool.carved_bytes, 0u);
    CHECK_EQ_U64(test->arena.used, 0u);
    section_test_end(test);
}

TEST(one_different_voxel_makes_an_array_and_undoing_it_returns_to_none) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 1234u, 5u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 1u);
    CHECK_EQ_U64(section_pool_bytes(&test->section), SECTION_TEST_BLOCK_1_BIT);
    section_test_set(t, test, 1234u, 0u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    CHECK_EQ_U64(test->section.block_uniform, 0u);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

TEST(overwriting_all_but_the_last_other_state_leaves_that_state_uniform) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 0u, 9u);
    for (uint32_t i = 1; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, 9u);
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    CHECK_EQ_U64(test->section.block_uniform, 9u);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

TEST(each_new_state_past_a_palette_widens_it_and_values_survive) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    /* state 0 is already there: after n new states there are n + 1. */
    static const section_test_form_t steps[] = {
        {1, 1, 0}, {2, 2, 0}, {3, 2, 0}, {4, 4, 0}, {15, 4, 0}, {16, 8, 0}, {255, 8, 0}, {256, 16, 0}, {1000, 16, 0},
    };
    uint32_t done = 0;
    for (uint32_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        for (; done < steps[s].distinct; done++) {
            /* A state far from its voxel's number, so that a confusion of the two shows. */
            section_test_set(t, test, done * 3u, (uint16_t)(done * 37u + 1u));
        }
        section_test_check_all(t, test);
        CHECK_EQ_U64(test->section.bits, steps[s].bits);
    }
    section_test_end(test);
}

TEST(losing_states_narrows_the_palette_but_only_with_room_to_spare) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    for (uint32_t i = 0; i < 16u; i++) {
        section_test_set(t, test, i, (uint16_t)(i + 1u)); /* 17 with air */
    }
    CHECK_EQ_U64(test->section.bits, 8u);
    /* Down to 9 states: a 4-bit palette would hold them and be nearly full, so it stays. */
    for (uint32_t i = 8; i < 16u; i++) {
        section_test_set(t, test, i, 0u);
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 8u);
    /* 8 states is half of 16. */
    section_test_set(t, test, 7u, 0u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 4u);
    CHECK_EQ_U64(section_pool_bytes(&test->section), SECTION_TEST_BLOCK_4_BIT);
    /* 3 stays; 2 is half of 4. */
    for (uint32_t i = 2; i < 7u; i++) {
        section_test_set(t, test, i, 0u);
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 4u);
    section_test_set(t, test, 1u, 0u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 2u);
    CHECK_EQ_U64(test->section.palette_used, 2u);
    section_test_set(t, test, 0u, 0u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    section_test_end(test);
}

TEST(a_full_palette_reuses_the_slot_of_a_voxel_that_was_its_last) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 0u, 1u); /* air and 1 fill a 1-bit palette */
    const uint8_t *const before = test->section.borrowed_storage;
    section_test_set(t, test, 0u, 2u); /* 1 had only this voxel */
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 1u);
    CHECK(test->section.borrowed_storage == before);
    section_test_end(test);
}

TEST(a_palette_slot_left_empty_is_used_again_and_its_old_state_is_not_found) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 0u, 1u);
    section_test_set(t, test, 1u, 2u);
    section_test_set(t, test, 2u, 3u); /* air, 1, 2, 3: 2 bits and full */
    section_test_set(t, test, 1u, 0u); /* 2's slot is empty, but still says 2 */
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 2u);
    CHECK_EQ_U64(test->section.palette_used, 3u);
    section_test_set(t, test, 5u, 2u); /* so 2 must be put back and counted */
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.palette_used, 4u);
    section_test_set(t, test, 6u, 40u); /* full, and no slot to reuse: wider */
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 4u);
    section_test_end(test);
}

TEST(a_direct_section_returns_to_a_palette_or_uniform_when_looked_at) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    for (uint32_t i = 0; i < 300u; i++) {
        section_test_set(t, test, i, (uint16_t)(i + 1u));
    }
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_DIRECT);
    /* A palette would need to have room: 129 states are too many, 128 are not. */
    for (uint32_t i = 128; i < 300u; i++) {
        section_test_set(t, test, i, 0u);
    }
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_DIRECT);
    section_test_set(t, test, 127u, 0u);
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 8u);
    CHECK_EQ_U64(test->section.palette_used, 128u);

    /* And by itself, at the 4096th write: nobody calls compact here. */
    for (uint32_t i = 0; i < 300u; i++) {
        section_test_set(t, test, i, (uint16_t)(i + 1u));
    }
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_DIRECT);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, 500u);
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    CHECK_EQ_U64(test->section.block_uniform, 500u);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

/* ---- Light ------------------------------------------------------------------------------------------------------- */

TEST(sky_and_block_light_share_a_byte_and_keep_to_their_halves) {
    for (uint32_t sky = 0; sky <= SECTION_LIGHT_MAX; sky++) {
        for (uint32_t block = 0; block <= SECTION_LIGHT_MAX; block++) {
            const uint8_t packed = section_light_pack(sky, block);
            CHECK_EQ_U64(section_light_sky(packed), sky);
            CHECK_EQ_U64(section_light_block(packed), block);
        }
    }
    CHECK_EQ_U64(SECTION_LIGHT_OPEN_SKY, section_light_pack(15u, 0u));
}

TEST(light_makes_its_array_on_the_first_difference_and_is_independent_of_blocks) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_init(&test->section, 3u, SECTION_LIGHT_OPEN_SKY);
    memset(test->expected_light, SECTION_LIGHT_OPEN_SKY, SECTION_VOXELS);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_blocks[i] = 3u;
    }
    CHECK(section_light_set(&test->section, &test->pool, 99u, section_light_pack(14u, 2u)));
    test->expected_light[99] = section_light_pack(14u, 2u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM); /* a lit block is still a uniform one */
    CHECK_EQ_U64(section_pool_bytes(&test->section), SECTION_TEST_LIGHT);
    CHECK(section_light_set(&test->section, &test->pool, 99u, SECTION_LIGHT_OPEN_SKY));
    test->expected_light[99] = SECTION_LIGHT_OPEN_SKY;
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_light == nullptr);
    CHECK_EQ_U64(test->section.light_uniform, SECTION_LIGHT_OPEN_SKY);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

TEST(light_that_became_uniform_is_found_at_the_4096th_write_without_being_asked) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    /* Every voxel from 0 to 7: the 4096th write is the first that leaves them all the same. */
    for (uint32_t i = 0; i < SECTION_VOXELS - 1u; i++) {
        CHECK(section_light_set(&test->section, &test->pool, i, 7u));
        test->expected_light[i] = 7u;
    }
    CHECK(test->section.borrowed_light != nullptr);
    section_test_check_all(t, test);
    CHECK(section_light_set(&test->section, &test->pool, SECTION_VOXELS - 1u, 7u));
    test->expected_light[SECTION_VOXELS - 1u] = 7u;
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_light == nullptr);
    CHECK_EQ_U64(test->section.light_uniform, 7u);
    CHECK_EQ_U64(test->section.scan_writes, 0u);
    section_test_end(test);
}

TEST(light_differing_only_in_its_last_voxel_is_not_uniform) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_light[i] = 7u;
    }
    test->expected_light[SECTION_VOXELS - 1u] = 8u;
    CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_light != nullptr);
    section_test_end(test);
}

/* A look costs 4096 voxels, and is paid for by writes that changed something: writing what is there must not bring it on. */
TEST(writes_that_change_nothing_do_not_count_towards_a_look) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    for (uint32_t i = 0; i < 300u; i++) {
        section_test_set(t, test, i, (uint16_t)(i + 1u));
    }
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_DIRECT);
    const uint32_t writes_before = test->section.scan_writes;
    section_test_set(t, test, 0u, test->expected_blocks[0]);
    CHECK_EQ_U64(test->section.scan_writes, writes_before);
    section_test_set(t, test, 0u, 9999u);
    CHECK_EQ_U64(test->section.scan_writes, writes_before + 1u);

    CHECK(section_light_set(&test->section, &test->pool, 5u, 3u));
    test->expected_light[5] = 3u;
    const uint32_t light_writes_before = test->section.scan_writes;
    CHECK(section_light_set(&test->section, &test->pool, 5u, 3u));
    CHECK_EQ_U64(test->section.scan_writes, light_writes_before);
    CHECK(section_light_set(&test->section, &test->pool, 5u, 4u));
    test->expected_light[5] = 4u;
    CHECK_EQ_U64(test->section.scan_writes, light_writes_before + 1u);
    section_test_check_all(t, test);
    section_test_end(test);
}

/* ---- Fills, imports and exports ----------------------------------------------------------------------------------- */

TEST(a_fill_returns_the_arrays) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 0u, 1u);
    CHECK(section_light_set(&test->section, &test->pool, 0u, 1u));
    CHECK(test->pool.live_bytes != 0u);
    section_block_fill(&test->section, &test->pool, 8u);
    section_light_fill(&test->section, &test->pool, 6u);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    CHECK_EQ_U64(section_block_get(&test->section, 4095u), 8u);
    CHECK_EQ_U64(section_light_get(&test->section, 4095u), 6u);
    section_test_end(test);
}

TEST(an_import_stores_the_smallest_form_and_reads_back_the_same) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    static const section_test_form_t cases[] = {
        {1, SECTION_BITS_UNIFORM, 0},
        {2, 1, SECTION_TEST_BLOCK_1_BIT},
        {3, 2, SECTION_TEST_BLOCK_2_BIT},
        {4, 2, SECTION_TEST_BLOCK_2_BIT},
        {5, 4, SECTION_TEST_BLOCK_4_BIT},
        {16, 4, SECTION_TEST_BLOCK_4_BIT},
        {17, 8, SECTION_TEST_BLOCK_8_BIT},
        {256, 8, SECTION_TEST_BLOCK_8_BIT},
        {257, SECTION_BITS_DIRECT, SECTION_TEST_BLOCK_DIRECT},
        {4096, SECTION_BITS_DIRECT, SECTION_TEST_BLOCK_DIRECT},
    };
    for (uint32_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            /* Every state appears, and far from one another across the range. */
            test->expected_blocks[i] = (uint16_t)((i % cases[c].distinct) * 16u + 3u);
        }
        CHECK(section_blocks_import(&test->section, &test->pool, test->expected_blocks));
        section_test_check_all(t, test);
        CHECK_EQ_U64(test->section.bits, cases[c].bits);
        CHECK_EQ_U64(section_pool_bytes(&test->section), cases[c].block_bytes);
    }
    /* Then editing it carries on as if it had been built by edits. */
    section_test_set(t, test, 17u, 0xFFFFu);
    section_test_check_all(t, test);
    section_deinit(&test->section, &test->pool);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    section_test_end(test);
}

TEST(an_import_of_many_colliding_states_finds_each) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    /* 256 states that are the same in the bits the table hashes least: the probe has to work. */
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_blocks[i] = (uint16_t)((i % 256u) << 8);
    }
    CHECK(section_blocks_import(&test->section, &test->pool, test->expected_blocks));
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 8u);
    section_test_end(test);
}

TEST(a_light_import_of_one_value_makes_no_array) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_light[i] = (uint8_t)i;
    }
    CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
    section_test_check_all(t, test);
    CHECK_EQ_U64(section_pool_bytes(&test->section), SECTION_TEST_LIGHT);
    /* Into the array it already has. */
    test->expected_light[0] = 200u;
    CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->pool.live_blocks[5], 1u);
    memset(test->expected_light, 11, SECTION_VOXELS);
    CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_light == nullptr);
    /* The last byte alone differing is still a difference. */
    test->expected_light[SECTION_VOXELS - 1u] = 12u;
    CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
    section_test_check_all(t, test);
    CHECK(test->section.borrowed_light != nullptr);
    section_test_end(test);
}

/* ---- Random edits against a plain array --------------------------------------------------------------------------- */

/* A write goes to a random voxel, or to a region of 256 so that some states die out and forms narrow again. */
static void section_test_random_edits(test_context_t *t, uint64_t seed, uint32_t state_count, uint32_t widest_bits) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    test->rng = seed;
    uint32_t forms_seen = 0;
    for (uint32_t edit = 0; edit < 40000u; edit++) {
        const uint64_t r = section_test_random(test);
        const uint32_t index = (uint32_t)(r % SECTION_VOXELS);
        const uint16_t state = (uint16_t)(((r >> 16) % state_count) * (65535u / state_count));
        const uint8_t light = (uint8_t)(r >> 40);
        const uint32_t kind = (uint32_t)((r >> 48) % 1024u);
        if (kind == 0u) { /* all of it goes one state, voxel by voxel, down through whatever forms it passes */
            for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
                section_test_set(t, test, i, state);
            }
            /* A palette is uniform by now by its counts. A direct array has none, and has to be looked at. */
            section_compact(&test->section, &test->pool);
            CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
            forms_seen |= 1u << SECTION_BITS_UNIFORM;
        } else if (kind < 16u) { /* a layer goes one state, as a big dig or a flood would */
            for (uint32_t i = 0; i < 256u; i++) {
                section_test_set(t, test, (index & 0xF00u) | i, state);
            }
        } else if (kind < 128u) {
            CHECK(section_light_set(&test->section, &test->pool, index, light));
            test->expected_light[index] = light;
        } else if (kind < 256u) { /* the same dark value, as most voxels of a light pass get */
            CHECK(section_light_set(&test->section, &test->pool, index, 0u));
            test->expected_light[index] = 0u;
        } else {
            section_test_set(t, test, index, state);
        }
        CHECK_EQ_U64(section_block_get(&test->section, index), test->expected_blocks[index]);
        forms_seen |= 1u << test->section.bits;
        if (edit % 1024u == 0u) {
            section_test_check_all(t, test);
        }
        if (t->failure_count != 0u) {
            section_test_end(test);
            return; /* one seed's first failure says it; the next thousand do not */
        }
    }
    section_test_check_all(t, test);
    /* Everything comes back: nothing is left over or counted twice. */
    section_deinit(&test->section, &test->pool);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    uint32_t live = 0;
    for (uint32_t c = 0; c < SECTION_CLASS_COUNT; c++) {
        live += test->pool.live_blocks[c];
    }
    CHECK_EQ_U64(live, 0u);
    /* It must have got as far as was meant, or this has tested less than it says. */
    CHECK((forms_seen & (1u << widest_bits)) != 0u);
    CHECK((forms_seen & (1u << SECTION_BITS_UNIFORM)) != 0u);
    section_test_end(test);
}

TEST(random_edits_of_two_states_match_a_plain_array) {
    section_test_random_edits(t, 0x1111u, 2u, 1u);
}
TEST(random_edits_of_five_states_match_a_plain_array) {
    section_test_random_edits(t, 0x2222u, 5u, 4u);
}
TEST(random_edits_of_seventeen_states_match_a_plain_array) {
    section_test_random_edits(t, 0x3333u, 17u, 8u);
}
TEST(random_edits_of_three_hundred_states_match_a_plain_array) {
    section_test_random_edits(t, 0x4444u, 300u, SECTION_BITS_DIRECT);
}
TEST(random_edits_of_any_state_match_a_plain_array) {
    section_test_random_edits(t, 0x5555u, 65535u, SECTION_BITS_DIRECT);
}

/* Few voxels in a few states, so a palette keeps dying down to two states, uniform, and back. */
TEST(random_edits_near_uniform_pass_through_uniform_and_match_a_plain_array) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    uint32_t times_uniform = 0;
    for (uint32_t edit = 0; edit < 200000u; edit++) {
        const uint64_t r = section_test_random(test);
        const uint32_t index = (uint32_t)(r % 8u) * 511u;
        const uint16_t state = (r >> 8) % 4u == 0u ? 1u : 0u;
        section_test_set(t, test, index, state);
        CHECK_EQ_U64(section_block_get(&test->section, index), state);
        times_uniform += test->section.bits == SECTION_BITS_UNIFORM ? 1u : 0u;
    }
    /* All but eight voxels are air, so it is uniform whenever those eight are. */
    section_test_check_all(t, test);
    CHECK(times_uniform > 0u);
    /* Two states need only the smallest block, and each return to uniform gives it back to be used again. */
    CHECK_EQ_U64(test->pool.carved_bytes, SECTION_TEST_BLOCK_1_BIT);
    section_test_end(test);
}

/* ---- Running out, and using memory again --------------------------------------------------------------------------- */

TEST(with_no_room_a_write_says_so_and_changes_nothing) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_BLOCK_1_BIT);
    /* Enough for a 1-bit block, and then nothing. */
    section_test_set(t, test, 0u, 1u);
    CHECK_EQ_U64(test->section.bits, 1u);
    CHECK(!section_block_set(&test->section, &test->pool, 1u, 2u));
    CHECK_EQ_U64(test->pool.failed_count, 1u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 1u);
    CHECK(!section_light_set(&test->section, &test->pool, 1u, 2u));
    CHECK_EQ_U64(test->pool.failed_count, 2u);
    section_test_check_all(t, test);
    uint16_t *const many = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        many[i] = (uint16_t)i;
    }
    CHECK(!section_blocks_import(&test->section, &test->pool, many));
    section_test_check_all(t, test);
    uint8_t *const lights = test_alloc(t, SECTION_VOXELS);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        lights[i] = (uint8_t)i;
    }
    CHECK(!section_lights_import(&test->section, &test->pool, lights));
    section_test_check_all(t, test);
    /* What needs no room still works, and a block given back is there for the next. */
    section_test_set(t, test, 0u, 0u);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    section_test_set(t, test, 2u, 2u);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->pool.carved_bytes, SECTION_TEST_BLOCK_1_BIT);
    section_test_end(test);
}

TEST(with_no_room_a_direct_section_stays_direct) {
    /* Direct, then a look wants to go to a 1-bit palette: there is room for 8192 only. */
    section_test_t *const test = section_test_begin(t, SECTION_TEST_BLOCK_DIRECT);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_blocks[i] = (uint16_t)i;
    }
    CHECK(section_blocks_import(&test->section, &test->pool, test->expected_blocks));
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, (uint16_t)(i & 1u));
    }
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_DIRECT);
    CHECK(test->pool.failed_count > 0u);
    /* A uniform one needs no room at all. */
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, 6u);
    }
    section_compact(&test->section, &test->pool);
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    section_test_end(test);
}

TEST(with_no_room_a_palette_that_could_narrow_stays_as_it_is) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_BLOCK_8_BIT);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        test->expected_blocks[i] = (uint16_t)(i % 17u);
    }
    CHECK(section_blocks_import(&test->section, &test->pool, test->expected_blocks));
    CHECK_EQ_U64(test->section.bits, 8u);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, (uint16_t)(i % 3u));
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 8u);
    CHECK(test->pool.failed_count > 0u);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        section_test_set(t, test, i, 2u);
    }
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, SECTION_BITS_UNIFORM);
    section_test_end(test);
}

TEST(with_no_room_a_palette_that_must_widen_says_so) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_BLOCK_1_BIT + SECTION_TEST_BLOCK_2_BIT);
    section_test_set(t, test, 0u, 1u);
    section_test_set(t, test, 1u, 2u); /* 2 bits, with a free 1-bit block behind it */
    section_test_set(t, test, 2u, 3u);
    CHECK_EQ_U64(test->section.bits, 2u);
    CHECK(!section_block_set(&test->section, &test->pool, 3u, 4u)); /* full, and no 4-bit block to be had */
    section_test_check_all(t, test);
    CHECK_EQ_U64(test->section.bits, 2u);
    section_test_end(test);
}

TEST(sections_that_come_and_go_use_the_same_memory_again) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_t sections[8];
    memset(sections, 0, sizeof(sections));
    uint64_t carved_after_first = 0;
    for (uint32_t round = 0; round < 50u; round++) {
        for (uint32_t s = 0; s < 8u; s++) {
            for (uint32_t i = 0; i < 20u; i++) {
                CHECK(section_block_set(&sections[s], &test->pool, i, (uint16_t)(i + s + 1u)));
            }
            CHECK(section_light_set(&sections[s], &test->pool, 0u, 9u));
        }
        for (uint32_t s = 0; s < 8u; s++) {
            CHECK_EQ_U64(section_block_get(&sections[s], 7u), 7u + s + 1u);
            section_deinit(&sections[s], &test->pool);
        }
        CHECK_EQ_U64(test->pool.live_bytes, 0u);
        if (round == 0u) {
            carved_after_first = test->pool.carved_bytes;
        }
    }
    CHECK(carved_after_first != 0u);
    CHECK_EQ_U64(test->pool.carved_bytes, carved_after_first);
    section_test_end(test);
}

TEST(a_pool_reset_forgets_its_lists_with_the_arena) {
    section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
    section_test_set(t, test, 0u, 1u);
    section_deinit(&test->section, &test->pool);
    arena_reset(&test->arena);
    section_pool_reset(&test->pool);
    CHECK_EQ_U64(test->pool.carved_bytes, 0u);
    for (uint32_t c = 0; c < SECTION_CLASS_COUNT; c++) {
        CHECK(test->pool.borrowed_free[c] == nullptr);
    }
    CHECK(test->pool.borrowed_arena == &test->arena);
    section_test_set(t, test, 0u, 1u);
    CHECK_EQ_U64(test->pool.carved_bytes, SECTION_TEST_BLOCK_1_BIT);
    section_pool_deinit(&test->pool);
    CHECK(test->pool.borrowed_arena == nullptr);
    section_test_end(test);
}

/* ---- What a section costs ----------------------------------------------------------------------------------------- */

/* Rows of the table in run/src/world/section/README.md: this test is where its numbers come from. Blocks fill by the
 * layer, as ground does: a stone base with ore, dirt, then grass and air. Light is uniform in the first few and varied
 * in the last, as a section with a surface in it is. */
TEST(memory_per_section_is_as_the_readme_says) {
    static const section_test_cost_t rows[] = {
        {"all air, or all stone, lit evenly", 0, 1, 0, {0}},
        {"air and one block, lit evenly", SECTION_TEST_BLOCK_1_BIT, 2, 0, {0}},
        {"4 states, lit evenly", SECTION_TEST_BLOCK_2_BIT, 4, 0, {0}},
        {"16 states, lit evenly", SECTION_TEST_BLOCK_4_BIT, 16, 0, {0}},
        {"all air, with varied light", SECTION_TEST_LIGHT, 1, 1, {0}},
        {"4 states, with varied light", SECTION_TEST_BLOCK_2_BIT + SECTION_TEST_LIGHT, 4, 1, {0}},
        {"16 states, with varied light", SECTION_TEST_BLOCK_4_BIT + SECTION_TEST_LIGHT, 16, 1, {0}},
        {"256 states, with varied light", SECTION_TEST_BLOCK_8_BIT + SECTION_TEST_LIGHT, 256, 1, {0}},
        {"4096 states, with varied light", SECTION_TEST_BLOCK_DIRECT + SECTION_TEST_LIGHT, 4096, 1, {0}},
    };
    /* Flat arrays of 2 bytes of block and 1 of light, for each voxel. */
    const uint64_t flat = SECTION_VOXELS * 3u;
    printf("%-34s %10s %11s %8s\n", "section", "pool bytes", "with struct", "of flat");
    for (uint32_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        section_test_t *const test = section_test_begin(t, SECTION_TEST_ARENA_BYTES);
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            test->expected_blocks[i] = (uint16_t)(i * rows[r].distinct / SECTION_VOXELS);
            test->expected_light[i] = rows[r].varied_light != 0u ? (uint8_t)((i >> 8) | 0x20u) : SECTION_LIGHT_OPEN_SKY;
        }
        CHECK(section_blocks_import(&test->section, &test->pool, test->expected_blocks));
        CHECK(section_lights_import(&test->section, &test->pool, test->expected_light));
        section_test_check_all(t, test);
        const uint64_t measured = test->pool.live_bytes;
        CHECK_EQ_U64(measured, rows[r].pool_bytes);
        const uint64_t with_struct = measured + SECTION_TEST_STRUCT_BYTES;
        printf("%-34s %10" PRIu64 " %11" PRIu64 " %7" PRIu64 "%%\n", rows[r].what, measured, with_struct,
               with_struct * 100u / flat);
        section_test_end(test);
    }
}

TEST_MAIN()
