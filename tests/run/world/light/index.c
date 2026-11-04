/* ctest run_world_light. See README.md. */
#include "harness/index.h"

#include "tables/blocks/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    LIGHT_TEST_ARENA_BYTES = 128ull << 20,
    LIGHT_TEST_POOL_BYTES = 96ull << 20,
};
enum : uint32_t {
    LIGHT_TEST_LOG = 1u << 16,
    /* Where the tests put blocks: z from 0 to 47. Under it is solid stone, and over it air. */
    LIGHT_TEST_FLOOR_SECTIONS = 16u,
    LIGHT_TEST_ROOF_Z = 40u,
};

/* A map, its edits and its light, over memory of their own, so that a test can ask for any size. */
typedef struct light_test_t {
    arena_t arena;
    arena_t arena_pool;
    section_pool_t pool;
    world_map_t map;
    world_edit_t edit;
    world_light_t light;
    uint8_t _pad0[32];
} light_test_t;
static_assert(sizeof(light_test_t) == 704);

/* The separate full recompute: a flat array over a box of z, by another route than the code under test, starting from nothing
 * and going over every voxel until nothing changes. */
typedef struct light_reference_t {
    uint16_t *borrowed_blocks;
    uint8_t *borrowed_sky;
    uint8_t *borrowed_block;
    uint8_t *borrowed_present; /* per column of the box */
    int32_t min_x;             /* chunks */
    int32_t min_y;
    int32_t min_z;
    uint32_t side;   /* columns along each of x and y */
    uint32_t size;   /* voxels along each of x and y */
    uint32_t height; /* voxels along z */
} light_reference_t;
static_assert(sizeof(light_reference_t) == 56);

static uint64_t light_test_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

