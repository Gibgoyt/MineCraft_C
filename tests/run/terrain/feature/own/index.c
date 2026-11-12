/* ctest run_terrain_feature_own. See README.md. */
#include "harness/index.h"

#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    OWN_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    OWN_VOXELS = TERRAIN_FEATURE_CHUNK_COLUMNS * OWN_LAYERS,
    OWN_SEEDS = 3u,
    /* Chunks far enough apart to land on different ground, and enough of them to have sea, peaks and caves through a top. */
    OWN_GRID_SIDE = 12u,
    OWN_GRID_STEP = 37u,
    /* How far out to look for chunks with a lake, and how many to run. */
    OWN_LAKE_SEARCH_SIDE = 96u,
    OWN_LAKES = 6u,
};

static const uint64_t OWN_WORLDS[OWN_SEEDS] = {TERRAIN_SEED_DEFAULT, 2198175988018515243u, 11u};

static terrain_density_box_t own_box(int32_t chunk_x, int32_t chunk_y) {
    return (terrain_density_box_t){
        .origin_x = chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_y = chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = TERRAIN_FEATURE_CHUNK,
        .count_y = TERRAIN_FEATURE_CHUNK,
        .count_z = OWN_LAYERS,
    };
}

/* The stages as far as terrain_generate_box has them when it reads a chunk's tops. */
static void own_stages_to_flood(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                                uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    terrain_cave_cut_with_mask(box, opened, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
}

/* Every stage, by hand, with features finding the chunk's ground for themselves, by walking down it in slices. */
static void own_by_hand(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    own_stages_to_flood(terrain, box, opened, blocks);
    terrain_surface_line_caves_box(terrain->borrowed_surface, box, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
    terrain_feature_fill_box(terrain->borrowed_feature, box, blocks);
}

TEST(a_chunks_tops_read_from_its_box_are_what_the_walk_down_finds) {
    uint16_t *const blocks = test_alloc(t, (uint64_t)OWN_VOXELS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, OWN_VOXELS);
    uint64_t chunks = 0u, wet = 0u, dry = 0u, cut_away = 0u;
    for (uint32_t w = 0u; w < OWN_SEEDS; w++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, OWN_WORLDS[w]));
        for (uint32_t gy = 0u; gy < OWN_GRID_SIDE; gy++) {
            for (uint32_t gx = 0u; gx < OWN_GRID_SIDE; gx++) {
                const int32_t chunk_x = ((int32_t)gx - (int32_t)OWN_GRID_SIDE / 2) * (int32_t)OWN_GRID_STEP;
                const int32_t chunk_y = ((int32_t)gy - (int32_t)OWN_GRID_SIDE / 2) * (int32_t)OWN_GRID_STEP;
                const terrain_density_box_t box = own_box(chunk_x, chunk_y);
                own_stages_to_flood(&terrain, &box, opened, blocks);
                terrain_feature_chunk_t read;
                REQUIRE(terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
                terrain_feature_column_t walked[TERRAIN_FEATURE_CHUNK_COLUMNS];
                terrain_feature_chunk_columns(terrain.borrowed_feature, chunk_x, chunk_y, walked);
                CHECK(memcmp(read.columns, walked, sizeof(walked)) == 0);
                chunks++;
                for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
                    if (walked[i].top == TERRAIN_FEATURE_NO_GROUND) {
                        cut_away++;
                    } else if (walked[i].water > 0u) {
                        wet++;
                    } else {
                        dry++;
                    }
                }
            }
        }
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " chunks: %" PRIu64 " dry columns, %" PRIu64 " under water, %" PRIu64
           " with no ground in range\n",
           chunks, dry, wet, cut_away);
    /* A walk over nothing but one sort of ground would say little. */
    CHECK(dry > 0u);
    CHECK(wet > 0u);
}

