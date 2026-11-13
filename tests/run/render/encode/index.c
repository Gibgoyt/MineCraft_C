/* ctest run_render_encode. See README.md. */
#include "harness/index.h"

#include "job/index.h"
#include "render/encode/index.h"
#include "tables/blocks/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    ENCODE_TEST_BUFFER_BYTES = WORLD_BUFFERS_SECTION_BYTES_MAX + 64u,
    ENCODE_TEST_JOB_SECTIONS = 512u,
    ENCODE_TEST_BYTE_DIRT = 0xA5u,
};

/* 8-aligned, with bytes after the most a section may take, all dirty, so that padding and overruns both show. */
static uint8_t *encode_test_buffer(test_context_t *t) {
    uint8_t *const buffer = test_alloc(t, ENCODE_TEST_BUFFER_BYTES);
    memset(buffer, ENCODE_TEST_BYTE_DIRT, ENCODE_TEST_BUFFER_BYTES);
    return buffer;
}

static uint64_t encode_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

static uint32_t encode_test_expected_bytes(uint32_t bricks, uint32_t voxels) {
    return (24u + 10u * bricks + 3u * voxels + 7u) & ~7u;
}

/* Every voxel, one at a time, against the arrays it came from, through the same lines a kernel runs, and every number the
 * header states, worked out again the long way. */