static light_test_t *light_test_begin_sized(test_context_t *t, uint32_t table_radius, uint64_t wave_capacity,
                                            uint64_t log_capacity, uint64_t pool_bytes) {
    uint8_t *const raw = test_alloc(t, sizeof(light_test_t) + 64u);
    light_test_t *const test = (light_test_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(&test->arena, "light_test", test_alloc(t, LIGHT_TEST_ARENA_BYTES), LIGHT_TEST_ARENA_BYTES);
    arena_init(&test->arena_pool, "light_test_pool", test_alloc(t, pool_bytes), pool_bytes);
    section_pool_init(&test->pool, &test->arena_pool);
    world_map_init(&test->map, &test->arena, &test->pool, table_radius, 0, 0);
    world_edit_init(&test->edit, &test->arena, &test->map, log_capacity);
    world_light_init(&test->light, &test->arena, &test->edit, wave_capacity);
    return test;
}

static light_test_t *light_test_begin(test_context_t *t, uint32_t table_radius) {
    return light_test_begin_sized(t, table_radius, WORLD_LIGHT_WAVE_CAPACITY_DEFAULT, LIGHT_TEST_LOG,
                                  LIGHT_TEST_POOL_BYTES);
}

static void light_test_end(light_test_t *test) {
    world_light_deinit(&test->light);
    world_edit_deinit(&test->edit);
    world_map_deinit(&test->map);
    section_pool_deinit(&test->pool);
    arena_deinit(&test->arena_pool);
    arena_deinit(&test->arena);
}

static uint32_t light_test_slot(const light_test_t *test, int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = UINT32_MAX;
    (void)world_map_slot_of(&test->map, chunk_x, chunk_y, &slot);
    return slot;
}

/* Every column the map wants, committed, and left with whatever its blocks are: air, with no light worked out. */
static void light_test_commit_all(test_context_t *t, light_test_t *test) {
    world_map_request_t request;
    while (world_map_stream_take(&test->map, &request, 1u) == 1u) {
        CHECK(world_map_stream_commit(&test->map, &request) != nullptr);
    }
}

static void light_test_add_all(test_context_t *t, light_test_t *test) {
    for (uint32_t i = 0u; i < test->map.active_count; i++) {
        CHECK_EQ_U64(world_light_column_added(&test->light, test->map.borrowed_active[i]), WORLD_LIGHT_STATUS_OK);
    }
}

static uint8_t light_test_at(test_context_t *t, const light_test_t *test, int32_t x, int32_t y, int32_t z) {
    uint8_t packed = 0u;
    CHECK_EQ_U64(world_map_light_get(&test->map, x, y, z, &packed), WORLD_MAP_STATUS_OK);
    return packed;
}

static void light_test_set(test_context_t *t, light_test_t *test, int32_t x, int32_t y, int32_t z, uint16_t state) {
    const world_edit_status_t status = world_edit_block_set(&test->edit, x, y, z, state);
    CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
}

/* ---- The reference --------------------------------------------------------------------------------------------------- */

static void light_reference_prepare(test_context_t *t, light_reference_t *reference, int32_t min_x, int32_t min_y,
                                    uint32_t side, int32_t min_z, uint32_t height) {
    const uint32_t size = side * COLUMN_SIZE;
    const uint64_t voxels = (uint64_t)size * size * height;
    *reference = (light_reference_t){
        .borrowed_blocks = test_alloc(t, voxels * sizeof(uint16_t)),
        .borrowed_sky = test_alloc(t, voxels),
        .borrowed_block = test_alloc(t, voxels),
        .borrowed_present = test_alloc(t, (uint64_t)side * side),
        .min_x = min_x,
        .min_y = min_y,
        .min_z = min_z,
        .side = side,
        .size = size,
        .height = height,
    };
}

static uint64_t light_reference_index(const light_reference_t *reference, uint32_t x, uint32_t y, uint32_t z) {
    return ((uint64_t)z * reference->size + y) * reference->size + x;
}

static uint8_t light_reference_present(const light_reference_t *reference, uint32_t x, uint32_t y) {
    return reference->borrowed_present[(y / COLUMN_SIZE) * reference->side + x / COLUMN_SIZE];
}

/* What goes from a voxel holding from into one holding block to, by the rules in the README, written out in full. */
static uint32_t light_reference_step(uint32_t from, uint16_t to, bool sky_falling) {
    const uint32_t opacity = block_defs[to].opacity;
    if (opacity == 15u) {
        return 0u;
    }
    if (sky_falling && from == 15u && opacity == 0u) {
        return 15u;
    }
    const uint32_t lost = opacity < 1u ? 1u : opacity;
    return from <= lost ? 0u : from - lost;
}

/* Gets each voxel's block from the map, by world position, and then works light out from nothing. */
static void light_reference_compute(light_reference_t *reference, const world_map_t *map) {
    const int32_t origin_x = reference->min_x * (int32_t)COLUMN_SIZE;
    const int32_t origin_y = reference->min_y * (int32_t)COLUMN_SIZE;
    const uint32_t size = reference->size;
    const uint32_t height = reference->height;
    for (uint32_t cy = 0u; cy < reference->side; cy++) {
        for (uint32_t cx = 0u; cx < reference->side; cx++) {
            reference->borrowed_present[cy * reference->side + cx] =
                world_map_column_at(map, reference->min_x + (int32_t)cx, reference->min_y + (int32_t)cy) != nullptr
                    ? 1u
                    : 0u;
        }
    }
    for (uint32_t z = 0u; z < height; z++) {
        const int32_t world_z = reference->min_z + (int32_t)z;
        const uint32_t section = column_section_of(world_z);
        const uint32_t local_z = column_section_local_of(world_z);
        for (uint32_t y = 0u; y < size; y++) {
            for (uint32_t x = 0u; x < size; x++) {
                const uint64_t at = light_reference_index(reference, x, y, z);
                const column_t *const column = world_map_column_at(map, column_chunk_of(origin_x + (int32_t)x),
                                                                   column_chunk_of(origin_y + (int32_t)y));
                reference->borrowed_blocks[at] =
                    column == nullptr
                        ? BLOCK_ID_AIR
                        : section_block_get(&column->sections[section],
                                            section_voxel_index(x % COLUMN_SIZE, y % COLUMN_SIZE, local_z));
                reference->borrowed_sky[at] = 0u;
                reference->borrowed_block[at] =
                    column == nullptr ? 0u : block_defs[reference->borrowed_blocks[at]].light;
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        /* From the top down, and from one corner to the other, then back: order is only for speed. */
        for (uint32_t turn = 0u; turn < 2u; turn++) {
            for (uint32_t zz = 0u; zz < height; zz++) {
                const uint32_t z = turn == 0u ? height - 1u - zz : zz;
                for (uint32_t yy = 0u; yy < size; yy++) {
                    const uint32_t y = turn == 0u ? yy : size - 1u - yy;
                    for (uint32_t xx = 0u; xx < size; xx++) {
                        const uint32_t x = turn == 0u ? xx : size - 1u - xx;
                        if (light_reference_present(reference, x, y) == 0u) {
                            continue;
                        }
                        const uint64_t at = light_reference_index(reference, x, y, z);
                        const uint16_t here = reference->borrowed_blocks[at];
                        uint32_t sky = reference->borrowed_sky[at];
                        uint32_t glow = reference->borrowed_block[at];
                        const uint32_t from_above_sky =
                            z + 1u == height
                                ? light_reference_step(15u, here, true)
                                : light_reference_step(reference->borrowed_sky[at + (uint64_t)size * size], here, true);
                        sky = from_above_sky > sky ? from_above_sky : sky;
                        const int64_t offsets[6][3] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
                                                       {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
                        for (uint32_t n = 0u; n < 6u; n++) {
                            const int64_t nx = (int64_t)x + offsets[n][0];
                            const int64_t ny = (int64_t)y + offsets[n][1];
                            const int64_t nz = (int64_t)z + offsets[n][2];
                            if (nx < 0 || ny < 0 || nz < 0 || nx >= (int64_t)size || ny >= (int64_t)size ||
                                nz >= (int64_t)height ||
                                light_reference_present(reference, (uint32_t)nx, (uint32_t)ny) == 0u) {
                                continue;
                            }
                            const uint64_t there =
                                light_reference_index(reference, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
                            const uint32_t got_sky = light_reference_step(reference->borrowed_sky[there], here, false);
                            const uint32_t got_glow =
                                light_reference_step(reference->borrowed_block[there], here, false);
                            sky = got_sky > sky ? got_sky : sky;
                            glow = got_glow > glow ? got_glow : glow;
                        }
                        if (sky != reference->borrowed_sky[at] || glow != reference->borrowed_block[at]) {
                            reference->borrowed_sky[at] = (uint8_t)sky;
                            reference->borrowed_block[at] = (uint8_t)glow;
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}

/* What a voxel of the box should hold, once its light has been worked out: its own, or, for an opaque block, what its faces see. An
 * opaque block that gives off light holds that in that channel. Past the top of the box is open sky. */
static uint8_t light_reference_wanted(const light_reference_t *reference, uint32_t x, uint32_t y, uint32_t z) {
    const uint64_t at = light_reference_index(reference, x, y, z);
    const uint16_t here = reference->borrowed_blocks[at];
    if (block_defs[here].opacity != 15u) {
        return section_light_pack(reference->borrowed_sky[at], reference->borrowed_block[at]);
    }
    uint32_t sky = z + 1u == reference->height ? 15u : 0u;
    uint32_t glow = 0u;
    const int64_t offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (uint32_t n = 0u; n < 6u; n++) {
        const int64_t nx = (int64_t)x + offsets[n][0];
        const int64_t ny = (int64_t)y + offsets[n][1];
        const int64_t nz = (int64_t)z + offsets[n][2];
        if (nx < 0 || ny < 0 || nz < 0 || nx >= (int64_t)reference->size || ny >= (int64_t)reference->size ||
            nz >= (int64_t)reference->height || light_reference_present(reference, (uint32_t)nx, (uint32_t)ny) == 0u) {
            continue;
        }
        const uint64_t there = light_reference_index(reference, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
        if (block_defs[reference->borrowed_blocks[there]].opacity != 15u) {
            sky = reference->borrowed_sky[there] > sky ? reference->borrowed_sky[there] : sky;
            glow = reference->borrowed_block[there] > glow ? reference->borrowed_block[there] : glow;
        }
    }
    return section_light_pack(sky, block_defs[here].light != 0u ? block_defs[here].light : glow);
}

/* How many voxels differ, in the box and all above and below it: above, whatever is lit must be plain open sky, and below, dark.
 * Prints the first. */
static uint64_t light_reference_differences(const light_reference_t *reference, const world_map_t *map) {
    uint64_t differences = 0u;
    const int32_t top = reference->min_z + (int32_t)reference->height;
    for (uint32_t cy = 0u; cy < reference->side; cy++) {
        for (uint32_t cx = 0u; cx < reference->side; cx++) {
            const column_t *const column =
                world_map_column_at(map, reference->min_x + (int32_t)cx, reference->min_y + (int32_t)cy);
            if (column == nullptr) {
                continue;
            }
            for (int32_t z = COLUMN_Z_STORAGE_MIN; z < COLUMN_Z_STORAGE_END; z++) {
                const section_t *const section = &column->sections[column_section_of(z)];
                const uint32_t local_z = column_section_local_of(z);
                for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
                    for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
                        const uint8_t got = section_light_get(section, section_voxel_index(x, y, local_z));
                        uint8_t want = 0u;
                        if (z >= top) {
                            want = SECTION_LIGHT_OPEN_SKY;
                        } else if (z >= reference->min_z) {
                            want = light_reference_wanted(reference, cx * COLUMN_SIZE + x, cy * COLUMN_SIZE + y,
                                                          (uint32_t)(z - reference->min_z));
                        }
                        if (got != want) {
                            if (differences == 0u) {
                                printf("first difference: chunk box (%" PRIu32 ", %" PRIu32 ") voxel (%" PRIu32
                                       ", %" PRIu32 ", %" PRId32 "): got %02x, want %02x\n",
                                       cx, cy, x, y, z, got, want);
                            }
                            differences++;
                        }
                    }
                }
            }
        }
    }
    return differences;
}

/* A box covering z from -1 to 63, which holds all the tests' light: blocks stop at 47 and a torch's light dies in 14 more. */
static void light_reference_of_band(test_context_t *t, light_reference_t *reference, int32_t min_x, int32_t min_y,
                                    uint32_t side) {
    light_reference_prepare(t, reference, min_x, min_y, side, -1, 65u);
}

/* ---- Worlds to edit -------------------------------------------------------------------------------------------------- */

/* Solid stone under z = 0, and sections left as air above, with no log: what a column is filled with. */
static void light_test_floor(light_test_t *test, uint32_t slot) {
    column_t *const column = &test->map.borrowed_columns[slot];
    for (uint32_t s = 0u; s < LIGHT_TEST_FLOOR_SECTIONS; s++) {
        section_block_fill(&column->sections[s], &test->pool, BLOCK_ID_STONE);
    }
}

static uint16_t light_test_random_block(uint64_t *rng) {
    static const uint16_t blocks[] = {
        BLOCK_ID_AIR,   BLOCK_ID_AIR,   BLOCK_ID_AIR,   BLOCK_ID_AIR,        BLOCK_ID_STONE,
        BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_DIRT,  BLOCK_ID_OAK_LEAVES, BLOCK_ID_ICE,
        BLOCK_ID_WATER, BLOCK_ID_GLASS, BLOCK_ID_TORCH, BLOCK_ID_LAVA,       BLOCK_ID_FURNACE_LIT,
    };
    return blocks[light_test_random(rng) % (sizeof(blocks) / sizeof(blocks[0]))];
}

/* Each active column gets a floor and scattered blocks. A roof, with holes, shuts most of the sky out. */
static void light_test_scatter(light_test_t *test, uint64_t *rng, bool roofed, uint32_t blocks_per_column) {
    for (uint32_t i = 0u; i < test->map.active_count; i++) {
        const uint32_t slot = test->map.borrowed_active[i];
        column_t *const column = &test->map.borrowed_columns[slot];
        light_test_floor(test, slot);
        if (roofed) {
            for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
                for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
                    if (light_test_random(rng) % 32u != 0u) {
                        (void)column_block_set(column, &test->pool, x, y, LIGHT_TEST_ROOF_Z, BLOCK_ID_STONE);
                    }
                }
            }
        }
        for (uint32_t b = 0u; b < blocks_per_column; b++) {
            const uint64_t r = light_test_random(rng);
            (void)column_block_set(column, &test->pool, (uint32_t)(r & 15u), (uint32_t)((r >> 4) & 15u),
                                   (int32_t)((r >> 8) % 48u), light_test_random_block(rng));
        }
        world_edit_column_filled(&test->edit, slot);
    }
}

static void light_test_edit_randomly(light_test_t *test, uint64_t *rng, uint32_t count, int32_t min_x, int32_t min_y,
                                     uint32_t size) {
    for (uint32_t i = 0u; i < count; i++) {
        const uint64_t r = light_test_random(rng);
        const int32_t x = min_x + (int32_t)((r & 0xFFFFu) % size);
        const int32_t y = min_y + (int32_t)(((r >> 16) & 0xFFFFu) % size);
        const int32_t z = (int32_t)((r >> 32) % 48u);
        (void)world_edit_block_set(&test->edit, x, y, z, light_test_random_block(rng));
    }
}

/* ---- Worked out by hand ---------------------------------------------------------------------------------------------- */

TEST(an_empty_column_is_open_sky_and_takes_no_arrays) {
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    light_test_add_all(t, test);
    CHECK_EQ_U64(light_test_at(t, test, 3, 3, 1250), SECTION_LIGHT_OPEN_SKY);
    CHECK_EQ_U64(light_test_at(t, test, 3, 3, -250), SECTION_LIGHT_OPEN_SKY);
    CHECK_EQ_U64(test->pool.live_bytes, 0u);
    /* Nothing changed, so a renderer has nothing to fetch. */
    CHECK(!column_mask_any(world_edit_dirty_peek(&test->edit, 0u, WORLD_EDIT_CONSUMER_RENDER)));
    light_test_end(test);
}

TEST(a_torch_lights_by_manhattan_distance_over_sections_and_columns) {
    light_test_t *const test = light_test_begin(t, 1u);
    light_test_commit_all(t, test);
    light_test_add_all(t, test);
    /* At a corner of a column and of a section: chunk 0, x = 0, and the top of section 16. */
    light_test_set(t, test, 0, 0, 15, BLOCK_ID_TORCH);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    const uint32_t torch = block_defs[BLOCK_ID_TORCH].light;
    CHECK(torch > 3u);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 0, 0, 15)), torch);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, -1, 0, 15)), torch - 1u); /* the next column */
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 0, 0, 16)), torch - 1u);  /* the next section */
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, -2, 1, 16)), torch - 4u);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 0, 0, 15 + (int32_t)torch)), 0u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 0, 0, 16)), 15u);
    /* And out it goes, every bit. */
    light_test_set(t, test, 0, 0, 15, BLOCK_ID_AIR);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    for (uint32_t i = 0u; i < test->map.active_count; i++) {
        const column_t *const column = &test->map.borrowed_columns[test->map.borrowed_active[i]];
        for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
            CHECK(column->sections[s].borrowed_light == nullptr);
            CHECK_EQ_U64(column->sections[s].light_uniform, SECTION_LIGHT_OPEN_SKY);
        }
    }
    light_test_end(test);
}

