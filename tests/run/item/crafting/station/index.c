/* ctest run_item_crafting_station. See README.md. */
#include "harness/index.h"

#include "item/crafting/index.h"
#include "item/smelting/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/recipes/index.h"

#include <stdint.h>
#include <string.h>

/* The plan's table, written here by hand and not read from the files, so that the files are what is being tested. */
static const char *const IS_BENCH[] = {
    "acacia_stairs", "birch_stairs",  "cherry_stairs", "dark_oak_stairs", "jungle_stairs", "mangrove_stairs",
    "oak_stairs",    "spruce_stairs", "oak_slab",      "wood_door",       "oak_sign",      "ladder",
    "chest",         "boat",          "bowl",          "wood_axe",        "wood_hoe",      "wood_pickaxe",
    "wood_shovel",   "wood_sword",
};
enum : uint32_t { IS_BENCH_COUNT = sizeof(IS_BENCH) / sizeof(IS_BENCH[0]) };

/* What it takes to get to a bench: the only things made of wood alone that are not made on one. */
static const char *const IS_BOOTSTRAP[] = {
    "acacia_planks", "birch_planks",  "cherry_planks", "dark_oak_planks", "jungle_planks",     "mangrove_planks",
    "oak_planks",    "spruce_planks", "stick",         "crafting_table",  "woodworking_bench",
};
enum : uint32_t { IS_BOOTSTRAP_COUNT = sizeof(IS_BOOTSTRAP) / sizeof(IS_BOOTSTRAP[0]) };

/* Anything with another material, a sample of each kind the plan names. */
static const char *const IS_TABLE[] = {
    "stone_axe",    "stone_hoe", "stone_pickaxe", "stone_shovel", "stone_sword", "iron_axe",
    "iron_pickaxe", "torch",     "bow",           "arrow",        "bed",         "cobblestone_stairs",
};
enum : uint32_t { IS_TABLE_COUNT = sizeof(IS_TABLE) / sizeof(IS_TABLE[0]) };

static uint32_t is_row_named(test_context_t *t, const char *name) {
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        if (strcmp(recipe_defs[row].name, name) == 0) {
            return row;
        }
    }
    CHECK_STR_EQ(name, "a recipe of that name");
    return CRAFTING_NO_RECIPE;
}

/* The first thing a cell of the tables can be. */
static uint16_t is_first_of(uint16_t cell) {
    return recipe_choice_items[recipe_choices[cell - 1u].first];
}

/* A recipe laid out on a grid of three, in its top left corner. */
static void is_lay(const recipe_def_t *recipe, item_stack_t cells[RECIPE_CELLS]) {
    memset(cells, 0, sizeof(item_stack_t) * RECIPE_CELLS);
    if (recipe->kind == RECIPE_KIND_SHAPELESS) {
        for (uint32_t i = 0u; i < recipe->cell_count; i++) {
            cells[i] = item_stack_make(is_first_of(recipe->cells[i]), 1u);
        }
        return;
    }
    for (uint32_t y = 0u; y < recipe->height; y++) {
        for (uint32_t x = 0u; x < recipe->width; x++) {
            const uint16_t cell = recipe->cells[y * recipe->width + x];
            if (cell != RECIPE_CELL_EMPTY) {
                cells[y * CRAFTING_SIDE_TABLE + x] = item_stack_make(is_first_of(cell), 1u);
            }
        }
    }
}

TEST(each_recipe_of_wood_makes_nothing_on_a_table_and_works_on_a_bench) {
    for (uint32_t i = 0u; i < IS_BENCH_COUNT; i++) {
        const uint32_t row = is_row_named(t, IS_BENCH[i]);
        REQUIRE(row != CRAFTING_NO_RECIPE);
        CHECK_EQ_U64(recipe_defs[row].station, RECIPE_STATION_WOODWORKING);
        item_stack_t cells[RECIPE_CELLS];
        is_lay(&recipe_defs[row], cells);
        CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), row);
        CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    }
}

TEST(each_recipe_of_the_table_makes_nothing_on_a_bench_and_works_on_a_table) {
    static const char *const *const lists[2] = {IS_TABLE, IS_BOOTSTRAP};
    const uint32_t counts[2] = {IS_TABLE_COUNT, IS_BOOTSTRAP_COUNT};
    for (uint32_t list = 0u; list < 2u; list++) {
        for (uint32_t i = 0u; i < counts[list]; i++) {
            const uint32_t row = is_row_named(t, lists[list][i]);
            REQUIRE(row != CRAFTING_NO_RECIPE);
            CHECK_EQ_U64(recipe_defs[row].station, RECIPE_STATION_TABLE);
            item_stack_t cells[RECIPE_CELLS];
            is_lay(&recipe_defs[row], cells);
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), row);
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), CRAFTING_NO_RECIPE);
        }
    }
}

