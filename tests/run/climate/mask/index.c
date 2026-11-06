/* ctest run_climate_mask. See README.md. */
#include "harness/index.h"

#include "climate/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"

#include <stdatomic.h>
#include <string.h>

static climate_mask_t mask_table(test_context_t *t, const ground_t *ground) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, (uint64_t)CLIMATE_MASK_SLOTS * sizeof(climate_slot_t) + 64u);
    climate_slot_t *const slots = (climate_slot_t *)((raw + 63u) & ~(uintptr_t)63u);
    climate_cache_empty(slots, CLIMATE_MASK_SLOTS);
    return (climate_mask_t){
        .borrowed_slots = slots,
        .borrowed_heights = test_alloc(t, CLIMATE_MASK_BYTES),
        .ground = ground_fn,
        .borrowed_ground_context = ground,
    };
}

static void mask_terrain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                uint32_t count_x, uint32_t count_y, float32_t *heights) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights);
}

TEST(a_page_holds_the_ground_at_each_cell_centre) {
    const ground_t ground = {.kind = GROUND_PLANE};
    float32_t page[CLIMATE_TILE_CELL_COUNT];
    climate_mask_page_build(ground_fn, &ground, 2, -3, page);
    uint64_t right = 0u;
    for (int32_t j = 0; j < CLIMATE_TILE_CELLS; j++) {
        for (int32_t i = 0; i < CLIMATE_TILE_CELLS; i++) {
            const float32_t expected = (float32_t)(2048 + 32 + 64 * i - 3 * (-3072 + 32 + 64 * j));
            right += page[j * CLIMATE_TILE_CELLS + i] == expected ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(right, (uint64_t)CLIMATE_TILE_CELL_COUNT);
}

TEST(a_page_from_the_real_terrain_is_the_height_estimate) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, 0x1234567890ABCDEFull));
    static const int32_t TILES[3][2] = {{0, 0}, {-1, -1}, {5, -9}};
    uint64_t same = 0u;
    for (uint32_t k = 0u; k < 3u; k++) {
        float32_t page[CLIMATE_TILE_CELL_COUNT];
        climate_mask_page_build(mask_terrain_ground, &terrain, TILES[k][0], TILES[k][1], page);
        for (int32_t j = 0; j < CLIMATE_TILE_CELLS; j++) {
            for (int32_t i = 0; i < CLIMATE_TILE_CELLS; i++) {
                const float32_t one = terrain_height_estimate(&terrain, TILES[k][0] * 1024 + 32 + 64 * i,
                                                              TILES[k][1] * 1024 + 32 + 64 * j);
                same += memcmp(&one, &page[j * CLIMATE_TILE_CELLS + i], sizeof(one)) == 0 ? 1u : 0u;
            }
        }
    }
    CHECK_EQ_U64(same, 3u * CLIMATE_TILE_CELL_COUNT);
    terrain_deinit(&terrain);
}

static uint64_t mask_window_right(const ground_t *ground, const float32_t *window, int32_t tile_x, int32_t tile_y) {
    uint64_t right = 0u;
    for (int32_t j = 0; j < CLIMATE_WINDOW_CELLS; j++) {
        for (int32_t i = 0; i < CLIMATE_WINDOW_CELLS; i++) {
            const int64_t x = (int64_t)(tile_x - 8) * 1024 + 64 * i + 32;
            const int64_t y = (int64_t)(tile_y - 8) * 1024 + 64 * j + 32;
            right += window[j * CLIMATE_WINDOW_CELLS + i] == ground_height(ground, x, y) ? 1u : 0u;
        }
    }
    return right;
}

TEST(a_window_is_its_seventeen_by_seventeen_pages_in_place) {
    const ground_t ground = {.kind = GROUND_PLANE};
    const climate_mask_t mask = mask_table(t, &ground);
    float32_t *const window = test_alloc(t, CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t));
    uint32_t sources[3] = {0u, 0u, 0u};
    climate_mask_window(&mask, 3, -2, 3, -2, window, sources);
    CHECK_EQ_U64(mask_window_right(&ground, window, 3, -2), (uint64_t)CLIMATE_WINDOW_CELL_COUNT);
    CHECK_EQ_U64(sources[CLIMATE_MASK_BUILT_KEPT], 289u);
    /* Again: every page from the table this time, and the same values. */
    memset(window, 0, CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t));
    uint32_t again[3] = {0u, 0u, 0u};
    climate_mask_window(&mask, 3, -2, 3, -2, window, again);
    CHECK_EQ_U64(again[CLIMATE_MASK_FROM_TABLE], 289u);
    CHECK_EQ_U64(mask_window_right(&ground, window, 3, -2), (uint64_t)CLIMATE_WINDOW_CELL_COUNT);
    /* Far from the keep square, nothing is kept, and the window is still right. */
    uint32_t far[3] = {0u, 0u, 0u};
    climate_mask_window(&mask, 300, 0, 3, -2, window, far);
    CHECK_EQ_U64(far[CLIMATE_MASK_BUILT_COPY], 289u);
    CHECK_EQ_U64(mask_window_right(&ground, window, 300, 0), (uint64_t)CLIMATE_WINDOW_CELL_COUNT);
}

TEST(a_page_claimed_by_another_is_copied_privately_and_matches) {
    const ground_t ground = {.kind = GROUND_PLANE};
    const climate_mask_t mask = mask_table(t, &ground);
    climate_slot_t *const held = &mask.borrowed_slots[climate_cache_slot_index(0, 0, CLIMATE_MASK_SIDE)];
    REQUIRE(climate_cache_claim(held, climate_cache_key(0, 0), true) == CLIMATE_CLAIM_OURS);
    float32_t *const window = test_alloc(t, CLIMATE_WINDOW_CELL_COUNT * sizeof(float32_t));
    uint32_t sources[3] = {0u, 0u, 0u};
    climate_mask_window(&mask, 0, 0, 0, 0, window, sources);
    CHECK_EQ_U64(mask_window_right(&ground, window, 0, 0), (uint64_t)CLIMATE_WINDOW_CELL_COUNT);
    CHECK_EQ_U64(sources[CLIMATE_MASK_BUILT_COPY], 1u);
    CHECK_EQ_U64(sources[CLIMATE_MASK_BUILT_KEPT], 288u);
    CHECK_EQ_U64(atomic_load(&held->state), CLIMATE_SLOT_CLAIMED);
}

TEST_MAIN()
