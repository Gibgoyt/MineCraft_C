/* ctest run_item_crafting. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "main/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/recipes/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    IC_FUZZ_ROUNDS = 40000u,
    IC_RANDOM_STEPS = 200000u,
    IC_WIDTH = 160u,
    IC_HEIGHT = 90u,
    IC_PROGRAM_TIMEOUT_MS = 120000u,
};

static uint64_t ic_random(uint64_t *state) {
    /* xorshift64*: only so that a failure comes back the same. */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x9E3779B97F4A7C15ull;
}

static void ic_inventory(test_context_t *t, inventory_t *inventory, arena_t *arena) {
    arena_init(arena, "ic_inventory", test_alloc(t, INVENTORY_BYTES), INVENTORY_BYTES);
    inventory_start(inventory, arena);
}

static uint32_t ic_recipe_named(test_context_t *t, const char *name) {
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        if (strcmp(recipe_defs[row].name, name) == 0) {
            return row;
        }
    }
    CHECK_STR_EQ("a recipe of this name", name);
    return CRAFTING_NO_RECIPE;
}

static item_stack_t ic_one(uint16_t item) {
    return item_stack_make(item, 1u);
}

/* What the file of a recipe's cell can first be. */
static uint16_t ic_first_of(uint16_t cell) {
    return recipe_choice_items[recipe_choices[cell - 1u].first];
}

/* ---- The tables ------------------------------------------------------------------------------------------------------ */

TEST(every_item_has_a_row_to_lie_on_the_ground_by_which_is_its_id_less_one) {
    CHECK_EQ_U64(BLOCK_DROP_ITEM_COUNT, ITEM_COUNT - 1u);
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        const uint16_t row = item_stack_drop_row((uint16_t)item);
        CHECK_EQ_U64(block_drop_items[row].item, item);
        CHECK_STR_EQ(block_drop_items[row].name, item_defs[item].name);
    }
}

TEST(what_each_table_row_says_is_possible) {
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        REQUIRE(recipe->gives != ITEM_ID_NONE && recipe->gives < ITEM_COUNT);
        CHECK(recipe->count >= 1u && recipe->count <= item_defs[recipe->gives].max_stack);
        uint32_t filled = 0u;
        for (uint32_t cell = 0u; cell < RECIPE_CELLS; cell++) {
            if (recipe->cells[cell] != RECIPE_CELL_EMPTY) {
                filled++;
                REQUIRE(recipe->cells[cell] <= RECIPE_CHOICE_COUNT);
                CHECK(recipe_choices[recipe->cells[cell] - 1u].count != 0u);
            }
        }
        CHECK_EQ_U64(filled, recipe->cell_count);
        if (recipe->kind == RECIPE_KIND_SHAPED) {
            CHECK(recipe->width >= 1u && recipe->width <= 3u && recipe->height >= 1u && recipe->height <= 3u);
        }
    }
    for (uint32_t i = 0u; i < RECIPE_CHOICE_ITEM_COUNT; i++) {
        CHECK(recipe_choice_items[i] != ITEM_ID_NONE && recipe_choice_items[i] < ITEM_COUNT);
    }
    for (uint32_t item = 0u; item < ITEM_COUNT; item++) {
        CHECK(recipe_smelt_rows[item] <= RECIPE_SMELT_COUNT);
        CHECK(recipe_fuels[item].leaves < ITEM_COUNT);
    }
    for (uint32_t row = 0u; row < RECIPE_SMELT_COUNT; row++) {
        CHECK(recipe_smelts[row].count <= item_defs[recipe_smelts[row].gives].max_stack);
        CHECK(recipe_smelts[row].ticks != 0u);
    }
}

/* The way from a tree to an iron pickaxe, and the things a player looks for first, must not get lost in a tidy of the files. */
TEST(the_basic_set_is_all_there) {
    CHECK(RECIPE_COUNT >= 100u);
    static const uint16_t wanted[] = {
        ITEM_ID_OAK_PLANKS,
        ITEM_ID_BIRCH_PLANKS,
        ITEM_ID_STICK,
        ITEM_ID_CRAFTING_TABLE,
        ITEM_ID_CHEST,
        ITEM_ID_FURNACE,
        ITEM_ID_TORCH,
        ITEM_ID_WOOD_PICKAXE,
        ITEM_ID_WOOD_AXE,
        ITEM_ID_WOOD_SHOVEL,
        ITEM_ID_WOOD_HOE,
        ITEM_ID_WOOD_SWORD,
        ITEM_ID_STONE_PICKAXE,
        ITEM_ID_IRON_PICKAXE,
        ITEM_ID_GOLD_PICKAXE,
        ITEM_ID_DIAMOND_PICKAXE,
        ITEM_ID_IRON_HELMET,
        ITEM_ID_DIAMOND_BOOTS,
        ITEM_ID_LEATHER_LEGGINGS,
        ITEM_ID_SHEARS,
        ITEM_ID_BUCKET,
        ITEM_ID_BOW,
        ITEM_ID_ARROW,
        ITEM_ID_BREAD,
        ITEM_ID_LADDER,
        ITEM_ID_BOWL,
        ITEM_ID_PAPER,
        ITEM_ID_BOOK,
        ITEM_ID_BRICKS,
        ITEM_ID_STONE_BRICKS,
    };
    for (uint32_t i = 0u; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        bool found = false;
        for (uint32_t row = 0u; row < RECIPE_COUNT && !found; row++) {
            found = recipe_defs[row].gives == wanted[i];
        }
        if (!CHECK(found)) {
            printf("  nothing makes %s\n", item_defs[wanted[i]].name);
        }
    }
    static const uint16_t smelted[] = {ITEM_ID_RAW_IRON,    ITEM_ID_IRON_ORE,  ITEM_ID_RAW_GOLD, ITEM_ID_SAND,
                                       ITEM_ID_COBBLESTONE, ITEM_ID_CLAY_BALL, ITEM_ID_OAK_LOG,  ITEM_ID_POTATO};
    for (uint32_t i = 0u; i < sizeof(smelted) / sizeof(smelted[0]); i++) {
        CHECK(smelting_recipe_of(smelted[i]) != nullptr);
    }
    CHECK(smelting_recipe_of(ITEM_ID_NONE) == nullptr);
    CHECK(smelting_recipe_of(ITEM_ID_DIRT) == nullptr);
    CHECK(smelting_recipe_of(ITEM_COUNT) == nullptr);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_COAL), 1600u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_CHARCOAL), 1600u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_LAVA_BUCKET), 20000u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_OAK_PLANKS), 300u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_DARK_OAK_LOG), 300u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_STICK), 100u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_MANGROVE_PROPAGULE), 100u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_STONE), 0u);
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_COUNT), 0u);
}

/* ---- Matching --------------------------------------------------------------------------------------------------------- */

static void ic_cells_clear(item_stack_t *cells) {
    memset(cells, 0, sizeof(item_stack_t) * RECIPE_CELLS);
}

/* A shaped recipe in a grid, at a place, one way round or the other, with this item in the one cell given. */
static void ic_lay_shaped(const recipe_def_t *recipe, item_stack_t *cells, uint32_t side, uint32_t left, uint32_t top,
                          bool mirrored, uint32_t special_cell, uint16_t special_item) {
    ic_cells_clear(cells);
    for (uint32_t y = 0u; y < recipe->height; y++) {
        for (uint32_t x = 0u; x < recipe->width; x++) {
            const uint32_t at = y * recipe->width + x;
            const uint16_t cell = recipe->cells[at];
            if (cell != RECIPE_CELL_EMPTY) {
                const uint32_t to = mirrored ? (uint32_t)recipe->width - 1u - x : x;
                cells[(top + y) * side + left + to] = ic_one(at == special_cell ? special_item : ic_first_of(cell));
            }
        }
    }
}