static void encode_test_compare(test_context_t *t, const uint8_t *encoded, uint32_t bytes, const uint16_t *blocks,
                                const uint8_t *light) {
    uint64_t bricks_seen = 0u;
    uint32_t voxels_seen = 0u;
    uint32_t wrong = 0u;
    /* Plan 009 phase 05: the bricks full of one material, light aside, the long way. */
    uint64_t uniform_seen = 0u;
    for (uint32_t brick = 0u; brick < 64u; brick++) {
        const uint32_t bx = (brick & 3u) * 4u, by = ((brick >> 2) & 3u) * 4u, bz = (brick >> 4) * 4u;
        const uint16_t first = blocks[section_voxel_index(bx, by, bz)];
        bool same = first != 0u;
        for (uint32_t v = 0u; v < 64u && same; v++) {
            same = blocks[section_voxel_index(bx + (v & 3u), by + ((v >> 2) & 3u), bz + (v >> 4))] == first;
        }
        uniform_seen |= same ? 1ull << brick : 0u;
    }
    for (uint32_t z = 0; z < 16u; z++) {
        for (uint32_t y = 0; y < 16u; y++) {
            for (uint32_t x = 0; x < 16u; x++) {
                const uint32_t index = section_voxel_index(x, y, z);
                uint16_t material = 0u;
                uint8_t packed = 0u;
                const bool found = bytes != 0u && world_buffers_section_voxel(encoded, x, y, z, &material, &packed);
                if (blocks[index] != 0u) {
                    voxels_seen++;
                    bricks_seen |= 1ull << ((x >> 2) + 4u * (y >> 2) + 16u * (z >> 2));
                }
                if (found != (blocks[index] != 0u) ||
                    (found && (material != blocks[index] || packed != light[index]))) {
                    wrong++;
                }
            }
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    if (voxels_seen == 0u) {
        CHECK_EQ_U64(bytes, 0u);
        return;
    }
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)encoded;
    CHECK_EQ_U64(header->uniform_mask, uniform_seen);
    if (header->brick_count == 0u) {
        CHECK_EQ_U64(header->brick_mask, ~0ull);
        CHECK_EQ_U64(bricks_seen, ~0ull);
        CHECK_EQ_U64(header->voxel_count, SECTION_VOXELS);
        CHECK_EQ_U64(voxels_seen, SECTION_VOXELS);
        CHECK_EQ_U64(header->material_offset, 24u);
        CHECK_EQ_U64(header->light_offset, 26u);
        CHECK_EQ_U64(bytes, WORLD_BUFFERS_SECTION_BYTES_UNIFORM);
        CHECK_EQ_U64(header->_pad0[0], 0u);
        for (uint32_t i = 27u; i < bytes; i++) {
            CHECK_EQ_U64(encoded[i], 0u);
        }
        for (uint32_t i = bytes; i < ENCODE_TEST_BUFFER_BYTES; i++) {
            if (encoded[i] != ENCODE_TEST_BYTE_DIRT) {
                CHECK_EQ_U64(encoded[i], ENCODE_TEST_BYTE_DIRT);
                break;
            }
        }
        return;
    }
    const uint32_t brick_total = (uint32_t)__builtin_popcountll(bricks_seen);
    CHECK_EQ_U64(header->brick_mask, bricks_seen);
    CHECK_EQ_U64(header->brick_count, brick_total);
    CHECK_EQ_U64(header->voxel_count, voxels_seen);
    CHECK_EQ_U64(header->material_offset, 24u + 10u * brick_total);
    CHECK_EQ_U64(header->light_offset, 24u + 10u * brick_total + 2u * voxels_seen);
    CHECK_EQ_U64(bytes, encode_test_expected_bytes(brick_total, voxels_seen));
    CHECK_EQ_U64(header->_pad0[0], 0u);
    for (uint32_t i = header->light_offset + voxels_seen; i < bytes; i++) {
        CHECK_EQ_U64(encoded[i], 0u); /* padding goes to the card as zeros */
    }
    for (uint32_t i = bytes; i < ENCODE_TEST_BUFFER_BYTES; i++) {
        if (encoded[i] != ENCODE_TEST_BYTE_DIRT) {
            CHECK_EQ_U64(encoded[i], ENCODE_TEST_BYTE_DIRT); /* it wrote past what it said it wrote */
            break;
        }
    }
}

static uint32_t encode_test_arrays(uint8_t *out, const uint16_t *blocks, const uint8_t *light) {
    memset(out, ENCODE_TEST_BYTE_DIRT, ENCODE_TEST_BUFFER_BYTES);
    const render_encode_source_t source = {.borrowed_blocks = blocks, .borrowed_light = light};
    return render_encode_section(&source, out);
}

TEST(a_section_with_no_block_has_no_bytes) {
    uint8_t *const out = encode_test_buffer(t);
    const render_encode_source_t uniform_air = {.block_uniform = 0u, .light_uniform = SECTION_LIGHT_OPEN_SKY};
    CHECK_EQ_U64(render_encode_section(&uniform_air, out), 0u);

    uint16_t *const air = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    memset(light, 0x7Bu, SECTION_VOXELS);
    /* An array of all air, as a section gets before it next compacts, and light with it. */
    CHECK_EQ_U64(encode_test_arrays(out, air, light), 0u);
    CHECK_EQ_U64(out[0], ENCODE_TEST_BYTE_DIRT);
}

/* The numbers here were worked out on paper. Voxel (5, 9, 14): brick (1, 2, 3) is 1 + 2*4 + 3*16 = 57. Inside it (1, 1, 2) is
 * 1 + 1*4 + 2*16 = 37. The size is 24 + 8 + 2 + 2 + 1 = 37, so 40 with padding. The header's uniform_mask is 0: no brick is full. */
TEST(one_voxel_gives_exactly_these_bytes) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    const uint32_t index = (14u << 8) | (9u << 4) | 5u;
    blocks[index] = 0xBEEFu;
    light[index] = 0xC3u;
    REQUIRE(encode_test_arrays(out, blocks, light) == 40u);
    uint64_t word = 0u;
    memcpy(&word, out, 8u);
    CHECK_EQ_U64(word, 1ull << 57);
    memcpy(&word, out + 8, 8u);
    CHECK_EQ_U64(word, 0u);
    memcpy(&word, out + 24, 8u);
    CHECK_EQ_U64(word, 1ull << 37);
    const uint8_t expected_header_tail[8] = {1u,  0u, 34u, 0u,
                                             36u, 0u, 1u,  0u}; /* 1 voxel, offsets 34 and 36, 1 brick */
    CHECK_MEM_EQ(out + 16, expected_header_tail, 8u);
    const uint8_t expected_tail[8] = {0u, 0u, 0xEFu, 0xBEu, 0xC3u, 0u, 0u, 0u}; /* first 0, material, light, padding */
    CHECK_MEM_EQ(out + 32, expected_tail, 8u);
}

/* Plan 009 phase 05. Brick 0 all water: marked. Brick 1 water with one stone in it: not. Brick 2 water lit two ways: marked, as the mask is of
 * the material alone (the water walk asks nothing else, and a lake is lit unevenly). Brick 3 water with one air: not, as it is not full. */