TEST(a_roof_shuts_out_sky_and_a_hole_lets_a_shaft_of_it_down) {
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    light_test_floor(test, 0u);
    light_test_add_all(t, test);
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            light_test_set(t, test, x, y, 30, BLOCK_ID_STONE);
        }
    }
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 31)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 29)), 0u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 30)), 15u); /* the roof, whose top sees the sky */

    light_test_set(t, test, 8, 8, 30, BLOCK_ID_AIR);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 30)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 0)), 15u);  /* no loss, all the way down */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 8, 29)), 14u); /* one step aside */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 10, 9, 29)), 12u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 8, 30)),
                 15u); /* the roof itself, whose top sees the sky */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, -1)), 15u); /* the floor under the shaft */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 8, -1)), 14u); /* the floor one step aside */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, -2)), 0u);  /* and under that, stone on all sides */

    light_test_set(t, test, 8, 8, 30, BLOCK_ID_STONE);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 0)), 0u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 8, 29)), 0u);
    light_test_end(test);
}

TEST(water_and_leaves_take_what_their_opacity_says_and_a_lit_furnace_shines_out) {
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    light_test_floor(test, 0u);
    light_test_add_all(t, test);
    /* Walled, so that only what falls through the top comes in. */
    for (int32_t z = 0; z < 20; z++) {
        for (int32_t i = 0; i < 3; i++) {
            light_test_set(t, test, 3 + i, 3, z, BLOCK_ID_STONE);
            light_test_set(t, test, 3 + i, 5, z, BLOCK_ID_STONE);
        }
        light_test_set(t, test, 3, 4, z, BLOCK_ID_STONE);
        light_test_set(t, test, 5, 4, z, BLOCK_ID_STONE);
    }
    const uint32_t water = block_defs[BLOCK_ID_WATER].opacity;
    const uint32_t leaves = block_defs[BLOCK_ID_OAK_LEAVES].opacity;
    CHECK(water >= 2u);
    CHECK_EQ_U64(leaves, 1u);
    light_test_set(t, test, 4, 4, 19, BLOCK_ID_OAK_LEAVES);
    light_test_set(t, test, 4, 4, 18, BLOCK_ID_WATER);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 4, 4, 20)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 4, 4, 19)), 15u - leaves);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 4, 4, 18)), 15u - leaves - water);
    /* Under those, air: below 15, so each step costs 1. */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 4, 4, 17)), 15u - leaves - water - 1u);

    const uint32_t furnace = block_defs[BLOCK_ID_FURNACE_LIT].light;
    CHECK_EQ_U64(block_defs[BLOCK_ID_FURNACE_LIT].opacity, 15u);
    light_test_set(t, test, 10, 10, 5, BLOCK_ID_FURNACE_LIT);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 10, 5)), furnace);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 11, 10, 5)), furnace - 1u);
    /* A torch beside a furnace: the furnace keeps its own, and no more, since nothing gets in. */
    light_test_set(t, test, 11, 10, 5, BLOCK_ID_TORCH);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 10, 5)), furnace);
    light_test_end(test);
}