TEST(every_recipe_matches_with_everything_each_cell_can_be_everywhere_it_fits_and_mirrored) {
    uint32_t tried = 0u, shaped = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        item_stack_t cells[RECIPE_CELLS];
        if (recipe->kind == RECIPE_KIND_SHAPELESS) {
            /* Each thing each cell can be, in the cells from each corner of the table. */
            for (uint32_t cell = 0u; cell < recipe->cell_count; cell++) {
                const recipe_choice_t *const choice = &recipe_choices[recipe->cells[cell] - 1u];
                for (uint32_t option = 0u; option < choice->count; option++) {
                    for (uint32_t shift = 0u; shift + recipe->cell_count <= RECIPE_CELLS; shift++) {
                        ic_cells_clear(cells);
                        for (uint32_t i = 0u; i < recipe->cell_count; i++) {
                            /* Backward, so that the cells are not in the order the file has them. */
                            const uint32_t to = shift + recipe->cell_count - 1u - i;
                            cells[to] = ic_one(i == cell ? recipe_choice_items[choice->first + option]
                                                         : ic_first_of(recipe->cells[i]));
                        }
                        CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, recipe->station), row);
                        tried++;
                    }
                }
            }
            continue;
        }
        shaped++;
        for (uint32_t at = 0u; at < (uint32_t)recipe->width * recipe->height; at++) {
            if (recipe->cells[at] == RECIPE_CELL_EMPTY) {
                continue;
            }
            const recipe_choice_t *const choice = &recipe_choices[recipe->cells[at] - 1u];
            for (uint32_t option = 0u; option < choice->count; option++) {
                for (uint32_t top = 0u; top + recipe->height <= CRAFTING_SIDE_TABLE; top++) {
                    for (uint32_t left = 0u; left + recipe->width <= CRAFTING_SIDE_TABLE; left++) {
                        for (uint32_t mirror = 0u; mirror < 2u; mirror++) {
                            ic_lay_shaped(recipe, cells, CRAFTING_SIDE_TABLE, left, top, mirror != 0u, at,
                                          recipe_choice_items[choice->first + option]);
                            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, recipe->station), row);
                            tried++;
                        }
                    }
                }
            }
        }
    }
    printf("  %u recipes, %u of them shaped, tried %u ways\n", (uint32_t)RECIPE_COUNT, shaped, tried);
    CHECK(tried > 1000u);
}

TEST(a_grid_of_two_makes_what_fits_in_it_and_nothing_bigger) {
    uint32_t fit = 0u, not_fit = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        if (recipe->kind != RECIPE_KIND_SHAPED) {
            continue;
        }
        if (recipe->width <= CRAFTING_SIDE_HANDS && recipe->height <= CRAFTING_SIDE_HANDS) {
            item_stack_t cells[RECIPE_CELLS];
            ic_lay_shaped(recipe, cells, CRAFTING_SIDE_HANDS, 0u, 0u, false, RECIPE_CELLS, ITEM_ID_NONE);
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, recipe->station), row);
            fit++;
        } else {
            not_fit++;
        }
    }
    CHECK(fit > 0u);
    CHECK(not_fit > 0u);
    /* The cells of a chest's recipe that a grid of two has cannot make a chest. */
    item_stack_t cells[RECIPE_CELLS];
    ic_cells_clear(cells);
    for (uint32_t i = 0u; i < 4u; i++) {
        cells[i] = ic_one(ITEM_ID_OAK_PLANKS);
    }
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE),
                 ic_recipe_named(t, "crafting_table"));
    /* In a table, the same four in a square in any corner are the same thing. */
    ic_cells_clear(cells);
    cells[4] = cells[5] = cells[7] = cells[8] = ic_one(ITEM_ID_SPRUCE_PLANKS);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE),
                 ic_recipe_named(t, "crafting_table"));
}

TEST(what_is_almost_a_recipe_is_nothing) {
    item_stack_t cells[RECIPE_CELLS];
    ic_cells_clear(cells);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);

    /* A stick is two planks one above the other: with a gap between them, or side by side, it is not. */
    cells[0] = cells[3] = ic_one(ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), ic_recipe_named(t, "stick"));
    cells[3] = (item_stack_t){0};
    cells[6] = ic_one(ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    ic_cells_clear(cells);
    cells[0] = cells[1] = ic_one(ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);

    /* A chest with its middle filled is nothing, and with one plank short it is nothing. */
    ic_cells_clear(cells);
    for (uint32_t i = 0u; i < RECIPE_CELLS; i++) {
        cells[i] = ic_one(ITEM_ID_OAK_PLANKS);
    }
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), CRAFTING_NO_RECIPE);
    cells[4] = (item_stack_t){0};
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), ic_recipe_named(t, "chest"));
    cells[0] = (item_stack_t){0};
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), CRAFTING_NO_RECIPE);

    /* A shapeless one wants all of its things, and no more. */
    ic_cells_clear(cells);
    cells[0] = ic_one(ITEM_ID_IRON_INGOT);
    cells[8] = ic_one(ITEM_ID_FLINT);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE),
                 ic_recipe_named(t, "flint_and_steel"));
    cells[4] = ic_one(ITEM_ID_FLINT);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    cells[4] = (item_stack_t){0};
    cells[8] = ic_one(ITEM_ID_IRON_INGOT);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);

    /* A pickaxe mixes woods, but a plank is no stick. */
    ic_cells_clear(cells);
    cells[0] = ic_one(ITEM_ID_OAK_PLANKS);
    cells[1] = ic_one(ITEM_ID_CHERRY_PLANKS);
    cells[2] = ic_one(ITEM_ID_JUNGLE_PLANKS);
    cells[4] = cells[7] = ic_one(ITEM_ID_STICK);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING),
                 ic_recipe_named(t, "wood_pickaxe"));
    cells[7] = ic_one(ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING), CRAFTING_NO_RECIPE);

    /* How many a cell holds, and wear, do not change it. */
    ic_cells_clear(cells);
    cells[0] = item_stack_make(ITEM_ID_OAK_LOG, 64u);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE), ic_recipe_named(t, "oak_planks"));
}

/* Tried a different way from what is under test: each recipe laid in every place on an empty grid and compared whole, and each order of a
 * shapeless one's things tried one by one. */
static bool ic_shapeless_by_orders(const recipe_def_t *recipe, const item_stack_t *cells, uint32_t side) {
    uint16_t items[RECIPE_CELLS];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < side * side; i++) {
        if (cells[i].count != 0u) {
            items[count] = cells[i].item;
            count++;
        }
    }
    if (count != recipe->cell_count) {
        return false;
    }
    uint32_t order[RECIPE_CELLS];
    uint32_t control[RECIPE_CELLS];
    for (uint32_t i = 0u; i < count; i++) {
        order[i] = i;
        control[i] = 0u;
    }
    /* Heap's algorithm, by its loop. */
    uint32_t level = 0u;
    for (;;) {
        bool all = true;
        for (uint32_t i = 0u; i < count && all; i++) {
            const recipe_choice_t *const choice = &recipe_choices[recipe->cells[i] - 1u];
            bool has = false;
            for (uint32_t c = 0u; c < choice->count && !has; c++) {
                has = recipe_choice_items[choice->first + c] == items[order[i]];
            }
            all = has;
        }
        if (all) {
            return true;
        }
        while (level < count && control[level] >= level) {
            control[level] = 0u;
            level++;
        }
        if (level >= count) {
            return false;
        }
        const uint32_t other = level % 2u == 0u ? 0u : control[level];
        const uint32_t held = order[other];
        order[other] = order[level];
        order[level] = held;
        control[level]++;
        level = 0u;
    }
}

static bool ic_shaped_by_placing(const recipe_def_t *recipe, const item_stack_t *cells, uint32_t side) {
    for (uint32_t top = 0u; top + recipe->height <= side; top++) {
        for (uint32_t left = 0u; left + recipe->width <= side; left++) {
            for (uint32_t mirror = 0u; mirror < 2u; mirror++) {
                bool same = true;
                for (uint32_t y = 0u; y < side && same; y++) {
                    for (uint32_t x = 0u; x < side && same; x++) {
                        uint16_t cell = RECIPE_CELL_EMPTY;
                        if (y >= top && y < top + recipe->height && x >= left && x < left + recipe->width) {
                            const uint32_t in = x - left;
                            const uint32_t from = mirror != 0u ? (uint32_t)recipe->width - 1u - in : in;
                            cell = recipe->cells[(y - top) * recipe->width + from];
                        }
                        const item_stack_t *const stack = &cells[y * side + x];
                        if (cell == RECIPE_CELL_EMPTY) {
                            same = stack->count == 0u;
                        } else {
                            const recipe_choice_t *const choice = &recipe_choices[cell - 1u];
                            bool has = false;
                            for (uint32_t c = 0u; c < choice->count && !has; c++) {
                                has = stack->count != 0u && recipe_choice_items[choice->first + c] == stack->item;
                            }
                            same = has;
                        }
                    }
                }
                if (same) {
                    return true;
                }
            }
        }
    }
    return false;
}