TEST(the_uniform_mask_marks_full_one_block_bricks) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    const uint16_t water = 0x0009u, stone = 0x0001u;
    for (uint32_t brick = 0u; brick < 4u; brick++) {
        for (uint32_t v = 0u; v < 64u; v++) {
            const uint32_t index = section_voxel_index(brick * 4u + (v & 3u), (v >> 2) & 3u, v >> 4);
            blocks[index] = water;
            light[index] = brick == 2u && v % 2u == 1u ? 0xA0u : 0xF0u;
        }
    }
    blocks[section_voxel_index(4u + 2u, 1u, 3u)] = stone;
    blocks[section_voxel_index(12u + 3u, 3u, 0u)] = 0u;
    const uint32_t bytes = encode_test_arrays(out, blocks, light);
    REQUIRE(bytes > 0u);
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)out;
    CHECK_EQ_U64(header->brick_mask, 0xFull);
    CHECK_EQ_U64(header->uniform_mask, 0x5ull);
    encode_test_compare(t, out, bytes, blocks, light);
}

TEST(any_one_voxel_is_found_where_it_was_put_and_nowhere_else) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    for (uint32_t index = 0; index < SECTION_VOXELS;
         index += 7u) { /* a full pass of every voxel is 16 million lookups */
        blocks[index] = (uint16_t)(index + 1u);
        light[index] = (uint8_t)(index ^ 0x5Au);
        const uint32_t bytes = encode_test_arrays(out, blocks, light);
        CHECK_EQ_U64(bytes, 40u);
        encode_test_compare(t, out, bytes, blocks, light);
        blocks[index] = 0u;
        light[index] = 0u;
    }
}

/* Worked out on paper: 24 bytes of header (every bit of both masks), the material, the light, then five zeros. Any one voxel that differs, in block or in
 * light, takes the long form, which is why each is tried at the first voxel, the last, and between. */
TEST(a_section_of_one_block_and_one_light_is_32_bytes_and_anything_else_is_not) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    const uint32_t places[3] = {0u, 1234u, SECTION_VOXELS - 1u};
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        blocks[i] = 0x0102u;
        light[i] = 0x35u;
    }
    REQUIRE(encode_test_arrays(out, blocks, light) == 32u);
    encode_test_compare(t, out, 32u, blocks, light);
    const uint8_t expected_tail[16] = {0x00u, 0x10u, 24u, 0u, 26u, 0u, 0u, 0u, 0x02u, 0x01u, 0x35u, 0u, 0u, 0u, 0u, 0u};
    CHECK_MEM_EQ(out + 16, expected_tail, sizeof(expected_tail));
    uint64_t uniform_word = 0u;
    memcpy(&uniform_word, out + 8, 8u);
    CHECK_EQ_U64(uniform_word, ~0ull);

    const render_encode_source_t uniform = {.block_uniform = 0x0102u, .light_uniform = 0x35u};
    memset(out, ENCODE_TEST_BYTE_DIRT, ENCODE_TEST_BUFFER_BYTES);
    REQUIRE(render_encode_section(&uniform, out) == 32u);
    encode_test_compare(t, out, 32u, blocks, light);

    for (uint32_t p = 0; p < 3u; p++) {
        blocks[places[p]] = 0x0103u;
        uint32_t bytes = encode_test_arrays(out, blocks, light);
        CHECK(bytes > 32u);
        encode_test_compare(t, out, bytes, blocks, light);
        blocks[places[p]] = 0x0102u;
        light[places[p]] = 0x36u;
        bytes = encode_test_arrays(out, blocks, light);
        CHECK(bytes > 32u);
        encode_test_compare(t, out, bytes, blocks, light);
        light[places[p]] = 0x35u;
    }
    /* One voxel of air, and it is not full at all. */
    blocks[77u] = 0u;
    const uint32_t bytes = encode_test_arrays(out, blocks, light);
    CHECK(bytes > 32u);
    encode_test_compare(t, out, bytes, blocks, light);
}