/* cuda/src/shade/ lights a block's faces by the byte in the block, since air is not on the card. If these go, the world goes black. */
TEST(an_opaque_block_holds_what_its_faces_see_and_passes_none_of_it_on) {
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    light_test_floor(test, 0u);
    light_test_add_all(t, test);
    const uint32_t torch = block_defs[BLOCK_ID_TORCH].light;
    /* The ground under open sky, and the stone under it. */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 5, 5, -1)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 5, 5, -2)), 0u);

    /* A wall one block thick: x = 8, with a closed room on the side away from the sky. */
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t z = 0; z < 6; z++) {
            light_test_set(t, test, 8, y, z, BLOCK_ID_STONE);
            light_test_set(t, test, 12, y, z, BLOCK_ID_STONE);
        }
        for (int32_t x = 8; x <= 12; x++) {
            light_test_set(t, test, x, y, 6, BLOCK_ID_STONE);
        }
    }
    light_test_set(t, test, 10, 8, 2, BLOCK_ID_TORCH);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    /* The wall sees 15 less a step sideways on its open side, and the torch 2 steps off on the other. */
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 8, 8, 2)), torch - 1u);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 7, 8, 2)), 0u); /* and none of it gets through */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 2)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 8, 2)), 0u); /* nor the sky the other way */

    /* Out goes the torch: the wall's faces see only what is left. */
    light_test_set(t, test, 10, 8, 2, BLOCK_ID_AIR);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 8, 8, 2)), 0u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 8, 2)), 15u);

    /* A lit furnace in a room, beside a torch that outshines it: in block light it holds its own, and light it gave goes with it
     * when it is swapped for stone, however bright its faces were. */
    light_test_set(t, test, 10, 4, 2, BLOCK_ID_FURNACE_LIT);
    light_test_set(t, test, 10, 3, 2, BLOCK_ID_TORCH);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    const uint32_t furnace = block_defs[BLOCK_ID_FURNACE_LIT].light;
    CHECK(torch > furnace);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 4, 2)), furnace);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 5, 2)), furnace - 1u);
    light_test_set(t, test, 10, 4, 2, BLOCK_ID_STONE);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 4, 2)), torch);
    /* The way past it is round: 3 sideways and over from the torch. */
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, 10, 5, 2)), torch - 4u);

    /* Dug out, it shows what is around it, as air. And the whole room against a rebuild. */
    const uint64_t written = test->light.rebuild_count;
    CHECK_EQ_U64(written, 0u);
    uint8_t before[64];
    for (uint32_t i = 0u; i < 64u; i++) {
        before[i] = light_test_at(t, test, (int32_t)(i % 16u), (int32_t)(i / 4u), 2);
    }
    CHECK_EQ_U64(world_light_rebuild(&test->light), WORLD_LIGHT_STATUS_OK);
    for (uint32_t i = 0u; i < 64u; i++) {
        CHECK_EQ_U64(before[i], light_test_at(t, test, (int32_t)(i % 16u), (int32_t)(i / 4u), 2));
    }
    light_test_end(test);
}

TEST(a_shaft_the_whole_height_of_storage_goes_dark_when_shut_at_the_top) {
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    /* All stone, and a shaft of air. Sections are written one voxel at a time, with no log. */
    column_t *const column = &test->map.borrowed_columns[0];
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        section_block_fill(&column->sections[s], &test->pool, BLOCK_ID_STONE);
    }
    for (int32_t z = COLUMN_Z_GAMEPLAY_MIN; z <= COLUMN_Z_GAMEPLAY_MAX; z++) {
        CHECK_EQ_U64(column_block_set(column, &test->pool, 7u, 7u, z, BLOCK_ID_AIR), COLUMN_STATUS_OK);
    }
    /* And open up to the sky at the very top of storage, which gameplay cannot reach. */
    for (uint32_t s = column_section_of(COLUMN_Z_GAMEPLAY_MAX) + 1u; s < COLUMN_SECTIONS; s++) {
        section_block_fill(&column->sections[s], &test->pool, BLOCK_ID_AIR);
    }
    for (int32_t z = COLUMN_Z_GAMEPLAY_MAX + 1; z < COLUMN_Z_STORAGE_END; z++) {
        for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
            for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
                section_t *const section = &column->sections[column_section_of(z)];
                CHECK(section_block_set(section, &test->pool, section_voxel_index(x, y, column_section_local_of(z)),
                                        BLOCK_ID_AIR));
            }
        }
    }
    light_test_add_all(t, test);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 7, 7, COLUMN_Z_GAMEPLAY_MIN)), 15u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 8, 7, COLUMN_Z_GAMEPLAY_MIN)), 15u); /* the shaft's wall */
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 9, 7, COLUMN_Z_GAMEPLAY_MIN)), 0u);  /* behind that */

    light_test_set(t, test, 7, 7, COLUMN_Z_GAMEPLAY_MAX, BLOCK_ID_STONE);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 7, 7, COLUMN_Z_GAMEPLAY_MIN)), 0u);
    CHECK_EQ_U64(test->light.rebuild_count, 0u); /* a wave did this */

    light_test_set(t, test, 7, 7, COLUMN_Z_GAMEPLAY_MAX, BLOCK_ID_AIR);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 7, 7, COLUMN_Z_GAMEPLAY_MIN)), 15u);
    light_test_end(test);
}