TEST(at_random_matching_agrees_with_trying_it_the_long_way) {
    uint64_t state = 0x13579u;
    uint32_t matched = 0u, unmatched = 0u;
    for (uint32_t round = 0u; round < IC_FUZZ_ROUNDS; round++) {
        const recipe_def_t *const recipe = &recipe_defs[ic_random(&state) % RECIPE_COUNT];
        const uint32_t side = ic_random(&state) % 4u == 0u ? CRAFTING_SIDE_HANDS : CRAFTING_SIDE_TABLE;
        const uint32_t station = ic_random(&state) % 8u == 0u ? (uint32_t)RECIPE_STATION_TABLE : recipe->station;
        item_stack_t cells[RECIPE_CELLS];
        ic_cells_clear(cells);
        if (recipe->kind == RECIPE_KIND_SHAPED) {
            if (recipe->width <= side && recipe->height <= side) {
                ic_lay_shaped(recipe, cells, side, (uint32_t)(ic_random(&state) % (side - recipe->width + 1u)),
                              (uint32_t)(ic_random(&state) % (side - recipe->height + 1u)),
                              ic_random(&state) % 2u == 0u, RECIPE_CELLS, ITEM_ID_NONE);
            }
        } else {
            for (uint32_t i = 0u; i < recipe->cell_count && i < side * side; i++) {
                cells[i] = ic_one(ic_first_of(recipe->cells[i]));
            }
        }
        /* Half of them spoiled in a cell or two. */
        const uint32_t spoil = (uint32_t)(ic_random(&state) % 4u);
        for (uint32_t i = 0u; i < spoil; i++) {
            const uint32_t at = (uint32_t)(ic_random(&state) % (side * side));
            cells[at] = ic_random(&state) % 3u == 0u ? (item_stack_t){0}
                                                     : ic_one((uint16_t)(1u + ic_random(&state) % (ITEM_COUNT - 1u)));
        }
        uint32_t expected = CRAFTING_NO_RECIPE;
        for (uint32_t row = 0u; row < RECIPE_COUNT && expected == CRAFTING_NO_RECIPE; row++) {
            const recipe_def_t *const candidate = &recipe_defs[row];
            if (candidate->station != station) {
                continue;
            }
            const bool fits = candidate->kind == RECIPE_KIND_SHAPELESS ? ic_shapeless_by_orders(candidate, cells, side)
                                                                       : ic_shaped_by_placing(candidate, cells, side);
            expected = fits ? row : CRAFTING_NO_RECIPE;
        }
        CHECK_EQ_U64(crafting_match(cells, side, station), expected);
        matched += expected != CRAFTING_NO_RECIPE ? 1u : 0u;
        unmatched += expected == CRAFTING_NO_RECIPE ? 1u : 0u;
    }
    printf("  %u matched, %u did not\n", matched, unmatched);
    CHECK(matched > IC_FUZZ_ROUNDS / 8u);
    CHECK(unmatched > IC_FUZZ_ROUNDS / 8u);
}

/* ---- A grid ----------------------------------------------------------------------------------------------------------- */

/* Puts down the whole of what the cursor holds on a cell, one by one as a player would for a stack, and checks where it went. */
static void ic_put(crafting_grid_t *grid, uint32_t cell, uint16_t item, uint32_t count) {
    item_stack_t cursor = item_stack_make(item, count);
    crafting_grid_click_primary(grid, cell, &cursor);
}

TEST(from_a_log_to_a_pickaxe_as_a_player_would) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    crafting_grid_t hands, table;
    crafting_grid_start(&hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE);
    crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    CHECK_EQ_U64(crafting_grid_cells(&hands), 4u);
    CHECK_EQ_U64(crafting_grid_cells(&table), 9u);
    CHECK(crafting_grid_cell(&hands, 4u) == nullptr);
    CHECK(crafting_grid_cell(&table, 9u) == nullptr);
    CHECK(item_stack_is_empty(&hands.output));
    CHECK_EQ_U64(hands.recipe, CRAFTING_NO_RECIPE);

    /* Three logs. A click takes one making, and the output stays until the logs run out. */
    item_stack_t cursor = {0};
    ic_put(&hands, 0u, ITEM_ID_OAK_LOG, 3u);
    CHECK_EQ_U64(hands.recipe, ic_recipe_named(t, "oak_planks"));
    CHECK_EQ_U64(hands.output.item, ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(hands.output.count, 4u);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 4u);
    CHECK_EQ_U64(cursor.item, ITEM_ID_OAK_PLANKS);
    CHECK_EQ_U64(cursor.count, 4u);
    CHECK_EQ_U64(hands.cells[0].count, 2u);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 4u);
    CHECK_EQ_U64(cursor.count, 8u);
    /* A shift-click takes the rest, and then there is nothing left to take. */
    CHECK_EQ_U64(crafting_grid_take_all(&hands, &inventory), 4u);
    CHECK_EQ_U64(hands.cells[0].count, 0u);
    CHECK(item_stack_is_empty(&hands.output));
    CHECK_EQ_U64(hands.crafted_count, 12u);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 0u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_OAK_PLANKS), 4u);

    /* Eight planks, four of them make a table, and two of what is left go to sticks. */
    (void)item_stack_take(&cursor, 4u);
    for (uint32_t i = 0u; i < 4u; i++) {
        crafting_grid_click_secondary(&hands, i, &cursor);
    }
    CHECK_EQ_U64(cursor.count, 0u);
    CHECK_EQ_U64(hands.output.item, ITEM_ID_CRAFTING_TABLE);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 1u);
    CHECK_EQ_U64(cursor.item, ITEM_ID_CRAFTING_TABLE);
    cursor = (item_stack_t){0};
    ic_put(&hands, 0u, ITEM_ID_OAK_PLANKS, 1u);
    ic_put(&hands, 2u, ITEM_ID_OAK_PLANKS, 1u);
    CHECK_EQ_U64(hands.output.item, ITEM_ID_STICK);
    CHECK_EQ_U64(crafting_grid_take(&hands, &cursor), 4u);

    /* At the bench, which this grid is: the pickaxe. A table makes none: see tests/run/item/crafting/station/. */
    ic_put(&table, 0u, ITEM_ID_OAK_PLANKS, 1u);
    ic_put(&table, 1u, ITEM_ID_OAK_PLANKS, 1u);
    ic_put(&table, 2u, ITEM_ID_OAK_PLANKS, 1u);
    /* Three of oak in a row are slabs, until the handle goes in. */
    CHECK_EQ_U64(table.output.item, ITEM_ID_OAK_SLAB);
    crafting_grid_click_secondary(&table, 4u, &cursor);
    crafting_grid_click_secondary(&table, 7u, &cursor);
    CHECK_EQ_U64(table.output.item, ITEM_ID_WOOD_PICKAXE);
    CHECK_EQ_U64(table.output.count, 1u);
    item_stack_t picked = {0};
    CHECK_EQ_U64(crafting_grid_take(&table, &picked), 1u);
    CHECK_EQ_U64(picked.item, ITEM_ID_WOOD_PICKAXE);
    CHECK_EQ_U64(picked.wear, 0u);
    CHECK(item_stack_is_valid(&picked));
    /* The pointer now carries a pickaxe, which does not stack, so no second one could come to it. */
    for (uint32_t i = 0u; i < RECIPE_CELLS; i++) {
        CHECK(item_stack_is_empty(&table.cells[i]));
    }
}

TEST(the_output_comes_whole_or_not_at_all) {
    crafting_grid_t grid;
    crafting_grid_start(&grid, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE);
    ic_put(&grid, 0u, ITEM_ID_OAK_LOG, 2u);

    /* The pointer holds something else. */
    item_stack_t cursor = item_stack_make(ITEM_ID_DIRT, 1u);
    CHECK_EQ_U64(crafting_grid_take(&grid, &cursor), 0u);
    CHECK_EQ_U64(cursor.count, 1u);
    CHECK_EQ_U64(grid.cells[0].count, 2u);

    /* The pointer has room for fewer than one making gives. */
    cursor = item_stack_make(ITEM_ID_OAK_PLANKS, 61u);
    CHECK_EQ_U64(crafting_grid_take(&grid, &cursor), 0u);
    CHECK_EQ_U64(cursor.count, 61u);
    CHECK_EQ_U64(grid.cells[0].count, 2u);
    CHECK_EQ_U64(grid.crafted_count, 0u);

    /* And just enough. */
    cursor = item_stack_make(ITEM_ID_OAK_PLANKS, 60u);
    CHECK_EQ_U64(crafting_grid_take(&grid, &cursor), 4u);
    CHECK_EQ_U64(cursor.count, 64u);
    CHECK_EQ_U64(grid.cells[0].count, 1u);
}