TEST(a_full_section_takes_the_most_a_section_can_and_says_so_in_brick_order) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        blocks[i] = (uint16_t)(i + 1u);
        light[i] = (uint8_t)(i * 31u);
    }
    const uint32_t bytes = encode_test_arrays(out, blocks, light);
    CHECK_EQ_U64(bytes, 12952u);
    CHECK_EQ_U64(bytes, WORLD_BUFFERS_SECTION_BYTES_MAX);
    encode_test_compare(t, out, bytes, blocks, light);

    /* The order on the wire, not only the lookup: brick 1, the second, starts at voxel 64, whose first voxel is (4, 0, 0). */
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)out;
    const uint16_t *const first = (const uint16_t *)(const void *)(out + 24u + 64u * 8u);
    const uint16_t *const material = (const uint16_t *)(const void *)(out + header->material_offset);
    CHECK_EQ_U64(first[0], 0u);
    CHECK_EQ_U64(first[1], 64u);
    CHECK_EQ_U64(first[63], 4032u);
    CHECK_EQ_U64(material[0], section_voxel_index(0u, 0u, 0u) + 1u);
    CHECK_EQ_U64(material[1], section_voxel_index(1u, 0u, 0u) + 1u);
    CHECK_EQ_U64(material[4], section_voxel_index(0u, 1u, 0u) + 1u);
    CHECK_EQ_U64(material[16], section_voxel_index(0u, 0u, 1u) + 1u);
    CHECK_EQ_U64(material[64], section_voxel_index(4u, 0u, 0u) + 1u);
    CHECK_EQ_U64(material[4095], section_voxel_index(15u, 15u, 15u) + 1u);
}

TEST(a_uniform_block_and_uniform_light_come_out_as_if_they_were_arrays) {
    uint8_t *const out = encode_test_buffer(t);
    uint8_t *const out_arrays = encode_test_buffer(t);
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        blocks[i] = 0xFFFFu; /* the highest state there is must not be taken for anything special */
        light[i] = (uint8_t)(i ^ (i >> 4));
    }
    const render_encode_source_t uniform_block = {
        .borrowed_light = light, .block_uniform = 0xFFFFu, .light_uniform = 0u};
    const uint32_t bytes = render_encode_section(&uniform_block, out);
    CHECK_EQ_U64(bytes, encode_test_arrays(out_arrays, blocks, light));
    CHECK_MEM_EQ(out, out_arrays, bytes);

    memset(light, 0xE1u, SECTION_VOXELS);
    const render_encode_source_t uniform_both = {.block_uniform = 0xFFFFu, .light_uniform = 0xE1u};
    const uint32_t both_bytes = render_encode_section(&uniform_both, out);
    CHECK_EQ_U64(both_bytes, encode_test_arrays(out_arrays, blocks, light));
    CHECK_MEM_EQ(out, out_arrays, both_bytes);

    /* Blocks in an array with uniform light. */
    memset(blocks, 0, SECTION_VOXELS * sizeof(uint16_t));
    blocks[1234] = 9u;
    memset(out, ENCODE_TEST_BYTE_DIRT, ENCODE_TEST_BUFFER_BYTES);
    const render_encode_source_t uniform_light = {.borrowed_blocks = blocks, .light_uniform = 0xE1u};
    const uint32_t mixed_bytes = render_encode_section(&uniform_light, out);
    encode_test_compare(t, out, mixed_bytes, blocks, light);
}

/* Random sections of every density, against a brute-force pass over every voxel. */
TEST(random_sections_of_every_density_match_a_brute_force_pass) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    uint64_t rng = 0x8F3C5A2D19E7B604ull;
    for (uint32_t round = 0; round < 400u; round++) {
        const uint32_t density = (uint32_t)(encode_test_random(&rng) % 257u);
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            const uint64_t r = encode_test_random(&rng);
            blocks[i] = (uint32_t)(r & 255u) < density ? (uint16_t)((r >> 8) | 1u) : 0u;
            light[i] = (uint8_t)(r >> 32);
        }
        const uint32_t bytes = encode_test_arrays(out, blocks, light);
        encode_test_compare(t, out, bytes, blocks, light);
    }
}

/* What an edge of the array does when only one brick is touched: one word, in one place. */
TEST(only_bricks_with_a_voxel_take_any_room) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    blocks[section_voxel_index(0u, 0u, 0u)] = 1u;
    blocks[section_voxel_index(3u, 3u, 3u)] = 2u;
    blocks[section_voxel_index(15u, 15u, 15u)] = 3u;
    light[section_voxel_index(15u, 15u, 15u)] = 0x11u;
    const uint32_t bytes = encode_test_arrays(out, blocks, light);
    CHECK_EQ_U64(bytes, encode_test_expected_bytes(2u, 3u));
    encode_test_compare(t, out, bytes, blocks, light);
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)out;
    CHECK_EQ_U64(header->brick_mask, 1ull | (1ull << 63));
}