/* ---- The whole of it, against the reference -------------------------------------------------------------------------- */

TEST(the_reference_agrees_with_hand_worked_values) {
    /* If the reference were wrong the same way as the code, the comparisons below would prove nothing. */
    light_test_t *const test = light_test_begin(t, 0u);
    light_test_commit_all(t, test);
    light_test_floor(test, 0u);
    column_t *const column = &test->map.borrowed_columns[0];
    (void)column_block_set(column, &test->pool, 4u, 4u, 3, BLOCK_ID_TORCH);
    for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
        for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
            (void)column_block_set(column, &test->pool, x, y, 10, BLOCK_ID_STONE);
        }
    }
    light_reference_t reference;
    light_reference_of_band(t, &reference, 0, 0, 1u);
    light_reference_compute(&reference, &test->map);
    const uint32_t torch = block_defs[BLOCK_ID_TORCH].light;
    const uint32_t z_torch = (uint32_t)(3 - reference.min_z);
    const uint32_t z_roof = (uint32_t)(10 - reference.min_z);
    CHECK_EQ_U64(reference.borrowed_block[light_reference_index(&reference, 4u, 4u, z_torch)], torch);
    CHECK_EQ_U64(reference.borrowed_block[light_reference_index(&reference, 6u, 5u, z_torch)], torch - 3u);
    CHECK_EQ_U64(reference.borrowed_block[light_reference_index(&reference, 4u, 4u, z_roof)], 0u);
    CHECK_EQ_U64(reference.borrowed_sky[light_reference_index(&reference, 4u, 4u, z_roof)], 0u);
    CHECK_EQ_U64(reference.borrowed_sky[light_reference_index(&reference, 4u, 4u, z_roof - 1u)], 0u);
    CHECK_EQ_U64(reference.borrowed_sky[light_reference_index(&reference, 4u, 4u, z_roof + 1u)], 15u);
    /* The roof's own byte: 15 of sky from above, and from below the torch's 14 less the 6 steps to z 9. */
    const uint8_t roof = light_reference_wanted(&reference, 4u, 4u, z_roof);
    CHECK_EQ_U64(section_light_sky(roof), 15u);
    CHECK_EQ_U64(section_light_block(roof), torch - 6u);
    /* The floor's: 3 steps down from the torch, at z 0, is what it sees from z -1. */
    CHECK_EQ_U64(section_light_block(light_reference_wanted(&reference, 4u, 4u, 0u)), torch - 3u);
    light_test_end(test);
}

/* Random edits in batches, each followed by a comparison with a full recompute. A batch may hit one voxel twice, or undo itself. */
static void light_test_random_edits(test_context_t *t, uint64_t seed, bool roofed, uint32_t blocks_per_column,
                                    uint32_t rounds, uint32_t batch_max) {
    light_test_t *const test = light_test_begin(t, 2u); /* a circle: radius 1 would have no columns at the corners */
    uint64_t rng = seed;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, roofed, blocks_per_column);
    light_test_add_all(t, test);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    light_reference_t reference;
    light_reference_of_band(t, &reference, -2, -2, 5u);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    uint64_t pool_mark = test->arena.used;
    for (uint32_t round = 0u; round < rounds; round++) {
        light_test_edit_randomly(test, &rng, 1u + (uint32_t)(light_test_random(&rng) % batch_max), -16, -16, 48u);
        CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
        light_reference_compute(&reference, &test->map);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    }
    /* Passes alone did this: no fallback hid a wrong one, and the arena gave nothing more. */
    CHECK_EQ_U64(test->light.rebuild_count, 0u);
    CHECK_EQ_U64(test->arena.used, pool_mark);
    /* The one full recompute of the code under test gives the same bytes. */
    CHECK_EQ_U64(world_light_rebuild(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    light_test_end(test);
}

TEST(random_edits_one_at_a_time_match_a_full_recompute) {
    light_test_random_edits(t, 0x1234567890ABCDEFull, true, 300u, 60u, 1u);
}

TEST(random_edits_under_open_sky_match_a_full_recompute) {
    light_test_random_edits(t, 0xFEDCBA0987654321ull, false, 600u, 40u, 40u);
}

TEST(random_edits_in_batches_under_a_roof_match_a_full_recompute) {
    light_test_random_edits(t, 0x0F0F0F0F12345678ull, true, 1200u, 40u, 40u);
}

TEST(batches_longer_than_one_read_of_the_log_match_a_full_recompute) {
    light_test_random_edits(t, 0x5555AAAA5555AAAAull, true, 600u, 6u, 3000u);
}

TEST(a_block_put_in_a_column_not_lit_yet_is_seen_when_it_is) {
    light_test_t *const test = light_test_begin(t, 1u);
    uint64_t rng = 99u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 200u);
    /* Half the world arrives, edits land on both halves, and then the rest. */
    for (uint32_t i = 0u; i < test->map.active_count / 2u; i++) {
        CHECK_EQ_U64(world_light_column_added(&test->light, test->map.borrowed_active[i]), WORLD_LIGHT_STATUS_OK);
    }
    light_test_edit_randomly(test, &rng, 500u, -16, -16, 48u);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    light_test_edit_randomly(test, &rng, 500u, -16, -16, 48u);
    light_test_add_all(t, test);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    light_reference_t reference;
    light_reference_of_band(t, &reference, -1, -1, 3u);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    /* Asking twice changes nothing. */
    const uint64_t written = test->light.voxels_written;
    light_test_add_all(t, test);
    CHECK_EQ_U64(test->light.voxels_written, written);
    light_test_end(test);
}

/* ---- Columns coming and going ------------------------------------------------------------------------------------------ */

TEST(light_does_not_depend_on_the_order_columns_arrive_in) {
    /* Each order against the reference, so that all of them are against each other. */
    for (uint64_t order = 0u; order < 3u; order++) {
        light_test_t *const test = light_test_begin(t, 1u);
        uint64_t rng = 777u;
        light_test_commit_all(t, test);
        light_test_scatter(test, &rng, true, 800u);
        uint32_t remaining[64];
        const uint32_t count = test->map.active_count;
        REQUIRE(count >= 5u && count <= 64u);
        memcpy(remaining, test->map.borrowed_active, count * sizeof(uint32_t));
        /* The same blocks each time, since rng starts the same. Another order, from a shuffle by another seed. */
        uint64_t shuffle = 0xABCDu + order * 7919u;
        for (uint32_t i = count; i > 1u; i--) {
            const uint32_t j = (uint32_t)(light_test_random(&shuffle) % i);
            const uint32_t swap = remaining[i - 1u];
            remaining[i - 1u] = remaining[j];
            remaining[j] = swap;
        }
        for (uint32_t i = 0u; i < count; i++) {
            CHECK_EQ_U64(world_light_column_added(&test->light, remaining[i]), WORLD_LIGHT_STATUS_OK);
        }
        light_reference_t reference;
        light_reference_of_band(t, &reference, -1, -1, 3u);
        light_reference_compute(&reference, &test->map);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
        CHECK_EQ_U64(test->light.rebuild_count, 0u);
        light_test_end(test);
    }
}

