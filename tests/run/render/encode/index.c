/* ctest run_render_encode. See README.md. */
#include "harness/index.h"

#include "job/index.h"
#include "render/encode/index.h"
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
    return (16u + 10u * bricks + 3u * voxels + 7u) & ~7u;
}

/* Every voxel, one at a time, against the arrays it came from, through the same lines a kernel runs, and every number the
 * header states, worked out again the long way. */
static void encode_test_compare(test_context_t *t, const uint8_t *encoded, uint32_t bytes, const uint16_t *blocks,
                                const uint8_t *light) {
    uint64_t bricks_seen = 0u;
    uint32_t voxels_seen = 0u;
    uint32_t wrong = 0u;
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
    if (header->brick_count == 0u) {
        CHECK_EQ_U64(header->brick_mask, ~0ull);
        CHECK_EQ_U64(bricks_seen, ~0ull);
        CHECK_EQ_U64(header->voxel_count, SECTION_VOXELS);
        CHECK_EQ_U64(voxels_seen, SECTION_VOXELS);
        CHECK_EQ_U64(header->material_offset, 16u);
        CHECK_EQ_U64(header->light_offset, 18u);
        CHECK_EQ_U64(bytes, WORLD_BUFFERS_SECTION_BYTES_UNIFORM);
        CHECK_EQ_U64(header->_pad0[0], 0u);
        for (uint32_t i = 19u; i < bytes; i++) {
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
    CHECK_EQ_U64(header->material_offset, 16u + 10u * brick_total);
    CHECK_EQ_U64(header->light_offset, 16u + 10u * brick_total + 2u * voxels_seen);
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
 * 1 + 1*4 + 2*16 = 37. The size is 16 + 8 + 2 + 2 + 1 = 29, so 32 with padding. */
TEST(one_voxel_gives_exactly_these_bytes) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    const uint32_t index = (14u << 8) | (9u << 4) | 5u;
    blocks[index] = 0xBEEFu;
    light[index] = 0xC3u;
    REQUIRE(encode_test_arrays(out, blocks, light) == 32u);
    uint64_t word = 0u;
    memcpy(&word, out, 8u);
    CHECK_EQ_U64(word, 1ull << 57);
    memcpy(&word, out + 16, 8u);
    CHECK_EQ_U64(word, 1ull << 37);
    const uint8_t expected_header_tail[8] = {1u,  0u, 26u, 0u,
                                             28u, 0u, 1u,  0u}; /* 1 voxel, offsets 26 and 28, 1 brick */
    CHECK_MEM_EQ(out + 8, expected_header_tail, 8u);
    const uint8_t expected_tail[8] = {0u, 0u, 0xEFu, 0xBEu, 0xC3u, 0u, 0u, 0u}; /* first 0, material, light, padding */
    CHECK_MEM_EQ(out + 24, expected_tail, 8u);
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
        CHECK_EQ_U64(bytes, 32u);
        encode_test_compare(t, out, bytes, blocks, light);
        blocks[index] = 0u;
        light[index] = 0u;
    }
}

/* Worked out on paper: 16 bytes of header, the material, the light, then five zeros. Any one voxel that differs, in block or in
 * light, takes the long form, which is why each is tried at the first voxel, the last, and between. */
TEST(a_section_of_one_block_and_one_light_is_24_bytes_and_anything_else_is_not) {
    uint16_t *const blocks = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const light = test_alloc(t, SECTION_VOXELS);
    uint8_t *const out = encode_test_buffer(t);
    const uint32_t places[3] = {0u, 1234u, SECTION_VOXELS - 1u};
    for (uint32_t i = 0; i < SECTION_VOXELS; i++) {
        blocks[i] = 0x0102u;
        light[i] = 0x35u;
    }
    REQUIRE(encode_test_arrays(out, blocks, light) == 24u);
    encode_test_compare(t, out, 24u, blocks, light);
    const uint8_t expected_tail[16] = {0x00u, 0x10u, 16u, 0u, 18u, 0u, 0u, 0u, 0x02u, 0x01u, 0x35u, 0u, 0u, 0u, 0u, 0u};
    CHECK_MEM_EQ(out + 8, expected_tail, sizeof(expected_tail));

    const render_encode_source_t uniform = {.block_uniform = 0x0102u, .light_uniform = 0x35u};
    memset(out, ENCODE_TEST_BYTE_DIRT, ENCODE_TEST_BUFFER_BYTES);
    REQUIRE(render_encode_section(&uniform, out) == 24u);
    encode_test_compare(t, out, 24u, blocks, light);

    for (uint32_t p = 0; p < 3u; p++) {
        blocks[places[p]] = 0x0103u;
        uint32_t bytes = encode_test_arrays(out, blocks, light);
        CHECK(bytes > 24u);
        encode_test_compare(t, out, bytes, blocks, light);
        blocks[places[p]] = 0x0102u;
        light[places[p]] = 0x36u;
        bytes = encode_test_arrays(out, blocks, light);
        CHECK(bytes > 24u);
        encode_test_compare(t, out, bytes, blocks, light);
        light[places[p]] = 0x35u;
    }
    /* One voxel of air, and it is not full at all. */
    blocks[77u] = 0u;
    const uint32_t bytes = encode_test_arrays(out, blocks, light);
    CHECK(bytes > 24u);
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
    CHECK_EQ_U64(bytes, 12944u);
    CHECK_EQ_U64(bytes, WORLD_BUFFERS_SECTION_BYTES_MAX);
    encode_test_compare(t, out, bytes, blocks, light);

    /* The order on the wire, not only the lookup: brick 1, the second, starts at voxel 64, whose first voxel is (4, 0, 0). */
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)out;
    const uint16_t *const first = (const uint16_t *)(const void *)(out + 16u + 64u * 8u);
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

TEST_MAIN()