/* ---- On workers ------------------------------------------------------------------------------------------------- */

/* One view by hand: sections of every shape, some with arrays and some without. */
static void encode_test_view(test_context_t *t, world_edit_snapshot_view_t *view) {
    world_edit_snapshot_section_t *const sections = test_alloc(t, ENCODE_TEST_JOB_SECTIONS * sizeof(sections[0]));
    world_edit_snapshot_voxels_t *const voxels = test_alloc(t, ENCODE_TEST_JOB_SECTIONS * sizeof(voxels[0]));
    uint64_t rng = 0x1D872B41C6F09E35ull;
    for (uint32_t s = 0; s < ENCODE_TEST_JOB_SECTIONS; s++) {
        world_edit_snapshot_section_t *const entry = &sections[s];
        entry->has_blocks = s % 5u != 0u ? 1u : 0u;
        entry->has_light = s % 3u != 0u ? 1u : 0u;
        entry->block_uniform = entry->has_blocks != 0u ? 0u : (uint16_t)(s % 2u * (s + 7u));
        entry->light_uniform = entry->has_light != 0u ? 0u : (uint8_t)(s * 3u);
        const uint32_t density = s % 257u;
        for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
            const uint64_t r = encode_test_random(&rng);
            voxels[s].blocks[i] = (uint32_t)(r & 255u) < density ? (uint16_t)((r >> 8) | 1u) : 0u;
            voxels[s].light[i] = (uint8_t)(r >> 32);
        }
    }
    view->borrowed_sections = sections;
    view->borrowed_voxels = voxels;
    view->section_count = ENCODE_TEST_JOB_SECTIONS;
}

/* The arena gives no alignment above what its callers ask for. */
static void *encode_test_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

TEST(workers_write_the_same_bytes_one_thread_does) {
    world_edit_snapshot_view_t *const view = encode_test_aligned(t, sizeof(*view));
    encode_test_view(t, view);
    uint8_t *const staging = encode_test_aligned(t, ENCODE_TEST_JOB_SECTIONS * WORLD_BUFFERS_SECTION_BYTES_MAX);
    render_encode_job_t *const jobs = encode_test_aligned(t, ENCODE_TEST_JOB_SECTIONS * sizeof(jobs[0]));

    job_system_t *const system = encode_test_aligned(t, sizeof(*system));
    thread_topology_t *const topology = test_alloc(t, sizeof(*topology));
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(system, topology));
    job_group_t *const group = encode_test_aligned(t, sizeof(*group));
    job_group_init(group);
    for (uint32_t s = 0; s < ENCODE_TEST_JOB_SECTIONS; s++) {
        render_encode_job_begin(&jobs[s], view, s, staging + (uint64_t)s * WORLD_BUFFERS_SECTION_BYTES_MAX);
        uint32_t ignored = 0u;
        CHECK(!render_encode_job_done(&jobs[s], &ignored));
        job_submit(system, group, render_encode_job_run, &jobs[s]);
    }
    job_group_wait(system, group);
    job_group_deinit(group);
    job_deinit(system);

    uint8_t *const out = encode_test_buffer(t);
    for (uint32_t s = 0; s < ENCODE_TEST_JOB_SECTIONS; s++) {
        const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[s];
        const render_encode_source_t source = {
            .borrowed_blocks = entry->has_blocks != 0u ? view->borrowed_voxels[s].blocks : nullptr,
            .borrowed_light = entry->has_light != 0u ? view->borrowed_voxels[s].light : nullptr,
            .block_uniform = entry->block_uniform,
            .light_uniform = entry->light_uniform,
        };
        const uint32_t expected = render_encode_section(&source, out);
        uint32_t bytes = UINT32_MAX;
        CHECK(render_encode_job_done(&jobs[s], &bytes));
        CHECK_EQ_U64(bytes, expected);
        CHECK_MEM_EQ(staging + (uint64_t)s * WORLD_BUFFERS_SECTION_BYTES_MAX, out, expected);
    }
}