/* Sections all of one state hold one value and no array, and a column skips looking at any that nothing could raise. This is
 * what could go wrong with that: one value beside a very different one, on each side, in each order. */
TEST(sections_of_one_value_beside_very_different_ones_are_still_raised) {
    for (uint64_t order = 0u; order < 2u; order++) {
        light_test_t *const test = light_test_begin_sized(t, 1u, 1ull << 18, LIGHT_TEST_LOG, 32ull << 20);
        light_test_commit_all(t, test);
        for (uint32_t i = 0u; i < test->map.active_count; i++) {
            light_test_floor(test, test->map.borrowed_active[i]);
        }
        const uint32_t middle = light_test_slot(test, 0, 0);
        const uint32_t east = light_test_slot(test, 1, 0);
        const uint32_t north = light_test_slot(test, 0, 1);
        /* A section of torches at z 16 to 31: the sections over and under it, and beside it to the north, hold 0 block light. */
        section_block_fill(&test->map.borrowed_columns[middle].sections[17], &test->pool, BLOCK_ID_TORCH);
        /* A roof at z 32 to 47, all of one state: under it to the east is one value of no sky, beside open sky in the middle. */
        section_block_fill(&test->map.borrowed_columns[east].sections[18], &test->pool, BLOCK_ID_STONE);
        section_block_fill(&test->map.borrowed_columns[north].sections[18], &test->pool, BLOCK_ID_STONE);
        if (order == 0u) {
            light_test_add_all(t, test);
        } else {
            for (uint32_t i = test->map.active_count; i > 0u; i--) {
                CHECK_EQ_U64(world_light_column_added(&test->light, test->map.borrowed_active[i - 1u]),
                             WORLD_LIGHT_STATUS_OK);
            }
        }
        const uint32_t torch = block_defs[BLOCK_ID_TORCH].light;
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 5, 5, 15)), torch - 1u); /* under */
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 5, 5, 32)), torch - 1u); /* over */
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 5, 16, 20)), torch - 1u);
        CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 16, 5, 31)), 14u); /* under the roof, beside the sky */
        CHECK_EQ_U64(section_light_sky(light_test_at(t, test, 17, 5, 31)), 13u);
        light_reference_t reference;
        light_reference_of_band(t, &reference, -1, -1, 3u);
        light_reference_compute(&reference, &test->map);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
        CHECK_EQ_U64(test->light.rebuild_count, 0u);
        /* And the rebuild, which skips by the same test. */
        CHECK_EQ_U64(world_light_rebuild(&test->light), WORLD_LIGHT_STATUS_OK);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
        light_test_end(test);
    }
}

/* No block in config/blocks/ gives off light that leaves two sections of one value exactly 2 apart, so the numbers are written in by
 * hand: a difference of 2 is the smallest that sends light over a column's edge. */
TEST(sections_of_one_value_two_apart_across_a_column_edge_are_still_raised) {
    enum : uint32_t {
        SECTION_INDEX = 20u, /* z from 64 to 79 */
    };
    const int32_t z = 70;
    /* The new column is the darker. */
    {
        light_test_t *const test = light_test_begin(t, 1u);
        light_test_commit_all(t, test);
        const uint32_t middle = light_test_slot(test, 0, 0);
        CHECK_EQ_U64(world_light_column_added(&test->light, middle), WORLD_LIGHT_STATUS_OK);
        section_light_fill(&test->map.borrowed_columns[middle].sections[SECTION_INDEX], &test->pool,
                           section_light_pack(SECTION_LIGHT_MAX, 2u));
        CHECK_EQ_U64(world_light_column_added(&test->light, light_test_slot(test, 1, 0)), WORLD_LIGHT_STATUS_OK);
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 16, 5, z)), 1u);
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 17, 5, z)), 0u);
        light_test_end(test);
    }
    /* The new column is the brighter: torches, 14, beside 12. */
    {
        light_test_t *const test = light_test_begin(t, 1u);
        light_test_commit_all(t, test);
        const uint32_t middle = light_test_slot(test, 0, 0);
        const uint32_t east = light_test_slot(test, 1, 0);
        CHECK_EQ_U64(world_light_column_added(&test->light, middle), WORLD_LIGHT_STATUS_OK);
        section_light_fill(&test->map.borrowed_columns[middle].sections[SECTION_INDEX], &test->pool,
                           section_light_pack(SECTION_LIGHT_MAX, 12u));
        section_block_fill(&test->map.borrowed_columns[east].sections[SECTION_INDEX], &test->pool, BLOCK_ID_TORCH);
        CHECK_EQ_U64(block_defs[BLOCK_ID_TORCH].light, 14u);
        CHECK_EQ_U64(world_light_column_added(&test->light, east), WORLD_LIGHT_STATUS_OK);
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 15, 5, z)), 13u);
        CHECK_EQ_U64(section_light_block(light_test_at(t, test, 14, 5, z)), 12u);
        light_test_end(test);
    }
}

TEST(a_column_leaving_takes_its_light_out_of_its_neighbours) {
    light_test_t *const test = light_test_begin(t, 1u);
    light_test_commit_all(t, test);
    light_test_add_all(t, test);
    /* A torch right at the edge of the middle column shines into the one to the west. */
    light_test_set(t, test, 0, 8, 20, BLOCK_ID_TORCH);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    const uint32_t torch = block_defs[BLOCK_ID_TORCH].light;
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, -1, 8, 20)), torch - 1u);
    const uint32_t middle = light_test_slot(test, 0, 0);
    CHECK_EQ_U64(world_light_column_leaving(&test->light, middle), WORLD_LIGHT_STATUS_OK);
    world_map_unload(&test->map, middle);
    CHECK_EQ_U64(section_light_block(light_test_at(t, test, -1, 8, 20)), 0u);
    CHECK_EQ_U64(section_light_sky(light_test_at(t, test, -1, 8, 20)), 15u);
    light_test_end(test);
}

