/* ctest run_item_crafting_stairs. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "display/headless/png/index.h"
#include "entity/explosion/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "item/index.h"
#include "job/index.h"
#include "physics/collision/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/recipes/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    CS_WOODS = 8u,
    CS_PLANKS_IN_PATTERN = 6u,
    CS_STAIRS_MADE = 4u,
    CS_FACES = 6u,
    CS_NAME_BYTES = 64u,
    /* Every wood's, and cobblestone's. */
    CS_ITEMS_OF_STAIRS = 9u,
    CS_WOOD_BURNS_TICKS = 300u,
    CS_WIDTH = 640u,
    CS_HEIGHT = 360u,
    /* Enough for the light and the history of the last picture to have gone. */
    CS_DRAWS_TO_SETTLE = 8u,
    /* Half the side of the patch in the middle of the picture, in pixels: well inside the top step from where the eye is. */
    CS_PATCH_HALF = 12u,
    CS_CHANNELS = 3u,
};

/* The woods, written here by hand and not read from the tables, so that the tables are what is being tested. */
typedef struct cs_wood_t {
    const char *name;  /* 0 */
    uint16_t planks;   /* 8: an item */
    uint16_t stairs;   /* 10: an item */
    uint16_t block;    /* 12: the block of those stairs */
    uint16_t _pad0[1]; /* 14 */
} cs_wood_t;
static_assert(sizeof(cs_wood_t) == 16);

static const cs_wood_t CS_WOOD_ROWS[CS_WOODS] = {
    {"acacia", ITEM_ID_ACACIA_PLANKS, ITEM_ID_ACACIA_STAIRS, BLOCK_ID_ACACIA_STAIRS, {0}},
    {"birch", ITEM_ID_BIRCH_PLANKS, ITEM_ID_BIRCH_STAIRS, BLOCK_ID_BIRCH_STAIRS, {0}},
    {"cherry", ITEM_ID_CHERRY_PLANKS, ITEM_ID_CHERRY_STAIRS, BLOCK_ID_CHERRY_STAIRS, {0}},
    {"dark_oak", ITEM_ID_DARK_OAK_PLANKS, ITEM_ID_DARK_OAK_STAIRS, BLOCK_ID_DARK_OAK_STAIRS, {0}},
    {"jungle", ITEM_ID_JUNGLE_PLANKS, ITEM_ID_JUNGLE_STAIRS, BLOCK_ID_JUNGLE_STAIRS, {0}},
    {"mangrove", ITEM_ID_MANGROVE_PLANKS, ITEM_ID_MANGROVE_STAIRS, BLOCK_ID_MANGROVE_STAIRS, {0}},
    {"oak", ITEM_ID_OAK_PLANKS, ITEM_ID_OAK_STAIRS, BLOCK_ID_OAK_STAIRS, {0}},
    {"spruce", ITEM_ID_SPRUCE_PLANKS, ITEM_ID_SPRUCE_STAIRS, BLOCK_ID_SPRUCE_STAIRS, {0}},
};

/* The cells of the pattern on a table, and the same mirrored. */
static const uint8_t CS_PATTERN[CS_PLANKS_IN_PATTERN] = {0u, 3u, 4u, 6u, 7u, 8u};
static const uint8_t CS_PATTERN_MIRRORED[CS_PLANKS_IN_PATTERN] = {2u, 4u, 5u, 6u, 7u, 8u};

/* What these cells make, and no item if they make nothing: a row is only there to read if there is a row. */
static uint32_t cs_makes(const item_stack_t *cells, uint32_t side) {
    const uint32_t recipe = crafting_match(cells, side);
    return recipe == CRAFTING_NO_RECIPE ? ITEM_ID_NONE : recipe_defs[recipe].gives;
}

static void cs_fill(item_stack_t *cells, const uint8_t *pattern, uint16_t item) {
    memset(cells, 0, sizeof(item_stack_t) * RECIPE_CELLS);
    for (uint32_t i = 0u; i < CS_PLANKS_IN_PATTERN; i++) {
        cells[pattern[i]] = item_stack_make(item, 1u);
    }
}