/* Prints what the stage file records, and checks nothing about time, since a timing on a shared machine would be flaky. */
TEST(sizes_and_time_on_this_machine) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    /* A surface section: stone below a rolling height, a few holes, and full sky light above. */
    uint64_t rng = 0x35D1A9E7F02C8B46ull;
    uint64_t total_bytes = 0u;
    uint64_t total_ns = 0u;
    const uint32_t rounds = 256u;
    for (uint32_t round = 0; round < rounds; round++) {
        const uint32_t base = (round * 7u) % 16u;
        for (uint32_t z = 0; z < 16u; z++) {
            for (uint32_t y = 0; y < 16u; y++) {
                for (uint32_t x = 0; x < 16u; x++) {
                    const uint32_t top = base + (x + y) / 8u;
                    const uint64_t r = encode_test_random(&rng);
                    const uint32_t index = section_voxel_index(x, y, z);
                    blocks[index] = z <= top && (r & 63u) != 0u ? (uint16_t)(1u + (r >> 8) % 4u) : 0u;
                    light[index] = z > top ? SECTION_LIGHT_OPEN_SKY : 0u;
                }
            }
        }
        const int64_t started = time_now_ns();
        const render_encode_source_t source = {.borrowed_blocks = blocks, .borrowed_light = light};
        total_bytes += render_encode_section(&source, out);
        total_ns += (uint64_t)(time_now_ns() - started);
    }
    printf("terrain-shaped section: %" PRIu64 " bytes on average, encoded in %" PRIu64 " ns on one thread\n",
           total_bytes / rounds, total_ns / rounds);
    CHECK(total_bytes / rounds <= WORLD_BUFFERS_SECTION_BYTES_MAX);
}

/* ---- The octant (plan 009 phase 03) ------------------------------------------------------------------------------------ */

/* One row of the phase file's "The reduction, worked": a 2 x 2 x 2 group, index x + 2 y + 4 z, so 0..3 is the bottom layer. */
typedef struct encode_test_octant_row_t {
    const char *name;                            /* 0 */
    uint16_t blocks[RENDER_ENCODE_GROUP_VOXELS]; /* 8 */
    uint8_t light[RENDER_ENCODE_GROUP_VOXELS];   /* 24 */
    uint16_t material;                           /* 32: what the voxel above comes to */
    uint8_t packed;                              /* 34 */
    uint8_t _pad0[5];                            /* 35 */
} encode_test_octant_row_t;
static_assert(sizeof(encode_test_octant_row_t) == 40);