TEST(a_shift_click_takes_what_the_cells_and_the_inventory_allow) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    crafting_grid_t grid;
    crafting_grid_start(&grid, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE);
    ic_put(&grid, 0u, ITEM_ID_OAK_LOG, 64u);
    CHECK_EQ_U64(crafting_grid_take_all(&grid, &inventory), 256u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_OAK_PLANKS), 256u);
    CHECK_EQ_U64(grid.cells[0].count, 0u);
    CHECK_EQ_U64(inventory.added_count, 256u);

    /* Room for three of the next making's four, which is no making at all. */
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        *inventory_slot(&inventory, i) = item_stack_make(ITEM_ID_DIRT, 64u);
    }
    *inventory_slot(&inventory, 20u) = item_stack_make(ITEM_ID_OAK_PLANKS, 61u);
    ic_put(&grid, 0u, ITEM_ID_OAK_LOG, 3u);
    CHECK_EQ_U64(crafting_grid_take_all(&grid, &inventory), 0u);
    CHECK_EQ_U64(grid.cells[0].count, 3u);
    /* Room for two makings of the three. */
    *inventory_slot(&inventory, 20u) = item_stack_make(ITEM_ID_OAK_PLANKS, 56u);
    CHECK_EQ_U64(crafting_grid_take_all(&grid, &inventory), 8u);
    CHECK_EQ_U64(grid.cells[0].count, 1u);
    CHECK_EQ_U64(inventory_slot_read(&inventory, 20u)->count, 64u);
}

TEST(closing_a_screen_gives_back_what_fits_and_leaves_the_rest) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    crafting_grid_t grid;
    crafting_grid_start(&grid, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    ic_put(&grid, 0u, ITEM_ID_COAL, 10u);
    ic_put(&grid, 8u, ITEM_ID_STICK, 7u);
    CHECK_EQ_U64(crafting_grid_give_back(&grid, &inventory), 0u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_COAL), 10u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_STICK), 7u);
    CHECK(item_stack_is_empty(&grid.cells[0]));
    CHECK(item_stack_is_empty(&grid.output));

    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        *inventory_slot(&inventory, i) = item_stack_make(ITEM_ID_DIRT, 64u);
    }
    ic_put(&grid, 3u, ITEM_ID_COAL, 5u);
    CHECK_EQ_U64(crafting_grid_give_back(&grid, &inventory), 5u);
    CHECK_EQ_U64(grid.cells[3].count, 5u);
}

/* ---- A furnace -------------------------------------------------------------------------------------------------------- */

static smelting_store_t *ic_furnaces(test_context_t *t) {
    smelting_store_t *const store = test_alloc(t, sizeof(smelting_store_t));
    arena_t *const arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(arena, "ic_furnaces", test_alloc(t, SMELTING_BYTES), SMELTING_BYTES);
    smelting_start(store, arena);
    return store;
}

static void ic_ticks(smelting_store_t *store, smelting_furnace_t *furnace, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        (void)smelting_tick(store, furnace);
    }
}

TEST(one_coal_smelts_eight_and_it_takes_two_hundred_ticks_each) {
    smelting_store_t *const store = ic_furnaces(t);
    smelting_furnace_t *const furnace = smelting_open(store, 1, 2, 3);
    REQUIRE(furnace != nullptr);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 20u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    CHECK(!smelting_is_lit(furnace));

    ic_ticks(store, furnace, 199u);
    CHECK(smelting_is_lit(furnace));
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 0u);
    CHECK_NEAR(smelting_cook_fraction(furnace), 0.995f32, 0.0001f32);
    ic_ticks(store, furnace, 1u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].item, ITEM_ID_IRON_INGOT);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 1u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].count, 19u);

    ic_ticks(store, furnace, 1400u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 8u);
    CHECK_EQ_U64(store->smelted_count, 8u);
    CHECK_EQ_U64(store->burned_count, 1u);
    CHECK(smelting_is_lit(furnace)); /* its last tick */
    CHECK(!smelting_tick(store, furnace));
    /* With nothing to burn it stays at eight for ever, and no ninth comes. */
    ic_ticks(store, furnace, 1000u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 8u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].count, 12u);
    CHECK_EQ_U64(furnace->cook_ticks, 0u);
}

TEST(every_smelt_and_every_fuel_does_what_the_table_says) {
    smelting_store_t *const store = ic_furnaces(t);
    uint32_t smelts = 0u, burns = 0u;
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        const recipe_smelt_t *const recipe = smelting_recipe_of((uint16_t)item);
        if (recipe != nullptr) {
            smelts++;
            smelting_furnace_t *const furnace = smelting_open(store, (int32_t)item, 0, 0);
            REQUIRE(furnace != nullptr);
            furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make((uint16_t)item, 1u);
            /* Lava, which is not used up by any of them. */
            furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_LAVA_BUCKET, 1u);
            ic_ticks(store, furnace, (uint32_t)recipe->ticks - 1u);
            CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));
            ic_ticks(store, furnace, 1u);
            CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].item, recipe->gives);
            CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, recipe->count);
            CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_INPUT]));
        }
        const uint32_t fuel_ticks = smelting_burn_ticks((uint16_t)item);
        if (fuel_ticks != 0u) {
            burns++;
            smelting_furnace_t *const furnace = smelting_open(store, (int32_t)item, 1, 0);
            REQUIRE(furnace != nullptr);
            furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_COBBLESTONE, 64u);
            furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make((uint16_t)item, 1u);
            ic_ticks(store, furnace, fuel_ticks);
            CHECK(smelting_is_lit(furnace));
            CHECK(!smelting_tick(store, furnace));
            /* A stack of 64 is all the input there is, however long it burns. */
            CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, fuel_ticks / 200u < 64u ? fuel_ticks / 200u : 64u);
        }
    }
    printf("  %u items smelt, %u burn\n", smelts, burns);
    CHECK(smelts >= 20u);
    CHECK(burns >= 20u);
}

TEST(fuel_goes_on_only_for_what_can_cook) {
    smelting_store_t *const store = ic_furnaces(t);
    smelting_furnace_t *const furnace = smelting_open(store, 0, 0, 0);
    REQUIRE(furnace != nullptr);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 3u);

    /* Nothing in. */
    ic_ticks(store, furnace, 50u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 3u);
    CHECK(!smelting_is_lit(furnace));
    /* What does not smelt. */
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_DIRT, 3u);
    ic_ticks(store, furnace, 50u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 3u);
    /* The output holding something else. */
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 3u);
    furnace->slots[SMELTING_SLOT_OUTPUT] = item_stack_make(ITEM_ID_COAL, 1u);
    ic_ticks(store, furnace, 50u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 3u);
    /* The output full. */
    furnace->slots[SMELTING_SLOT_OUTPUT] = item_stack_make(ITEM_ID_IRON_INGOT, 64u);
    ic_ticks(store, furnace, 50u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 3u);
    CHECK_EQ_U64(store->burned_count, 0u);
    /* Room comes, and it starts. */
    furnace->slots[SMELTING_SLOT_OUTPUT] = item_stack_make(ITEM_ID_IRON_INGOT, 63u);
    ic_ticks(store, furnace, 200u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 2u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 64u);
    /* Full again, burning on, with what was being done gone at once. */
    CHECK_EQ_U64(furnace->cook_ticks, 0u);
    ic_ticks(store, furnace, 10u);
    CHECK_EQ_U64(furnace->cook_ticks, 0u);
    CHECK(smelting_is_lit(furnace));
    /* The fuel on the fire burns away for nothing, and no more goes on. */
    ic_ticks(store, furnace, 1600u);
    CHECK(!smelting_is_lit(furnace));
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 2u);
}