TEST(no_recipe_of_either_station_is_made_on_the_other_whatever_it_is) {
    uint32_t bench = 0u, table = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        const uint32_t other =
            recipe->station == RECIPE_STATION_TABLE ? RECIPE_STATION_WOODWORKING : RECIPE_STATION_TABLE;
        item_stack_t cells[RECIPE_CELLS];
        is_lay(recipe, cells);
        CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, recipe->station), row);
        CHECK(crafting_match(cells, CRAFTING_SIDE_TABLE, other) != row);
        bench += recipe->station == RECIPE_STATION_WOODWORKING ? 1u : 0u;
        table += recipe->station == RECIPE_STATION_TABLE ? 1u : 0u;
    }
    CHECK_EQ_U64(bench, IS_BENCH_COUNT);
    CHECK(table > IS_TABLE_COUNT + IS_BOOTSTRAP_COUNT);
}

/* The planks, the logs and the sticks. */
static bool is_item_is_wood(uint16_t item) {
    const char *const name = item_defs[item].name;
    const uint64_t length = strlen(name);
    return strcmp(name, "stick") == 0 || (length > 7u && strcmp(name + length - 7u, "_planks") == 0) ||
           (length > 4u && strcmp(name + length - 4u, "_log") == 0);
}

/* Whether every item that can go in every cell of a recipe is wood. */
static bool is_recipe_is_only_wood(const recipe_def_t *recipe) {
    for (uint32_t i = 0u; i < RECIPE_CELLS; i++) {
        const uint16_t cell = recipe->cells[i];
        if (cell == RECIPE_CELL_EMPTY) {
            continue;
        }
        const recipe_choice_t *const choice = &recipe_choices[cell - 1u];
        for (uint32_t c = 0u; c < choice->count; c++) {
            if (!is_item_is_wood(recipe_choice_items[choice->first + c])) {
                return false;
            }
        }
    }
    return true;
}

/* Ahmed's words: "all wood working is done on this bench". So a new recipe of wood alone fails here until it says where it is made. */
TEST(everything_made_of_wood_alone_is_made_on_a_bench_but_what_it_takes_to_get_one) {
    uint32_t bootstrap = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        if (!is_recipe_is_only_wood(recipe)) {
            CHECK_EQ_U64(recipe->station, RECIPE_STATION_TABLE);
            continue;
        }
        bool is_bootstrap = false;
        for (uint32_t i = 0u; i < IS_BOOTSTRAP_COUNT; i++) {
            is_bootstrap = is_bootstrap || strcmp(recipe->name, IS_BOOTSTRAP[i]) == 0;
        }
        if (!is_bootstrap && recipe->station != RECIPE_STATION_WOODWORKING) {
            CHECK_STR_EQ(recipe->name, "made on a bench");
        }
        bootstrap += is_bootstrap ? 1u : 0u;
    }
    CHECK_EQ_U64(bootstrap, IS_BOOTSTRAP_COUNT);
}

TEST(a_bench_is_three_planks_over_two_sticks_of_any_wood_on_a_table_and_only_there) {
    item_stack_t cells[RECIPE_CELLS];
    memset(cells, 0, sizeof(cells));
    cells[0] = item_stack_make(ITEM_ID_OAK_PLANKS, 1u);
    cells[1] = item_stack_make(ITEM_ID_MANGROVE_PLANKS, 1u);
    cells[2] = item_stack_make(ITEM_ID_SPRUCE_PLANKS, 1u);
    cells[3] = cells[5] = item_stack_make(ITEM_ID_STICK, 1u);
    const uint32_t row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_WOODWORKING_BENCH);
    CHECK_EQ_U64(recipe_defs[row].count, 1u);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), CRAFTING_NO_RECIPE);
    /* Lower down the grid it is the same, and it does not fit in a grid of two: it is three wide. */
    memmove(&cells[3], &cells[0], 6u * sizeof(cells[0]));
    memset(&cells[0], 0, 3u * sizeof(cells[0]));
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), row);
    CHECK_EQ_U64(recipe_defs[row].width, 3u);
    CHECK_EQ_U64(recipe_defs[row].height, 2u);
    /* A stick short, or one in the middle, is nothing. */
    cells[6] = (item_stack_t){0};
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    cells[7] = item_stack_make(ITEM_ID_STICK, 1u);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
}