/* Oak was the only one for a long time, and this test was run on it by itself before there were others: see plan 003 phase 3. */
TEST(each_wood_makes_four_of_its_own_stairs_on_a_table_and_uses_every_plank) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        const cs_wood_t *const wood = &CS_WOOD_ROWS[i];
        const uint8_t *const patterns[2] = {CS_PATTERN, CS_PATTERN_MIRRORED};
        for (uint32_t which = 0u; which < 2u; which++) {
            crafting_grid_t table;
            crafting_grid_start(&table, CRAFTING_SIDE_TABLE);
            item_stack_t cursor = item_stack_make(wood->planks, 64u);
            for (uint32_t cell = 0u; cell < CS_PLANKS_IN_PATTERN; cell++) {
                crafting_grid_click_secondary(&table, patterns[which][cell], &cursor);
            }
            CHECK_EQ_U64(cursor.count, 64u - CS_PLANKS_IN_PATTERN);
            REQUIRE(table.recipe != CRAFTING_NO_RECIPE);
            CHECK_EQ_U64(table.output.item, wood->stairs);
            CHECK_EQ_U64(table.output.count, CS_STAIRS_MADE);

            item_stack_t picked = {0};
            CHECK_EQ_U64(crafting_grid_take(&table, &picked), CS_STAIRS_MADE);
            CHECK_EQ_U64(picked.item, wood->stairs);
            CHECK(item_stack_is_valid(&picked));
            CHECK(item_stack_is_empty(&table.output));
            for (uint32_t cell = 0u; cell < RECIPE_CELLS; cell++) {
                CHECK(item_stack_is_empty(&table.cells[cell]));
            }
        }
    }
}

TEST(a_grid_of_two_makes_no_stairs) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        const cs_wood_t *const wood = &CS_WOOD_ROWS[i];
        for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
            if (recipe_defs[row].gives == wood->stairs) {
                CHECK_EQ_U64(recipe_defs[row].width, CRAFTING_SIDE_TABLE);
                CHECK_EQ_U64(recipe_defs[row].height, CRAFTING_SIDE_TABLE);
            }
        }
        /* Three of them in a grid of two is the pattern's own bottom left corner. */
        item_stack_t cells[RECIPE_CELLS];
        memset(cells, 0, sizeof(cells));
        cells[0] = cells[2] = cells[3] = item_stack_make(wood->planks, 1u);
        CHECK(cs_makes(cells, CRAFTING_SIDE_HANDS) != wood->stairs);
    }
}

TEST(one_plank_of_another_wood_anywhere_makes_nothing) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        for (uint32_t other = 0u; other < CS_WOODS; other++) {
            if (other == i) {
                continue;
            }
            for (uint32_t spoiled = 0u; spoiled < CS_PLANKS_IN_PATTERN; spoiled++) {
                item_stack_t cells[RECIPE_CELLS];
                cs_fill(cells, CS_PATTERN, CS_WOOD_ROWS[i].planks);
                cells[CS_PATTERN[spoiled]] = item_stack_make(CS_WOOD_ROWS[other].planks, 1u);
                CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE), CRAFTING_NO_RECIPE);
            }
        }
    }
}

TEST(a_pattern_of_all_different_woods_makes_nothing) {
    item_stack_t cells[RECIPE_CELLS];
    memset(cells, 0, sizeof(cells));
    for (uint32_t i = 0u; i < CS_PLANKS_IN_PATTERN; i++) {
        cells[CS_PATTERN[i]] = item_stack_make(CS_WOOD_ROWS[i].planks, 1u);
    }
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE), CRAFTING_NO_RECIPE);
}

TEST(one_plank_short_is_not_stairs) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        for (uint32_t gone = 0u; gone < CS_PLANKS_IN_PATTERN; gone++) {
            item_stack_t cells[RECIPE_CELLS];
            cs_fill(cells, CS_PATTERN, CS_WOOD_ROWS[i].planks);
            cells[CS_PATTERN[gone]] = (item_stack_t){0};
            CHECK(cs_makes(cells, CRAFTING_SIDE_TABLE) != CS_WOOD_ROWS[i].stairs);
        }
    }
}