TEST(a_random_walk_of_columns_coming_and_going_matches_a_full_recompute) {
    light_test_t *const test = light_test_begin(t, 2u);
    world_map_set_view_radius(&test->map, 1u);
    uint64_t rng = 0xC0FFEEull;
    /* The box has to hold every place the map may go, so it is large and the walk is small. */
    light_reference_t reference;
    light_reference_of_band(t, &reference, -5, -5, 11u);
    int32_t center_x = 0;
    int32_t center_y = 0;
    for (uint32_t round = 0u; round < 16u; round++) {
        const uint64_t r = light_test_random(&rng);
        center_x += (int32_t)(r % 3u) - 1;
        center_y += (int32_t)((r >> 8) % 3u) - 1;
        center_x = center_x < -2 ? -2 : (center_x > 2 ? 2 : center_x);
        center_y = center_y < -2 ? -2 : (center_y > 2 ? 2 : center_y);
        world_map_set_center(&test->map, center_x, center_y);
        world_map_leaving_t leaving;
        while (world_map_stream_leaving(&test->map, &leaving)) {
            CHECK_EQ_U64(world_light_column_leaving(&test->light, leaving.slot), WORLD_LIGHT_STATUS_OK);
            world_map_unload(&test->map, leaving.slot);
        }
        world_map_request_t request;
        while (world_map_stream_take(&test->map, &request, 1u) == 1u) {
            column_t *const column = world_map_stream_commit(&test->map, &request);
            REQUIRE(column != nullptr);
            for (uint32_t s = 0u; s < LIGHT_TEST_FLOOR_SECTIONS; s++) {
                section_block_fill(&column->sections[s], &test->pool, BLOCK_ID_STONE);
            }
            for (uint32_t b = 0u; b < 400u; b++) {
                const uint64_t q = light_test_random(&rng);
                (void)column_block_set(column, &test->pool, (uint32_t)(q & 15u), (uint32_t)((q >> 4) & 15u),
                                       (int32_t)((q >> 8) % 48u), light_test_random_block(&rng));
            }
            world_edit_column_filled(&test->edit, request.slot);
            CHECK_EQ_U64(world_light_column_added(&test->light, request.slot), WORLD_LIGHT_STATUS_OK);
        }
        light_test_edit_randomly(test, &rng, 60u, (center_x - 1) * 16, (center_y - 1) * 16, 48u);
        CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
        light_reference_compute(&reference, &test->map);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    }
    CHECK_EQ_U64(test->light.rebuild_count, 0u);
    light_test_end(test);
}

TEST(sections_written_past_the_log_are_put_right_when_told_of) {
    light_test_t *const test = light_test_begin(t, 1u);
    uint64_t rng = 31337u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 400u);
    light_test_add_all(t, test);
    /* Sections 16 and 18: the first full of air, and the second gets new blocks, none of them in the log. */
    const uint32_t slot = light_test_slot(test, 0, 0);
    column_t *const column = &test->map.borrowed_columns[slot];
    section_block_fill(&column->sections[16], &test->pool, BLOCK_ID_AIR);
    for (uint32_t i = 0u; i < 400u; i++) {
        const uint64_t q = light_test_random(&rng);
        CHECK(section_block_set(&column->sections[18], &test->pool, (uint32_t)(q & (SECTION_VOXELS - 1u)),
                                light_test_random_block(&rng)));
        CHECK(section_block_set(&column->sections[18], &test->pool, (uint32_t)((q >> 16) & (SECTION_VOXELS - 1u)),
                                light_test_random_block(&rng)));
    }
    CHECK_EQ_U64(
        world_light_sections_replaced(&test->light, slot, column_mask_or(column_mask_of(16u), column_mask_of(18u))),
        WORLD_LIGHT_STATUS_OK);
    light_reference_t reference;
    light_reference_of_band(t, &reference, -1, -1, 3u);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    CHECK_EQ_U64(test->light.rebuild_count, 0u);
    light_test_end(test);
}

/* ---- Running out ------------------------------------------------------------------------------------------------------- */

TEST(running_out_of_room_in_a_pass_falls_back_and_is_still_correct) {
    /* So small that every pass that does anything runs out. */
    light_test_t *const test = light_test_begin_sized(t, 1u, 64u, LIGHT_TEST_LOG, LIGHT_TEST_POOL_BYTES);
    uint64_t rng = 2024u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 300u);
    light_test_add_all(t, test);
    CHECK(test->light.overflow_count > 0u);
    CHECK_EQ_U64(test->light.overflow_count, test->light.rebuild_count);
    light_reference_t reference;
    light_reference_of_band(t, &reference, -1, -1, 3u);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    for (uint32_t round = 0u; round < 6u; round++) {
        light_test_edit_randomly(test, &rng, 300u, -16, -16, 48u);
        CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
        light_reference_compute(&reference, &test->map);
        CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    }
    /* A column leaving, too. */
    const uint32_t middle = light_test_slot(test, 0, 0);
    CHECK_EQ_U64(world_light_column_leaving(&test->light, middle), WORLD_LIGHT_STATUS_OK);
    world_map_unload(&test->map, middle);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    light_test_end(test);
}

TEST(a_log_that_lost_records_falls_back_and_is_still_correct) {
    light_test_t *const test =
        light_test_begin_sized(t, 1u, WORLD_LIGHT_WAVE_CAPACITY_DEFAULT, 64u, LIGHT_TEST_POOL_BYTES);
    uint64_t rng = 4096u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 300u);
    light_test_add_all(t, test);
    light_test_edit_randomly(test, &rng, 1000u, -16, -16, 48u);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK_EQ_U64(test->light.lost_count, 1u);
    CHECK_EQ_U64(test->light.rebuild_count, 1u);
    light_reference_t reference;
    light_reference_of_band(t, &reference, -1, -1, 3u);
    light_reference_compute(&reference, &test->map);
    CHECK_EQ_U64(light_reference_differences(&reference, &test->map), 0u);
    light_test_end(test);
}

TEST(a_full_pool_is_reported_and_is_put_right_once_there_is_room) {
    /* Room for 24 arrays of light, and the blocks of three columns' sections are all one state, so need none. */
    light_test_t *const test =
        light_test_begin_sized(t, 1u, WORLD_LIGHT_WAVE_CAPACITY_DEFAULT, LIGHT_TEST_LOG, 24u * 4096u);
    light_test_commit_all(t, test);
    light_test_add_all(t, test);
    /* Lights in many sections want arrays: more than there is room for. */
    for (int32_t z = 0; z < 16 * 40; z += 16) {
        const int32_t place = z - 200;
        const world_edit_status_t status = world_edit_block_set(&test->edit, 2, 2, place, BLOCK_ID_FURNACE_LIT);
        CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_NO_MEMORY);
    }
    const world_light_status_t full = world_light_update(&test->light);
    CHECK_EQ_U64(full, WORLD_LIGHT_STATUS_NO_MEMORY);
    CHECK(test->pool.failed_count > 0u);
    /* It goes on saying so while nothing has changed, and does not sweep the world over to find out. */
    const uint64_t rebuilds = test->light.rebuild_count;
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_NO_MEMORY);
    CHECK_EQ_U64(test->light.rebuild_count, rebuilds);

    /* Taking back most of what was asked for leaves room. Each edit is air where there was a furnace. */
    for (int32_t z = 0; z < 16 * 40; z += 16) {
        const int32_t place = z - 200;
        uint16_t block = 0u;
        if (world_map_block_get(&test->map, 2, 2, place, &block) == WORLD_MAP_STATUS_OK && block != BLOCK_ID_AIR) {
            CHECK_EQ_U64(world_edit_block_set(&test->edit, 2, 2, place, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
        }
    }
    /* And a column going away gives back what it held. */
    for (uint32_t i = test->map.active_count; i > 0u; i--) {
        const uint32_t slot = test->map.borrowed_active[i - 1u];
        if (slot != light_test_slot(test, 0, 0)) {
            (void)world_light_column_leaving(&test->light, slot);
            world_map_unload(&test->map, slot);
        }
    }
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    CHECK(test->light.rebuild_count > rebuilds);
    /* With no blocks left and one column, it is all open sky again. */
    const column_t *const column = &test->map.borrowed_columns[light_test_slot(test, 0, 0)];
    for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
        for (uint32_t i = 0u; i < SECTION_VOXELS; i += 97u) {
            CHECK_EQ_U64(section_light_get(&column->sections[s], i), SECTION_LIGHT_OPEN_SKY);
        }
    }
    light_test_end(test);
}