TEST(going_out_cools_it_and_changing_the_input_starts_over) {
    smelting_store_t *const store = ic_furnaces(t);
    smelting_furnace_t *const furnace = smelting_open(store, 0, 0, 0);
    REQUIRE(furnace != nullptr);
    /* A stick burns 100 ticks. */
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 2u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_STICK, 1u);
    ic_ticks(store, furnace, 100u);
    CHECK_EQ_U64(furnace->cook_ticks, 100u);
    CHECK(smelting_is_lit(furnace));
    ic_ticks(store, furnace, 1u);
    CHECK(!smelting_is_lit(furnace));
    CHECK_EQ_U64(furnace->cook_ticks, 98u);
    ic_ticks(store, furnace, 1u);
    CHECK_EQ_U64(furnace->cook_ticks, 96u);
    ic_ticks(store, furnace, 1000u);
    CHECK_EQ_U64(furnace->cook_ticks, 0u);

    /* Half done, and then another thing that smelts. */
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    ic_ticks(store, furnace, 100u);
    CHECK_EQ_U64(furnace->cook_ticks, 100u);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 1u);
    ic_ticks(store, furnace, 1u);
    CHECK_EQ_U64(furnace->cook_ticks, 1u);
    /* Taken out, and put back. */
    furnace->slots[SMELTING_SLOT_INPUT] = (item_stack_t){0};
    ic_ticks(store, furnace, 1u);
    CHECK_EQ_U64(furnace->cook_ticks, 0u);
    CHECK_NEAR(smelting_cook_fraction(furnace), 0.0f32, 0.0f32);
    CHECK(smelting_burn_fraction(furnace) > 0.0f32 && smelting_burn_fraction(furnace) < 1.0f32);
}

TEST(a_lava_bucket_leaves_a_bucket_which_does_not_burn) {
    smelting_store_t *const store = ic_furnaces(t);
    smelting_furnace_t *const furnace = smelting_open(store, 0, 0, 0);
    REQUIRE(furnace != nullptr);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_COBBLESTONE, 64u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_LAVA_BUCKET, 1u);
    ic_ticks(store, furnace, 1u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].item, ITEM_ID_BUCKET);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 1u);
    CHECK(item_stack_is_valid(&furnace->slots[SMELTING_SLOT_FUEL]));
    ic_ticks(store, furnace, 64u * 200u + 500u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 64u);
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_INPUT]));
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].item, ITEM_ID_BUCKET);
    /* It burned on with nothing to do, and has gone out with its 20000 ticks. */
    ic_ticks(store, furnace, 20000u);
    CHECK(!smelting_is_lit(furnace));
    CHECK_EQ_U64(store->burned_count, 1u);
}

TEST(what_a_screen_may_do_with_a_furnace) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    smelting_store_t *const store = ic_furnaces(t);
    smelting_furnace_t *const furnace = smelting_open(store, 0, 0, 0);
    REQUIRE(furnace != nullptr);

    /* Only what burns goes in the fuel slot. */
    item_stack_t cursor = item_stack_make(ITEM_ID_DIRT, 5u);
    smelting_click(furnace, SMELTING_SLOT_FUEL, &cursor, false);
    CHECK_EQ_U64(cursor.count, 5u);
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_FUEL]));
    cursor = item_stack_make(ITEM_ID_COAL, 5u);
    smelting_click(furnace, SMELTING_SLOT_FUEL, &cursor, true);
    CHECK_EQ_U64(cursor.count, 4u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 1u);
    smelting_click(furnace, SMELTING_SLOT_FUEL, &cursor, false);
    CHECK_EQ_U64(cursor.count, 0u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 5u);

    /* Anything goes in the input, as it just waits. */
    cursor = item_stack_make(ITEM_ID_DIRT, 5u);
    smelting_click(furnace, SMELTING_SLOT_INPUT, &cursor, false);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].item, ITEM_ID_DIRT);

    /* Nothing goes in the output, and what comes out comes whole. */
    furnace->slots[SMELTING_SLOT_OUTPUT] = item_stack_make(ITEM_ID_GLASS, 10u);
    cursor = item_stack_make(ITEM_ID_DIRT, 1u);
    smelting_click(furnace, SMELTING_SLOT_OUTPUT, &cursor, false);
    CHECK_EQ_U64(cursor.item, ITEM_ID_DIRT);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 10u);
    cursor = item_stack_make(ITEM_ID_GLASS, 60u);
    smelting_click(furnace, SMELTING_SLOT_OUTPUT, &cursor, false);
    CHECK_EQ_U64(cursor.count, 60u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 10u);
    cursor = item_stack_make(ITEM_ID_GLASS, 54u);
    smelting_click(furnace, SMELTING_SLOT_OUTPUT, &cursor, true);
    CHECK_EQ_U64(cursor.count, 64u);
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));

    /* A shift-click: what smelts to the input, even a log that also burns, and what only burns to the fuel. */
    furnace->slots[SMELTING_SLOT_INPUT] = (item_stack_t){0};
    furnace->slots[SMELTING_SLOT_FUEL] = (item_stack_t){0};
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_OAK_LOG, 7u), 7u);
    CHECK_EQ_U64(smelting_quick_move_in(furnace, &inventory, 0u), 7u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].item, ITEM_ID_OAK_LOG);
    CHECK_EQ_U64(inventory.removed_count, 7u);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 3u), 3u);
    CHECK_EQ_U64(smelting_quick_move_in(furnace, &inventory, 0u), 3u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].item, ITEM_ID_COAL);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_DIRT, 3u), 3u);
    CHECK_EQ_U64(smelting_quick_move_in(furnace, &inventory, 0u), 0u);
    CHECK_EQ_U64(smelting_quick_move_in(furnace, &inventory, 35u), 0u); /* an empty slot */
    CHECK_EQ_U64(smelting_quick_move_in(furnace, &inventory, 36u), 0u); /* no slot */
    CHECK_EQ_U64(smelting_quick_move_out(furnace, &inventory, SMELTING_SLOT_FUEL), 3u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_COAL), 3u);
    CHECK_EQ_U64(smelting_quick_move_out(furnace, &inventory, SMELTING_SLOTS), 0u);
    CHECK_EQ_U64(inventory.added_count, inventory.removed_count + inventory_items_held(&inventory));
}

TEST(a_store_of_furnaces_finds_opens_refuses_and_gives_back) {
    smelting_store_t *const store = ic_furnaces(t);
    CHECK(smelting_at(store, 0, 0, 0) == nullptr);
    item_stack_t out[SMELTING_SLOTS];
    CHECK_EQ_U64(smelting_remove(store, 0, 0, 0, out), 0u);
    for (uint32_t i = 0u; i < SMELTING_FURNACES; i++) {
        smelting_furnace_t *const furnace = smelting_open(store, (int32_t)i, -1, 5);
        REQUIRE(furnace != nullptr);
        furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 1u + i % 64u);
    }
    /* What is there is found, however often it is opened. */
    CHECK_EQ_U64(smelting_open(store, 7, -1, 5)->slots[SMELTING_SLOT_INPUT].count, 8u);
    CHECK_EQ_U64(store->count, SMELTING_FURNACES);
    CHECK(smelting_open(store, 100000, 0, 0) == nullptr);
    CHECK_EQ_U64(store->refused_count, 1u);

    /* Going takes what it held, and the last one takes its place. */
    CHECK_EQ_U64(smelting_remove(store, 7, -1, 5, out), 1u);
    CHECK_EQ_U64(out[0].count, 8u);
    CHECK(smelting_at(store, 7, -1, 5) == nullptr);
    const smelting_furnace_t *const last = smelting_at(store, (int32_t)SMELTING_FURNACES - 1, -1, 5);
    REQUIRE(last != nullptr);
    CHECK_EQ_U64(last->slots[SMELTING_SLOT_INPUT].count, 1u + (SMELTING_FURNACES - 1u) % 64u);
    CHECK(smelting_open(store, 100000, 0, 0) != nullptr);
}

/* ---- A chest ---------------------------------------------------------------------------------------------------------- */

static container_store_t *ic_chests(test_context_t *t) {
    container_store_t *const store = test_alloc(t, sizeof(container_store_t));
    arena_t *const arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(arena, "ic_chests", test_alloc(t, CONTAINER_BYTES), CONTAINER_BYTES);
    container_start(store, arena);
    return store;
}