TEST(each_block_is_stairs_of_its_wood_in_its_wood_colour_and_gives_itself) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        const cs_wood_t *const wood = &CS_WOOD_ROWS[i];
        char stairs_name[CS_NAME_BYTES];
        char texture_name[CS_NAME_BYTES];
        (void)snprintf(stairs_name, sizeof(stairs_name), "%s_stairs", wood->name);
        (void)snprintf(texture_name, sizeof(texture_name), "blocks/%s_planks", wood->name);

        const block_def_t *const def = &block_defs[wood->block];
        const block_def_t *const oak = &block_defs[BLOCK_ID_OAK_STAIRS];
        CHECK_STR_EQ(def->name, stairs_name);
        CHECK_EQ_U64(def->shape, BLOCK_SHAPE_STAIRS);
        CHECK_EQ_U64(def->tool, BLOCK_TOOL_AXE);
        CHECK_EQ_U64(def->tier, oak->tier);
        CHECK_EQ_U64(def->flags, oak->flags);
        CHECK_EQ_U64(def->opacity, oak->opacity);
        CHECK_NEAR(def->hardness, oak->hardness, 0.0f64);
        CHECK(def->flags & BLOCK_FLAG_SOLID);
        for (uint32_t face = 0u; face < CS_FACES; face++) {
            CHECK_STR_EQ(block_texture_names[def->faces[face]], texture_name);
        }

        REQUIRE(def->drop_count == 1u);
        const block_drop_t *const drop = &block_drops[def->first_drop];
        CHECK_EQ_U64(block_drop_items[drop->item].item, wood->stairs);
        CHECK_EQ_U64(drop->count_min, 1u);
        CHECK_EQ_U64(drop->count_max, 1u);

        const item_def_t *const item = &item_defs[wood->stairs];
        CHECK_STR_EQ(item->name, stairs_name);
        CHECK_STR_EQ(item->texture, texture_name);
        CHECK_EQ_U64(item->block, wood->block);
    }
}

TEST(each_burns_stands_up_to_a_blast_and_is_walked_on_as_oak_stairs_are) {
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        const cs_wood_t *const wood = &CS_WOOD_ROWS[i];
        CHECK_EQ_U64(recipe_fuels[wood->stairs].ticks, CS_WOOD_BURNS_TICKS);
        CHECK_EQ_U64(recipe_fuels[wood->stairs].leaves, ITEM_ID_NONE);
        CHECK_NEAR(explosion_resistance(wood->block), explosion_resistance(BLOCK_ID_OAK_PLANKS), 0.0f64);

        collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
        collision_box_t oak[COLLISION_BOXES_PER_BLOCK];
        CHECK_EQ_U64(collision_block_boxes(wood->block, 0, 0, 0, boxes), COLLISION_BOXES_PER_BLOCK);
        CHECK_EQ_U64(collision_block_boxes(BLOCK_ID_OAK_STAIRS, 0, 0, 0, oak), COLLISION_BOXES_PER_BLOCK);
        CHECK(memcmp(boxes, oak, sizeof(boxes)) == 0);
    }
}

/* A new wood with planks and no stairs fails here, and this file's list is then the place to add it. */
TEST(no_wood_is_left_out) {
    uint32_t planks = 0u, stairs = 0u;
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        const char *const name = item_defs[item].name;
        const uint64_t length = strlen(name);
        if (length > 7u && strcmp(name + length - 7u, "_planks") == 0) {
            planks++;
        }
        if (length > 7u && strcmp(name + length - 7u, "_stairs") == 0) {
            stairs++;
        }
    }
    CHECK_EQ_U64(planks, CS_WOODS);
    CHECK_EQ_U64(stairs, CS_ITEMS_OF_STAIRS);
}

TEST(cobblestone_stairs_are_still_cobblestone) {
    item_stack_t cells[RECIPE_CELLS];
    cs_fill(cells, CS_PATTERN, ITEM_ID_COBBLESTONE);
    CHECK_EQ_U64(cs_makes(cells, CRAFTING_SIDE_TABLE), ITEM_ID_COBBLESTONE_STAIRS);
}

/* ---- A real picture ------------------------------------------------------------------------------------------------- */

typedef struct cs_game_t {
    gpu_bridge_t *bridge;           /* 0 */
    job_system_t *job;              /* 8 */
    world_t *world;                 /* 16 */
    terrain_t *terrain;             /* 24 */
    render_t *render;               /* 32 */
    texture_t *texture;             /* 40 */
    game_t *game;                   /* 48 */
    thread_topology_t *topology;    /* 56 */
    display_script_state_t *state;  /* 64 */
    player_interaction_t *hands;    /* 72 */
    player_movement_t *movement;    /* 80 */
    display_headless_scene_t scene; /* 88 */
    display_buffer_t target;        /* 112 */
    uint32_t tick;                  /* 168 */
    uint32_t _pad0;                 /* 172 */
} cs_game_t;
static_assert(sizeof(cs_game_t) == 176);