TEST(the_octant_reduction_table) {
    enum : uint16_t {
        S = BLOCK_ID_STONE,
        D = BLOCK_ID_DIRT,
        G = BLOCK_ID_GRASS_BLOCK,
        W = BLOCK_ID_WATER,
        A = BLOCK_ID_AIR,
        T = BLOCK_ID_TORCH,
        F = BLOCK_ID_DANDELION,
        O = BLOCK_ID_WOOD_DOOR,
        L = BLOCK_ID_OAK_SLAB,
        R = BLOCK_ID_OAK_STAIRS,
    };
    static const encode_test_octant_row_t rows[] = {
        {"8 stone", {S, S, S, S, S, S, S, S}, {0}, S, 0u, {0}},
        {"4 stone, 4 air: the half rule keeps it", {S, S, S, S, A, A, A, A}, {0}, S, 0u, {0}},
        {"3 stone, 5 air", {S, S, S, A, A, A, A, A}, {0}, A, 0u, {0}},
        {"4 stone above air, the half rule still", {A, A, A, A, S, S, S, S}, {0}, S, 0u, {0}},
        {"4 dirt below, 2 grass above, 2 air above: the grass top", {D, D, D, D, G, A, G, A}, {0}, G, 0u, {0}},
        {"4 dirt below, 4 grass above", {D, D, D, D, G, G, G, G}, {0}, G, 0u, {0}},
        {"a tie in the top layer goes to the first met", {D, D, D, D, G, S, S, G}, {0}, G, 0u, {0}},
        {"the most common of the top layer", {S, S, S, S, D, G, G, G}, {0}, G, 0u, {0}},
        {"4 water, 4 air: water counts", {W, W, W, W, A, A, A, A}, {0}, W, 0u, {0}},
        {"torches do not count", {S, S, S, T, T, T, T, A}, {0}, A, 0u, {0}},
        {"flowers do not count", {G, G, G, F, F, F, F, A}, {0}, A, 0u, {0}},
        {"doors do not count", {S, S, S, O, O, A, A, A}, {0}, A, 0u, {0}},
        {"slabs count", {L, L, L, L, A, A, A, A}, {0}, L, 0u, {0}},
        {"stairs count", {S, S, R, R, A, A, A, A}, {0}, S, 0u, {0}},
        {"stairs on top", {S, S, S, S, R, A, A, A}, {0}, R, 0u, {0}},
        {"light: the brightest of the counted, sky and block light each",
         {S, S, S, S, G, G, A, A},
         {0x00u, 0x03u, 0x10u, 0x00u, 0xE1u, 0x2Au, 0xFFu, 0xFFu},
         G,
         0xEAu,
         {0}},
        {"air keeps no light",
         {A, A, A, A, A, A, A, A},
         {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu},
         A,
         0u,
         {0}},
        {"a torch's light is not counted",
         {S, S, S, S, T, A, A, A},
         {0x00u, 0x00u, 0x00u, 0x00u, 0x0Fu, 0u, 0u, 0u},
         S,
         0u,
         {0}},
    };
    for (uint32_t i = 0u; i < sizeof(rows) / sizeof(rows[0]); i++) {
        uint16_t material = 0xFFFFu;
        uint8_t packed = 0xFFu;
        render_encode_octant_voxel(rows[i].blocks, rows[i].light, &material, &packed);
        if (!CHECK_EQ_U64(material, rows[i].material) || !CHECK_EQ_U64(packed, rows[i].packed)) {
            printf("  row %" PRIu32 ", %s: %s with light %#x\n", i, rows[i].name, block_defs[material].name,
                   (uint32_t)packed);
        }
    }
}

/* The section's octant is each 2 x 2 x 2 group through the rule; a uniform section stays uniform, and an empty one empty. */
TEST(a_section_reduces_to_its_octant) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    render_encode_octant_t *const octant = test_alloc(t, sizeof(render_encode_octant_t));
    uint64_t rng = 0x243F6A8885A308D3ull;
    static const uint16_t palette[4] = {BLOCK_ID_AIR, BLOCK_ID_STONE, BLOCK_ID_DIRT, BLOCK_ID_TORCH};
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        const uint64_t r = encode_test_random(&rng);
        blocks[i] = palette[r & 3u];
        light[i] = (uint8_t)(r >> 8);
    }
    const render_encode_source_t mixed = {blocks, light, 0u, 0u, {0}};
    CHECK(!render_encode_section_octant(&mixed, octant));
    uint32_t wrong = 0u;
    for (uint32_t z = 0u; z < 8u; z++) {
        for (uint32_t y = 0u; y < 8u; y++) {
            for (uint32_t x = 0u; x < 8u; x++) {
                uint16_t group[8];
                uint8_t group_light[8];
                for (uint32_t i = 0u; i < 8u; i++) {
                    const uint32_t index =
                        section_voxel_index(2u * x + (i & 1u), 2u * y + ((i >> 1) & 1u), 2u * z + (i >> 2));
                    group[i] = blocks[index];
                    group_light[i] = light[index];
                }
                uint16_t material = 0u;
                uint8_t packed = 0u;
                render_encode_octant_voxel(group, group_light, &material, &packed);
                const uint32_t place = x + 8u * y + 64u * z;
                wrong += octant->material[place] != material || octant->light[place] != packed ? 1u : 0u;
            }
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    const render_encode_source_t stone = {nullptr, nullptr, BLOCK_ID_STONE, 0x07u, {0}};
    CHECK(render_encode_section_octant(&stone, octant));
    CHECK_EQ_U64(octant->material[0], BLOCK_ID_STONE);
    CHECK_EQ_U64(octant->material[511], BLOCK_ID_STONE);
    CHECK_EQ_U64(octant->light[300], 0x07u);
    const render_encode_source_t air = {nullptr, nullptr, BLOCK_ID_AIR, 0xF0u, {0}};
    CHECK(render_encode_section_octant(&air, octant));
    CHECK_EQ_U64(octant->material[100], 0u);
    CHECK_EQ_U64(octant->light[100], 0u);
}

TEST_MAIN()