TEST(a_chest_holds_what_is_put_in_it_where_an_inventory_would) {
    container_store_t *const store = ic_chests(t);
    container_chest_t *const chest = container_open(store, 1, 2, 3);
    REQUIRE(chest != nullptr);
    CHECK_EQ_I64(chest->cell[2], 3);
    CHECK_EQ_U64(container_items_held(chest), 0u);
    CHECK(container_slot(chest, CONTAINER_CHEST_SLOTS) == nullptr);

    /* On to stacks of it, however far along. */
    *container_slot(chest, 5u) = item_stack_make(ITEM_ID_DIRT, 60u);
    item_stack_t stack = item_stack_make(ITEM_ID_DIRT, 10u);
    CHECK_EQ_U64(container_add_stack(chest, &stack), 10u);
    CHECK_EQ_U64(chest->slots[5].count, 64u);
    CHECK_EQ_U64(chest->slots[0].count, 6u);
    CHECK(item_stack_is_empty(&stack));
    CHECK_EQ_U64(container_items_held(chest), 70u);

    /* A worn tool keeps its wear. */
    item_stack_t tool = item_stack_make(ITEM_ID_IRON_PICKAXE, 1u);
    tool.wear = 17u;
    CHECK_EQ_U64(container_add_stack(chest, &tool), 1u);
    CHECK_EQ_U64(chest->slots[1].wear, 17u);

    /* Full, and what is not taken stays. */
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS; i++) {
        chest->slots[i] = item_stack_make(ITEM_ID_COAL, 64u);
    }
    stack = item_stack_make(ITEM_ID_DIRT, 10u);
    CHECK_EQ_U64(container_add_stack(chest, &stack), 0u);
    CHECK_EQ_U64(stack.count, 10u);
    chest->slots[26] = item_stack_make(ITEM_ID_DIRT, 60u);
    CHECK_EQ_U64(container_add_stack(chest, &stack), 4u);
    CHECK_EQ_U64(stack.count, 6u);
}

TEST(a_shift_click_goes_each_way_and_the_books_balance) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    container_store_t *const store = ic_chests(t);
    container_chest_t *const chest = container_open(store, 0, 0, 0);
    REQUIRE(chest != nullptr);
    CHECK_EQ_U64(inventory_add(&inventory, ITEM_ID_COAL, 40u), 40u);
    chest->slots[3] = item_stack_make(ITEM_ID_COAL, 50u);
    CHECK_EQ_U64(container_quick_move_in(chest, &inventory, 0u), 40u);
    CHECK_EQ_U64(chest->slots[3].count, 64u);
    CHECK_EQ_U64(chest->slots[0].count, 26u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_COAL), 0u);
    CHECK_EQ_U64(inventory.removed_count, 40u);
    CHECK_EQ_U64(container_quick_move_in(chest, &inventory, 0u), 0u);
    CHECK_EQ_U64(container_quick_move_in(chest, &inventory, INVENTORY_SLOTS), 0u);

    CHECK_EQ_U64(container_quick_move_out(chest, &inventory, 3u), 64u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_COAL), 64u);
    CHECK(item_stack_is_empty(&chest->slots[3]));
    CHECK_EQ_U64(container_quick_move_out(chest, &inventory, CONTAINER_CHEST_SLOTS), 0u);
    CHECK_EQ_U64(inventory.added_count, inventory.removed_count + inventory_items_held(&inventory));
    CHECK_EQ_U64(inventory_room(&inventory, ITEM_ID_COAL, 10u), 10u);

    /* An inventory with no room takes what it can, and the chest keeps the rest. */
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        *inventory_slot(&inventory, i) = item_stack_make(ITEM_ID_DIRT, 64u);
    }
    chest->slots[0] = item_stack_make(ITEM_ID_DIRT, 5u);
    (void)item_stack_take(inventory_slot(&inventory, 9u), 2u);
    CHECK_EQ_U64(container_quick_move_out(chest, &inventory, 0u), 2u);
    CHECK_EQ_U64(chest->slots[0].count, 3u);
}

TEST(a_store_of_chests_finds_opens_refuses_and_gives_back) {
    container_store_t *const store = ic_chests(t);
    CHECK(container_at(store, 0, 0, 0) == nullptr);
    item_stack_t out[CONTAINER_CHEST_SLOTS];
    CHECK_EQ_U64(container_remove(store, 0, 0, 0, out), 0u);
    for (uint32_t i = 0u; i < CONTAINER_CHESTS; i++) {
        container_chest_t *const chest = container_open(store, (int32_t)i, 9, 9);
        REQUIRE(chest != nullptr);
        chest->slots[i % CONTAINER_CHEST_SLOTS] = item_stack_make(ITEM_ID_COAL, 1u + i % 64u);
        chest->slots[(i + 1u) % CONTAINER_CHEST_SLOTS] = item_stack_make(ITEM_ID_DIRT, 1u);
    }
    CHECK(container_open(store, 100000, 0, 0) == nullptr);
    CHECK_EQ_U64(store->refused_count, 1u);
    CHECK_EQ_U64(container_items_held(container_open(store, 40, 9, 9)), 1u + 40u + 1u);
    CHECK_EQ_U64(container_remove(store, 40, 9, 9, out), 2u);
    CHECK(container_at(store, 40, 9, 9) == nullptr);
    CHECK_EQ_U64(store->count, CONTAINER_CHESTS - 1u);
    const container_chest_t *const last = container_at(store, (int32_t)CONTAINER_CHESTS - 1, 9, 9);
    REQUIRE(last != nullptr);
    CHECK_EQ_U64(last->slots[(CONTAINER_CHESTS - 1u) % CONTAINER_CHEST_SLOTS].count,
                 1u + (CONTAINER_CHESTS - 1u) % 64u);
}

TEST(at_random_nothing_is_lost_or_made_between_a_chest_and_an_inventory) {
    arena_t arena;
    inventory_t inventory;
    ic_inventory(t, &inventory, &arena);
    container_store_t *const store = ic_chests(t);
    container_chest_t *const chest = container_open(store, 0, 0, 0);
    REQUIRE(chest != nullptr);
    uint64_t state = 0xC0FFEEu;
    uint64_t total = 0u;
    const uint16_t items[] = {ITEM_ID_DIRT, ITEM_ID_COAL, ITEM_ID_IRON_PICKAXE, ITEM_ID_STICK};
    for (uint32_t step = 0u; step < IC_RANDOM_STEPS; step++) {
        switch (ic_random(&state) % 4u) {
        case 0u:
            total += inventory_add(&inventory, items[ic_random(&state) % 4u], (uint32_t)(ic_random(&state) % 16u));
            break;
        case 1u:
            (void)container_quick_move_in(chest, &inventory, (uint32_t)(ic_random(&state) % (INVENTORY_SLOTS + 2u)));
            break;
        case 2u:
            (void)container_quick_move_out(chest, &inventory,
                                           (uint32_t)(ic_random(&state) % (CONTAINER_CHEST_SLOTS + 2u)));
            break;
        default: {
            const uint16_t item = items[ic_random(&state) % 4u];
            total -= inventory_remove(&inventory, item, (uint32_t)(ic_random(&state) % 8u));
            break;
        }
        }
        CHECK_EQ_U64(inventory_items_held(&inventory) + container_items_held(chest), total);
    }
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS; i++) {
        CHECK(item_stack_is_valid(&chest->slots[i]));
    }
    CHECK(total > 0u);
}

/* ---- The Owner, and the world ----------------------------------------------------------------------------------------- */

enum : uint32_t {
    IC_SPILLS_MAX = 64u,
};

/* What spilled. */
typedef struct ic_spills_t {
    item_stack_t stacks[IC_SPILLS_MAX]; /* 0 */
    int32_t cell[3];                    /* 512 */
    uint32_t count;                     /* 524 */
} ic_spills_t;
static_assert(sizeof(ic_spills_t) == 528);

static void ic_spill(void *borrowed_spills, const item_stack_t *stack, const int32_t cell[3]) {
    ic_spills_t *const spills = borrowed_spills;
    if (spills->count < IC_SPILLS_MAX) {
        spills->stacks[spills->count] = *stack;
        spills->count++;
    }
    memcpy(spills->cell, cell, sizeof(spills->cell));
}

static item_t *ic_item(test_context_t *t) {
    item_t *const item = test_world_aligned(t, sizeof(item_t));
    return item_init(item) ? item : nullptr;
}

static uint16_t ic_block(test_world_t *w, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    (void)world_map_block_get(w->map, x, y, z, &block);
    return block;
}