TEST(every_stage_gives_the_same_bytes_as_features_walking_the_chunk_for_themselves) {
    uint16_t *const by_hand = test_alloc(t, (uint64_t)OWN_VOXELS * sizeof(uint16_t));
    uint16_t *const generated = test_alloc(t, (uint64_t)OWN_VOXELS * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, OWN_VOXELS);
    uint8_t *const by_hand_opened = test_alloc(t, OWN_VOXELS);
    uint64_t chunks = 0u, lakes = 0u, cut = 0u, wanted_only = 0u;
    for (uint32_t w = 0u; w < OWN_SEEDS; w++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, OWN_WORLDS[w]));
        /* A spread of ground. */
        for (uint32_t g = 0u; g < OWN_GRID_SIDE * 3u; g++) {
            const int32_t chunk_x =
                ((int32_t)(g % OWN_GRID_SIDE) - (int32_t)OWN_GRID_SIDE / 2) * (int32_t)OWN_GRID_STEP;
            const int32_t chunk_y = ((int32_t)(g / 3u) - (int32_t)OWN_GRID_SIDE / 2) * (int32_t)OWN_GRID_STEP + 5;
            const terrain_density_box_t box = own_box(chunk_x, chunk_y);
            own_by_hand(&terrain, &box, by_hand_opened, by_hand);
            terrain_generate_box_fallback(&terrain, &box, opened, generated);
            CHECK(memcmp(by_hand, generated, (uint64_t)OWN_VOXELS * sizeof(uint16_t)) == 0);
            /* What the map and walk tools read from opened: where a cave truly cut, and no more. */
            uint64_t differ = 0u;
            for (uint64_t i = 0u; i < OWN_VOXELS; i++) {
                const bool says_cut = (opened[i] & ~(uint32_t)TERRAIN_CAVE_WANTED_UNCUT) != 0u;
                differ += says_cut != (by_hand_opened[i] != 0u) ? 1u : 0u;
                cut += says_cut ? 1u : 0u;
                wanted_only += opened[i] == TERRAIN_CAVE_WANTED_UNCUT ? 1u : 0u;
            }
            CHECK_EQ_U64(differ, 0u);
            chunks++;
        }
        /* And chunks with a lake, the one case that reads the surface's noise only when it gets there. */
        uint32_t found = 0u;
        for (uint32_t i = 0u; i < OWN_LAKE_SEARCH_SIDE * OWN_LAKE_SEARCH_SIDE && found < OWN_LAKES; i++) {
            const int32_t chunk_x = (int32_t)(i % OWN_LAKE_SEARCH_SIDE) - (int32_t)OWN_LAKE_SEARCH_SIDE / 2;
            const int32_t chunk_y = (int32_t)(i / OWN_LAKE_SEARCH_SIDE) - (int32_t)OWN_LAKE_SEARCH_SIDE / 2;
            if (terrain_feature_chunk_lake(terrain.borrowed_feature, chunk_x, chunk_y).exists == 0u) {
                continue;
            }
            const terrain_density_box_t box = own_box(chunk_x, chunk_y);
            own_by_hand(&terrain, &box, opened, by_hand);
            terrain_generate_box_fallback(&terrain, &box, opened, generated);
            CHECK(memcmp(by_hand, generated, (uint64_t)OWN_VOXELS * sizeof(uint16_t)) == 0);
            found++;
        }
        lakes += found;
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " chunks of ground and %" PRIu64 " with a lake; %" PRIu64 " voxels cut, %" PRIu64
           " wanted and left\n",
           chunks, lakes, cut, wanted_only);
    CHECK(lakes > 0u);
    /* Both must turn up, or the check above says nothing. */
    CHECK(cut > 0u);
    CHECK(wanted_only > 0u);
}

TEST(a_box_that_is_not_one_whole_chunk_with_all_its_ground_is_left_to_find_its_own) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, TERRAIN_SEED_DEFAULT));
    uint16_t *const blocks = test_alloc(t, (uint64_t)OWN_VOXELS * sizeof(uint16_t));
    memset(blocks, 0, (uint64_t)OWN_VOXELS * sizeof(uint16_t));
    terrain_feature_chunk_t read;
    terrain_density_box_t box = own_box(3, -4);
    CHECK(terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    CHECK_EQ_U64(read.tops_known, 1u);
    /* Off the chunk's lines, and not as wide as it. */
    box = own_box(3, -4);
    box.origin_x += 1;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    box = own_box(3, -4);
    box.origin_y -= 8;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    box = own_box(3, -4);
    box.count_x = 8u;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    box = own_box(3, -4);
    box.count_y = 32u;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    /* Layers that stop short of the chunk's ground, from below and from above. */
    box = own_box(3, -4);
    box.count_z = 1u;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    box = own_box(3, -4);
    box.origin_z = MC_WORLD_Z_MAX;
    box.count_z = 1u;
    CHECK(!terrain_feature_chunk_read_box(terrain.borrowed_feature, &box, blocks, &read));
    terrain_deinit(&terrain);
}

TEST_MAIN()