/* ---- Flags -------------------------------------------------------------------------------------------------------------- */

TEST(every_section_whose_light_changed_is_flagged_for_render_and_not_for_save) {
    light_test_t *const test = light_test_begin(t, 1u);
    uint64_t rng = 555u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 300u);
    light_test_add_all(t, test);
    CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
    const uint32_t columns = test->map.active_count;
    REQUIRE(columns <= 16u);
    uint8_t *const before = test_alloc(t, 16ull * COLUMN_SECTIONS * SECTION_VOXELS);
    for (uint32_t round = 0u; round < 20u; round++) {
        for (uint32_t i = 0u; i < columns; i++) {
            const uint32_t slot = test->map.borrowed_active[i];
            (void)world_edit_dirty_peek(&test->edit, slot, WORLD_EDIT_CONSUMER_RENDER);
            world_edit_dirty_clear(&test->edit, slot, WORLD_EDIT_CONSUMER_RENDER, WORLD_EDIT_SECTIONS_ALL);
            world_edit_dirty_clear(&test->edit, slot, WORLD_EDIT_CONSUMER_SAVE, WORLD_EDIT_SECTIONS_ALL);
            for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
                section_lights_export(&test->map.borrowed_columns[slot].sections[s],
                                      before + ((uint64_t)i * COLUMN_SECTIONS + s) * SECTION_VOXELS);
            }
        }
        light_test_edit_randomly(test, &rng, 5u, -16, -16, 48u);
        column_mask_t edited[16];
        for (uint32_t i = 0u; i < columns; i++) {
            edited[i] = world_edit_dirty_peek(&test->edit, test->map.borrowed_active[i], WORLD_EDIT_CONSUMER_SAVE);
        }
        CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
        uint8_t now[SECTION_VOXELS];
        for (uint32_t i = 0u; i < columns; i++) {
            const uint32_t slot = test->map.borrowed_active[i];
            const column_mask_t render = world_edit_dirty_peek(&test->edit, slot, WORLD_EDIT_CONSUMER_RENDER);
            CHECK(!column_mask_any(world_edit_dirty_peek(&test->edit, slot, WORLD_EDIT_CONSUMER_LIGHT)));
            /* Only the blocks themselves are for saving. Light is not. */
            CHECK(column_mask_equal(world_edit_dirty_peek(&test->edit, slot, WORLD_EDIT_CONSUMER_SAVE), edited[i]));
            for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
                section_lights_export(&test->map.borrowed_columns[slot].sections[s], now);
                if (memcmp(now, before + ((uint64_t)i * COLUMN_SECTIONS + s) * SECTION_VOXELS, SECTION_VOXELS) != 0) {
                    CHECK(column_mask_has(render, s));
                }
            }
        }
    }
    /* What a pass leaves is compact: no section that all holds one value keeps an array. */
    for (uint32_t i = 0u; i < columns; i++) {
        const column_t *const column = &test->map.borrowed_columns[test->map.borrowed_active[i]];
        for (uint32_t s = LIGHT_TEST_FLOOR_SECTIONS + 4u; s < COLUMN_SECTIONS; s++) {
            CHECK(column->sections[s].borrowed_light == nullptr);
        }
    }
    light_test_end(test);
}

TEST(world_open_starts_light_and_a_real_world_uses_it) {
    world_t *const world = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const aligned = (world_t *)(((uintptr_t)world + 63u) & ~(uintptr_t)63u);
    const world_limits_t limits = {.section_bytes = 64ull << 20, .column_count = 9u};
    REQUIRE(world_init(aligned, limits));
    REQUIRE(world_open(aligned, 1u, 0, 0));
    world_map_request_t request;
    while (world_map_stream_take(&aligned->map, &request, 1u) == 1u) {
        CHECK(world_map_stream_commit(&aligned->map, &request) != nullptr);
        CHECK_EQ_U64(world_light_column_added(&aligned->light, request.slot), WORLD_LIGHT_STATUS_OK);
    }
    CHECK_EQ_U64(world_edit_block_set(&aligned->edit, 15, 15, 0, BLOCK_ID_TORCH), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_light_update(&aligned->light), WORLD_LIGHT_STATUS_OK);
    uint8_t packed = 0u;
    CHECK_EQ_U64(world_map_light_get(&aligned->map, 16, 15, 0, &packed), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(section_light_block(packed), block_defs[BLOCK_ID_TORCH].light - 1u);
    world_close(aligned);
    world_deinit(aligned);
}

/* ---- For the stage file: it checks nothing, since a timing on a shared machine would make a flaky test -------------------- */

TEST(cost_on_this_machine) {
    light_test_t *const test = light_test_begin(t, 2u);
    uint64_t rng = 8675309u;
    light_test_commit_all(t, test);
    light_test_scatter(test, &rng, true, 2000u);
    const int64_t added_start = time_now_ns();
    light_test_add_all(t, test);
    const int64_t added_ns = time_now_ns() - added_start;
    const int64_t per_column = added_ns / (int64_t)test->map.active_count;

    const int32_t edits = 2000;
    int64_t passes_ns = 0;
    for (int32_t i = 0; i < edits; i++) {
        const uint64_t r = light_test_random(&rng);
        (void)world_edit_block_set(&test->edit, (int32_t)(r % 80u) - 40, (int32_t)((r >> 16) % 80u) - 40,
                                   (int32_t)((r >> 32) % 48u), light_test_random_block(&rng));
        const int64_t pass_start = time_now_ns();
        CHECK_EQ_U64(world_light_update(&test->light), WORLD_LIGHT_STATUS_OK);
        passes_ns += time_now_ns() - pass_start;
    }

    const int64_t rebuild_start = time_now_ns();
    CHECK_EQ_U64(world_light_rebuild(&test->light), WORLD_LIGHT_STATUS_OK);
    const int64_t rebuild_ns = time_now_ns() - rebuild_start;

    printf("a new column with 2000 scattered blocks: %" PRId64 " us; one edit's pass: %" PRId64
           " us on average; a rebuild of %" PRIu32 " columns: %" PRId64 " us\n",
           per_column / 1000, passes_ns / edits / 1000, test->map.active_count, rebuild_ns / 1000);
    light_test_end(test);
}

TEST_MAIN()