static bool cs_game_begin(test_context_t *t, cs_game_t *g) {
    g->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    g->job = test_world_aligned(t, sizeof(job_system_t));
    g->world = test_world_aligned(t, sizeof(world_t));
    g->terrain = test_world_aligned(t, sizeof(terrain_t));
    g->render = test_world_aligned(t, sizeof(render_t));
    g->texture = test_world_aligned(t, sizeof(texture_t));
    g->game = test_world_aligned(t, sizeof(game_t));
    g->topology = test_alloc(t, sizeof(thread_topology_t));
    g->state = test_alloc(t, sizeof(display_script_state_t));
    g->hands = test_world_aligned(t, sizeof(player_interaction_t));
    g->movement = test_world_aligned(t, sizeof(player_movement_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(g->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(g->topology)) || !CHECK(job_init(g->job, g->topology)) ||
        !CHECK(texture_init(g->texture)) || !CHECK(world_init(g->world, world_limits_default())) ||
        !CHECK(terrain_init(g->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(g->render, &g->bridge->device_memory, g->job, render_limits_default()))) {
        return false;
    }
    game_init(g->game, g->world, g->terrain, g->render, g->job, g->texture, g->topology);
    static const game_config_t config = {.render_distance = 4u, .width = CS_WIDTH, .height = CS_HEIGHT, ._pad0 = {0}};
    if (!CHECK(game_open(g->game, &config)) || !CHECK(game_settle(g->game))) {
        return false;
    }
    game_headless_scene(g->game, &g->scene);
    const uint64_t bytes = post_xrgb_bytes(CS_WIDTH, CS_HEIGHT);
    g->target = (display_buffer_t){.owned_map = test_world_aligned(t, bytes),
                                   .map_bytes = bytes,
                                   .pitch = CS_WIDTH * 4u,
                                   .width = CS_WIDTH,
                                   .height = CS_HEIGHT};
    memset(g->state, 0, sizeof(*g->state));
    return true;
}

static void cs_game_end(test_context_t *t, cs_game_t *g) {
    game_close(g->game);
    game_deinit(g->game);
    render_deinit(g->render);
    CHECK(gpu_bridge_exit_check(g->bridge));
    terrain_deinit(g->terrain);
    world_deinit(g->world);
    texture_deinit(g->texture);
    job_deinit(g->job);
    gpu_bridge_deinit(g->bridge);
}

static void cs_game_settle(test_context_t *t, cs_game_t *g) {
    for (uint32_t i = 0u; i < CS_DRAWS_TO_SETTLE; i++) {
        display_script_state_begin_tick(g->state, g->tick);
        const display_headless_tick_t info = {.events = nullptr, .state = g->state, .event_count = 0u, .tick = g->tick};
        CHECK(g->scene.tick(g->scene.borrowed_context, &info));
        CHECK(g->scene.draw(g->scene.borrowed_context, &info, &g->target));
        g->tick++;
    }
}

/* Puts what an item places on top of the stone below this cell, by the hands, by a look straight down from a real eye. */
static void cs_place(test_context_t *t, cs_game_t *g, uint16_t item, int32_t x, int32_t y, int32_t z) {
    const uint16_t block = item_defs[item].block;
    REQUIRE(block != ITEM_NO_BLOCK);
    player_interaction_hand_give(g->hands, block, 1u);
    const float64_t eye[3] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z + 2.0f64};
    player_target_t target;
    player_camera_target(&g->world->map, eye, 0.0f32, -1.5707963f32, &target);
    REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
    const player_interaction_input_t input = {
        .borrowed_target = &target, .borrowed_movement = g->movement, .attack = 0u, .place = 1u, ._pad0 = {0}};
    g->hands->place_delay = 0u;
    player_interaction_tick(g->hands, &g->world->edit, &input);
    uint16_t placed = BLOCK_ID_AIR;
    CHECK(world_map_block_get(&g->world->map, x, y, z, &placed) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(placed, block);
}

/* The mean colour in the middle of the picture, as red, green, blue. */
static void cs_patch_mean(const display_buffer_t *target, float64_t *out) {
    uint64_t sums[CS_CHANNELS] = {0u, 0u, 0u};
    const uint32_t cx = target->width / 2u, cy = target->height / 2u;
    for (uint32_t y = cy - CS_PATCH_HALF; y < cy + CS_PATCH_HALF; y++) {
        const uint8_t *const row = (const uint8_t *)target->owned_map + (uint64_t)y * target->pitch;
        for (uint32_t x = cx - CS_PATCH_HALF; x < cx + CS_PATCH_HALF; x++) {
            /* XRGB in memory: blue, green, red, nothing. */
            sums[0] += row[x * 4u + 2u];
            sums[1] += row[x * 4u + 1u];
            sums[2] += row[x * 4u];
        }
    }
    const float64_t pixels = (float64_t)(4u * CS_PATCH_HALF * CS_PATCH_HALF);
    for (uint32_t c = 0u; c < CS_CHANNELS; c++) {
        out[c] = (float64_t)sums[c] / pixels;
    }
}

static float64_t cs_colour_distance(const float64_t *a, const float64_t *b) {
    float64_t sum = 0.0f64;
    for (uint32_t c = 0u; c < CS_CHANNELS; c++) {
        sum += (a[c] - b[c]) * (a[c] - b[c]);
    }
    return sqrt(sum);
}

/* Only for someone wanting to look: MC_STAIRS_SHOTS names a directory that exists. */
static void cs_shot_write(test_context_t *t, const display_buffer_t *target, const char *name) {
    const char *const directory = getenv("MC_STAIRS_SHOTS");
    if (directory == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(target->width, target->height);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    REQUIRE(display_png_encode(target->owned_map, target->pitch, target->width, target->height, png, capacity, &bytes));
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/%s", directory, name);
    CHECK(file_write_atomic(path, png, bytes) == FILE_OK);
}

/* Each wood's stairs, put down by the hands and seen from above over the top step, against the same wood's planks in the same cell and
 * the same light: they must be nearest to their own of all eight. So "that wood's colour" is what the card draws, and not only a name
 * in a table. */
TEST(the_card_draws_each_wood_in_its_own_colour) {
    cs_game_t *const g = test_alloc(t, sizeof(cs_game_t));
    memset(g, 0, sizeof(*g));
    REQUIRE(cs_game_begin(t, g));
    game_t *const game = g->game;
    game_day_set(game, 0.5f64);
    const int32_t x = (int32_t)floor(game->camera.position[0]);
    const int32_t y = (int32_t)floor(game->camera.position[1]);
    const int32_t z = (int32_t)floor(game->camera.position[2]) + 8;
    world_edit_t *const edit = &g->world->edit;
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    /* A row for all eight, in the air, with stone under each. */
    const int32_t row_y = y + 3;
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        REQUIRE(world_edit_block_set(edit, x + 2 * (int32_t)i, row_y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    }
    player_interaction_start(g->hands, 77u);
    /* The body is well away from every cell here. */
    player_movement_start(g->movement, (float64_t)x + 40.5f64, (float64_t)y + 40.5f64, (float64_t)z);
    g->movement->on_ground = 1u;

    /* Over the top step, which is the north half of the cell, and straight down. */
    game->camera.position[0] = (float64_t)x + 0.5f64;
    game->camera.position[1] = (float64_t)y + 0.75f64;
    game->camera.position[2] = (float64_t)z + 3.0f64;
    game->camera.yaw = 0.0f32;
    game->camera.pitch = -1.57f32;

    float64_t planks[CS_WOODS][CS_CHANNELS];
    float64_t stairs[CS_WOODS][CS_CHANNELS];
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        const cs_wood_t *const wood = &CS_WOOD_ROWS[i];
        cs_place(t, g, wood->planks, x, y, z + 1);
        cs_game_settle(t, g);
        cs_patch_mean(&g->target, planks[i]);
        REQUIRE(world_edit_block_set(edit, x, y, z + 1, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
        cs_place(t, g, wood->stairs, x, y, z + 1);
        cs_game_settle(t, g);
        cs_patch_mean(&g->target, stairs[i]);
        REQUIRE(world_edit_block_set(edit, x, y, z + 1, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
        printf("  %-8s planks %3u %3u %3u, stairs %3u %3u %3u\n", wood->name, (uint32_t)planks[i][0],
               (uint32_t)planks[i][1], (uint32_t)planks[i][2], (uint32_t)stairs[i][0], (uint32_t)stairs[i][1],
               (uint32_t)stairs[i][2]);
    }
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        uint32_t nearest = 0u;
        for (uint32_t other = 1u; other < CS_WOODS; other++) {
            if (cs_colour_distance(stairs[i], planks[other]) < cs_colour_distance(stairs[i], planks[nearest])) {
                nearest = other;
            }
        }
        CHECK_STR_EQ(CS_WOOD_ROWS[nearest].name, CS_WOOD_ROWS[i].name);
    }

    /* All eight together, from the south, for someone to look at. */
    for (uint32_t i = 0u; i < CS_WOODS; i++) {
        cs_place(t, g, CS_WOOD_ROWS[i].stairs, x + 2 * (int32_t)i, row_y, z + 1);
    }
    game->camera.position[0] = (float64_t)x + 7.5f64;
    game->camera.position[1] = (float64_t)row_y - 7.0f64;
    game->camera.position[2] = (float64_t)z + 4.0f64;
    game->camera.yaw = 1.5707963f32;
    game->camera.pitch = -0.35f32;
    cs_game_settle(t, g);
    cs_shot_write(t, &g->target, "all_eight.png");
    cs_game_end(t, g);
}

TEST_MAIN()