TEST(the_owner_holds_a_world_of_each_and_a_new_world_starts_empty) {
    heap_counters_t before, after;
    heap_counters_read(&before);
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    CHECK_EQ_U64(item->crafting_hands.side, CRAFTING_SIDE_HANDS);
    CHECK_EQ_U64(item->crafting_table.side, CRAFTING_SIDE_TABLE);
    REQUIRE(container_open(&item->chests, 1, 1, 1) != nullptr);
    REQUIRE(smelting_open(&item->furnaces, 1, 1, 1) != nullptr);
    ic_put(&item->crafting_hands, 0u, ITEM_ID_DIRT, 5u);
    /* Everything at once fits the region, up to the last chest and furnace. */
    for (uint32_t i = 0u; i < CONTAINER_CHESTS; i++) {
        (void)container_open(&item->chests, (int32_t)i + 2, 0, 0);
    }
    for (uint32_t i = 0u; i < SMELTING_FURNACES; i++) {
        (void)smelting_open(&item->furnaces, (int32_t)i + 2, 0, 0);
    }
    item_close(item);
    CHECK(item->chests.borrowed_chests == nullptr);
    item_open(item);
    CHECK_EQ_U64(item->chests.count, 0u);
    CHECK_EQ_U64(item->furnaces.count, 0u);
    CHECK(item_stack_is_empty(&item->crafting_hands.cells[0]));
    item_close(item);
    item_deinit(item);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST(a_chest_that_goes_spills_all_it_held_and_one_that_comes_starts_empty) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    item_world_begin(item, edit);
    ic_spills_t spills = {0};

    /* What was edited before it looked is nothing to it. */
    REQUIRE(world_edit_block_set(edit, 3, 3, 3, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(item->chests.count, 0u);

    REQUIRE(world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(item->chests.count, 1u);
    container_chest_t *const chest = container_at(&item->chests, 3, 3, 4);
    REQUIRE(chest != nullptr);
    chest->slots[0] = item_stack_make(ITEM_ID_DIRT, 64u);
    chest->slots[26] = item_stack_make(ITEM_ID_IRON_PICKAXE, 1u);
    chest->slots[13] = item_stack_make(ITEM_ID_COAL, 7u);

    /* Nothing happens to it in a quiet tick. */
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(spills.count, 0u);
    CHECK_EQ_U64(container_items_held(chest), 72u);

    REQUIRE(world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(item->chests.count, 0u);
    CHECK_EQ_U64(spills.count, 3u);
    CHECK_EQ_U64(spills.stacks[0].item, ITEM_ID_DIRT);
    CHECK_EQ_U64(spills.stacks[0].count, 64u);
    CHECK_EQ_U64(spills.stacks[1].item, ITEM_ID_COAL);
    CHECK_EQ_U64(spills.stacks[2].item, ITEM_ID_IRON_PICKAXE);
    CHECK_EQ_I64(spills.cell[0], 3);
    CHECK_EQ_I64(spills.cell[2], 4);
    CHECK_EQ_U64(item->spilled_count, 72u);

    /* A new one holds none of it, and neither does one whose old slots were still kept for that cell. */
    spills.count = 0u;
    REQUIRE(world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(container_items_held(container_at(&item->chests, 3, 3, 4)), 0u);
    container_at(&item->chests, 3, 3, 4)->slots[0] = item_stack_make(ITEM_ID_DIRT, 3u);
    REQUIRE(world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(spills.count, 1u); /* the going, as it should, and the coming held nothing */
    CHECK_EQ_U64(container_items_held(container_at(&item->chests, 3, 3, 4)), 0u);

    /* An empty chest, broken, spills nothing. */
    spills.count = 0u;
    (void)world_edit_block_set(edit, 3, 3, 4, BLOCK_ID_AIR);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(spills.count, 0u);

    item_close(item);
    item_deinit(item);
    test_world_end(&world);
}

TEST(a_furnace_lights_its_block_burns_and_goes_out_and_spills_when_it_goes) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    item_world_begin(item, edit);
    ic_spills_t spills = {0};
    const int32_t x = 5, y = 6, z = 7;

    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_FURNACE) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    smelting_furnace_t *const furnace = smelting_at(&item->furnaces, x, y, z);
    REQUIRE(furnace != nullptr);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(ic_block(&world, x, y, z), BLOCK_ID_FURNACE);

    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 3u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_STICK, 1u);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(ic_block(&world, x, y, z), BLOCK_ID_FURNACE_LIT);
    CHECK_EQ_U64(block_defs[BLOCK_ID_FURNACE_LIT].light, 13u);
    /* Lighting up neither spills it nor starts it over. */
    for (uint32_t i = 0u; i < 10u; i++) {
        item_world_tick(item, edit, ic_spill, &spills);
    }
    CHECK_EQ_U64(spills.count, 0u);
    CHECK_EQ_U64(item->furnaces.count, 1u);
    CHECK_EQ_U64(furnace->cook_ticks, 11u);
    /* A stick is 100 ticks. */
    for (uint32_t i = 0u; i < 90u; i++) {
        item_world_tick(item, edit, ic_spill, &spills);
    }
    CHECK_EQ_U64(ic_block(&world, x, y, z), BLOCK_ID_FURNACE);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].count, 3u);

    /* Lit again, and broken lit, it gives back what it holds. */
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 4u);
    for (uint32_t i = 0u; i < 300u; i++) {
        item_world_tick(item, edit, ic_spill, &spills);
    }
    CHECK_EQ_U64(ic_block(&world, x, y, z), BLOCK_ID_FURNACE_LIT);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].count, 1u);
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(item->furnaces.count, 0u);
    CHECK_EQ_U64(spills.count, 3u);
    CHECK_EQ_U64(spills.stacks[0].item, ITEM_ID_RAW_IRON);
    CHECK_EQ_U64(spills.stacks[0].count, 2u);
    CHECK_EQ_U64(spills.stacks[1].item, ITEM_ID_COAL);
    CHECK_EQ_U64(spills.stacks[1].count, 3u);
    CHECK_EQ_U64(spills.stacks[2].item, ITEM_ID_IRON_INGOT);
    /* Nothing is left to change the block back. */
    CHECK_EQ_U64(ic_block(&world, x, y, z), BLOCK_ID_AIR);

    item_close(item);
    item_deinit(item);
    test_world_end(&world);
}

TEST(a_furnace_waits_for_its_column_and_lets_go_of_a_cell_without_one) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    item_world_begin(item, edit);
    ic_spills_t spills = {0};

    /* No column here at all. */
    const int32_t far = 1 << 20;
    uint16_t block = 0u;
    REQUIRE(world_map_block_get(world.map, far, far, 0, &block) != WORLD_MAP_STATUS_OK);
    smelting_furnace_t *const waiting = smelting_open(&item->furnaces, far, far, 0);
    REQUIRE(waiting != nullptr);
    waiting->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 1u);
    waiting->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    for (uint32_t i = 0u; i < 300u; i++) {
        item_world_tick(item, edit, ic_spill, &spills);
    }
    CHECK_EQ_U64(item->furnaces.count, 1u);
    CHECK_EQ_U64(waiting->cook_ticks, 0u);
    CHECK_EQ_U64(waiting->slots[SMELTING_SLOT_FUEL].count, 1u);

    /* A column here that has no furnace in it: its own was lost with the column's edits, and so is this. */
    REQUIRE(smelting_open(&item->furnaces, 1, 1, 1) != nullptr);
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK(smelting_at(&item->furnaces, 1, 1, 1) == nullptr);
    CHECK_EQ_U64(spills.count, 0u);

    item_close(item);
    item_deinit(item);
    test_world_end(&world);
}