TEST(from_logs_to_a_bench_and_on_it_a_pickaxe_as_a_player_would) {
    crafting_grid_t hands, table, bench;
    crafting_grid_start(&hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE);
    crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    crafting_grid_start(&bench, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    CHECK_EQ_U64(bench.side, CRAFTING_SIDE_TABLE);
    CHECK_EQ_U64(bench.station, RECIPE_STATION_WOODWORKING);

    /* Hands: a log to planks, four planks to a table, two to sticks. */
    item_stack_t cursor = item_stack_make(ITEM_ID_OAK_LOG, 1u);
    crafting_grid_click_primary(&hands, 0u, &cursor);
    CHECK_EQ_U64(hands.output.item, ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 4u);
    for (uint32_t i = 0u; i < 4u; i++) {
        crafting_grid_click_secondary(&hands, i, &cursor);
    }
    CHECK_EQ_U64(hands.output.item, ITEM_ID_CRAFTING_TABLE);
    item_stack_t made = {0};
    CHECK_EQ_U64(crafting_grid_take(&hands, &made), 1u);
    cursor = item_stack_make(ITEM_ID_OAK_PLANKS, 2u);
    crafting_grid_click_secondary(&hands, 0u, &cursor);
    crafting_grid_click_secondary(&hands, 2u, &cursor);
    CHECK_EQ_U64(hands.output.item, ITEM_ID_STICK);

    /* Hands have no bench to make, and a table does. */
    cursor = item_stack_make(ITEM_ID_OAK_PLANKS, 3u);
    for (uint32_t i = 0u; i < 3u; i++) {
        crafting_grid_click_secondary(&table, i, &cursor);
    }
    /* Three of oak in a row were slabs on a table, and now are nothing there until the sticks come. */
    CHECK(item_stack_is_empty(&table.output));
    cursor = item_stack_make(ITEM_ID_STICK, 2u);
    crafting_grid_click_secondary(&table, 3u, &cursor);
    crafting_grid_click_secondary(&table, 5u, &cursor);
    CHECK_EQ_U64(table.output.item, ITEM_ID_WOODWORKING_BENCH);
    made = (item_stack_t){0};
    CHECK_EQ_U64(crafting_grid_take(&table, &made), 1u);
    CHECK_EQ_U64(made.item, ITEM_ID_WOODWORKING_BENCH);

    /* The pickaxe's cells make it only on the bench. */
    const uint8_t plank_cells[3] = {0u, 1u, 2u}, stick_cells[2] = {4u, 7u};
    crafting_grid_t *const grids[2] = {&table, &bench};
    for (uint32_t g = 0u; g < 2u; g++) {
        cursor = item_stack_make(ITEM_ID_OAK_PLANKS, 3u);
        for (uint32_t i = 0u; i < 3u; i++) {
            crafting_grid_click_secondary(grids[g], plank_cells[i], &cursor);
        }
        cursor = item_stack_make(ITEM_ID_STICK, 2u);
        for (uint32_t i = 0u; i < 2u; i++) {
            crafting_grid_click_secondary(grids[g], stick_cells[i], &cursor);
        }
    }
    CHECK(item_stack_is_empty(&table.output));
    CHECK_EQ_U64(bench.output.item, ITEM_ID_WOOD_PICKAXE);
    made = (item_stack_t){0};
    CHECK_EQ_U64(crafting_grid_take(&bench, &made), 1u);
    CHECK_EQ_U64(made.item, ITEM_ID_WOOD_PICKAXE);
    CHECK_EQ_U64(bench.crafted_count, 1u);
}

TEST(the_bench_item_is_the_west_half_each_half_drops_one_bench_and_it_burns_as_a_table_burns) {
    CHECK_EQ_U64(item_defs[ITEM_ID_WOODWORKING_BENCH].block, BLOCK_ID_WOODWORKING_BENCH_WEST);
    static const uint16_t halves[2] = {BLOCK_ID_WOODWORKING_BENCH_WEST, BLOCK_ID_WOODWORKING_BENCH_EAST};
    for (uint32_t i = 0u; i < 2u; i++) {
        const block_def_t *const def = &block_defs[halves[i]];
        CHECK_EQ_U64(def->drop_count, 1u);
        CHECK_EQ_U64(block_drop_items[block_drops[def->first_drop].item].item, ITEM_ID_WOODWORKING_BENCH);
        CHECK_EQ_U64(block_drops[def->first_drop].count_min, 1u);
        CHECK_EQ_U64(block_drops[def->first_drop].count_max, 1u);
        CHECK_EQ_U64(def->tool, block_defs[BLOCK_ID_CRAFTING_TABLE].tool);
        CHECK_EQ_U64(def->shape, BLOCK_SHAPE_CUBE);
    }
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_WOODWORKING_BENCH), smelting_burn_ticks(ITEM_ID_CRAFTING_TABLE));
}

TEST_MAIN()