TEST(if_the_log_has_gone_round_past_it_it_asks_the_world) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    item_world_begin(item, edit);
    ic_spills_t spills = {0};
    REQUIRE(world_edit_block_set(edit, 2, 2, 2, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 2, 3, 2, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 2, 4, 2, BLOCK_ID_FURNACE) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    container_at(&item->chests, 2, 2, 2)->slots[0] = item_stack_make(ITEM_ID_DIRT, 9u);
    container_at(&item->chests, 2, 3, 2)->slots[0] = item_stack_make(ITEM_ID_COAL, 8u);
    smelting_at(&item->furnaces, 2, 4, 2)->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_DIRT, 7u);

    REQUIRE(world_edit_block_set(edit, 2, 2, 2, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 2, 4, 2, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    const uint64_t edits = edit->log_capacity + 16u;
    for (uint64_t i = 0u; i < edits; i++) {
        REQUIRE(world_edit_block_set(edit, 9, 9, 9, i % 2u == 0u ? BLOCK_ID_STONE : BLOCK_ID_AIR) ==
                WORLD_EDIT_STATUS_CHANGED);
    }
    item_world_tick(item, edit, ic_spill, &spills);
    CHECK_EQ_U64(spills.count, 2u);
    CHECK_EQ_U64(item->spilled_count, 16u);
    CHECK_EQ_U64(item->chests.count, 1u);
    CHECK_EQ_U64(item->furnaces.count, 0u);
    /* The one still standing kept what it held. */
    CHECK_EQ_U64(container_items_held(container_at(&item->chests, 2, 3, 2)), 8u);

    item_close(item);
    item_deinit(item);
    test_world_end(&world);
}

TEST(nothing_here_touches_the_heap_while_playing) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    item_t *const item = ic_item(t);
    REQUIRE(item != nullptr);
    item_open(item);
    item_world_begin(item, edit);
    ic_spills_t spills = {0};
    REQUIRE(world_edit_block_set(edit, 4, 4, 4, BLOCK_ID_FURNACE) == WORLD_EDIT_STATUS_CHANGED);
    item_world_tick(item, edit, ic_spill, &spills);
    smelting_furnace_t *const furnace = smelting_at(&item->furnaces, 4, 4, 4);
    REQUIRE(furnace != nullptr);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 64u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 64u);
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 2000u; i++) {
        item_world_tick(item, edit, ic_spill, &spills);
        ic_put(&item->crafting_table, i % 9u, ITEM_ID_OAK_PLANKS, 1u);
        (void)crafting_grid_give_back(&item->crafting_table, &item->inventory);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    CHECK_EQ_U64(item->furnaces.smelted_count, 10u);
    item_close(item);
    item_deinit(item);
    test_world_end(&world);
}

TEST(the_same_clicks_give_the_same_bytes) {
    smelting_furnace_t results[2];
    for (uint32_t run = 0u; run < 2u; run++) {
        smelting_store_t *const store = ic_furnaces(t);
        smelting_furnace_t *const furnace = smelting_open(store, 0, 0, 0);
        REQUIRE(furnace != nullptr);
        uint64_t state = 1234u;
        for (uint32_t i = 0u; i < 20000u; i++) {
            const uint64_t roll = ic_random(&state) % 500u;
            if (roll == 0u) {
                furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 2u);
            } else if (roll == 1u) {
                furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 20u);
            } else if (roll == 2u) {
                furnace->slots[SMELTING_SLOT_OUTPUT] = (item_stack_t){0};
            }
            (void)smelting_tick(store, furnace);
        }
        results[run] = *furnace;
    }
    CHECK_MEM_EQ(&results[0], &results[1], sizeof(results[0]));
}

/* ---- A real game ------------------------------------------------------------------------------------------------------ */

static void ic_spill_nowhere(void *borrowed_nothing, const item_stack_t *stack, const int32_t cell[3]) {
    (void)borrowed_nothing;
    (void)stack;
    (void)cell;
}

/* game_sim_tick's road: a chest broken while the threads run throws what it held on the ground as items the store can draw, and a furnace
 * cooks and is lit by the tick alone. */
TEST(in_a_real_game_a_broken_chest_spills_on_the_ground_and_a_furnace_burns) {
    gpu_bridge_t *const bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    job_system_t *const job = test_world_aligned(t, sizeof(job_system_t));
    world_t *const world = test_world_aligned(t, sizeof(world_t));
    terrain_t *const terrain = test_world_aligned(t, sizeof(terrain_t));
    render_t *const render = test_world_aligned(t, sizeof(render_t));
    texture_t *const texture = test_world_aligned(t, sizeof(texture_t));
    game_t *const game = test_world_aligned(t, sizeof(game_t));
    physics_t *const physics = test_world_aligned(t, sizeof(physics_t));
    player_t *const player = test_world_aligned(t, sizeof(player_t));
    entity_t *const entity = test_world_aligned(t, sizeof(entity_t));
    item_t *const held = test_world_aligned(t, sizeof(item_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(CHECK_EQ_U64(gpu_bridge_init(bridge, gpu_bridge_limits_default()), GPU_STATUS_OK));
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    REQUIRE(physics_init(physics));
    REQUIRE(entity_init(entity, physics));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    REQUIRE(item_init(held));
    player_init(player, physics);
    game_init(game, world, terrain, render, job, texture, topology);
    game_player_set(game, player);
    game_entity_set(game, entity);
    game_item_set(game, held);
    static const game_config_t small = {.render_distance = 5u, .width = IC_WIDTH, .height = IC_HEIGHT, .show_date = 0u};
    REQUIRE(game_open(game, &small));
    REQUIRE(game_settle(game));

    /* Away from the body, which is not to pick it up. */
    const int32_t x = (int32_t)floor(player->movement.position[0]) + 12;
    const int32_t y = (int32_t)floor(player->movement.position[1]);
    const int32_t z = (int32_t)floor(player->movement.position[2]) + 1;
    world_edit_t *const edit = &world->edit;
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, x, y + 3, z, BLOCK_ID_FURNACE) == WORLD_EDIT_STATUS_CHANGED);
    /* The threads are not up yet, so this is the one tick of it that this thread may do. */
    item_world_tick(held, edit, ic_spill_nowhere, nullptr);
    container_chest_t *const chest = container_at(&held->chests, x, y, z);
    smelting_furnace_t *const furnace = smelting_at(&held->furnaces, x, y + 3, z);
    REQUIRE(chest != nullptr);
    REQUIRE(furnace != nullptr);
    chest->slots[4] = item_stack_make(ITEM_ID_IRON_PICKAXE, 1u);
    chest->slots[9] = item_stack_make(ITEM_ID_COAL, 5u);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_SAND, 2u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);

    const uint64_t bytes = post_xrgb_bytes(IC_WIDTH, IC_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = IC_WIDTH * 4u,
                               .width = IC_WIDTH,
                               .height = IC_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    const int64_t give_up = time_now_ns() + 40000000000;
    while (time_now_ns() < give_up && held->furnaces.smelted_count == 0u) {
        game_walk_send(game, &none, false, false, time_now_ns());
        CHECK(game_loop_frame(game, &none, 0.0f32, &target));
        time_sleep_ns(4000000);
    }
    CHECK(game_loop_stop(game));

    const item_drop_store_t *const drops = &entity->item_drops;
    CHECK_EQ_U64(held->spilled_count, 6u);
    CHECK_EQ_U64(held->chests.count, 0u);
    /* At least: a plant that stood on what the test changed comes out as well, which is the world doing its own part. */
    CHECK(drops->dropped_count >= 6u);
    CHECK_EQ_U64(item_drop_on_ground(drops), drops->dropped_count);
    uint32_t coal = 0u, pickaxes = 0u;
    for (uint32_t i = 0u; i < drops->count; i++) {
        const uint16_t item = block_drop_items[drops->borrowed_items[i].item].item;
        coal += item == ITEM_ID_COAL ? drops->borrowed_items[i].count : 0u;
        pickaxes += item == ITEM_ID_IRON_PICKAXE ? drops->borrowed_items[i].count : 0u;
    }
    CHECK_EQ_U64(coal, 5u);
    CHECK_EQ_U64(pickaxes, 1u);

    CHECK_EQ_U64(held->furnaces.smelted_count, 1u);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].item, ITEM_ID_GLASS);
    uint16_t block = BLOCK_ID_AIR;
    CHECK_EQ_U64(world_map_block_get(&world->map, x, y + 3, z, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_FURNACE_LIT);

    game_close(game);
    game_deinit(game);
    render_deinit(render);
    CHECK(gpu_bridge_exit_check(bridge));
    item_deinit(held);
    entity_deinit(entity);
    player_deinit(player);
    physics_deinit(physics);
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
}

/* ---- The program ------------------------------------------------------------------------------------------------------ */

static void ic_temp_dir_remove(const char *dir) {
    DIR *const handle = opendir(dir);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir);
}

TEST(the_program_runs_with_all_of_this_and_says_so) {
    char dir[] = "/tmp/mc_c_item_crafting_XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    char script[128];
    snprintf(script, sizeof(script), "%s/walk.script", dir);
    static const char text[] = "1 move 0 500\n20 end\n";
    CHECK_EQ_U64(file_write_atomic(script, text, strlen(text)), FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"3",
                          (char *)"--script",
                          script,
                          (char *)"--screenshot-dir",
                          dir,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, IC_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "game: 0 chests and 0 furnaces stand; 0 items smelted, 0 of fuel burned, 0 spilled");
    ic_temp_dir_remove(dir);
}

TEST_MAIN()
