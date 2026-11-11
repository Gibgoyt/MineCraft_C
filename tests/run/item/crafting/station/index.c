/* ctest run_item_crafting_station. See README.md. */
#include "harness/index.h"

#include "item/crafting/index.h"
#include "item/smelting/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/recipes/index.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The item that is each station's block, written here by hand and not read from the files, so that the files are what is tested. The
 * hands need none. */
static const uint16_t IS_STATION_ITEM[RECIPE_STATION_COUNT] = {
    [RECIPE_STATION_TABLE] = ITEM_ID_CRAFTING_TABLE,
    [RECIPE_STATION_WOODWORKING] = ITEM_ID_WOODWORKING_BENCH,
    [RECIPE_STATION_STONEWORKING] = ITEM_ID_STONEWORKING_BENCH,
    [RECIPE_STATION_SMITHING] = ITEM_ID_SMITH_TABLE,
    [RECIPE_STATION_LEATHERWORKING] = ITEM_ID_LEATHER_WORKBENCH,
    [RECIPE_STATION_HANDS] = ITEM_ID_NONE,
};
static const uint16_t IS_PROCESS_ITEM[RECIPE_PROCESS_COUNT] = {
    [RECIPE_PROCESS_FURNACE] = ITEM_ID_FURNACE,
    [RECIPE_PROCESS_BLAST_FURNACE] = ITEM_ID_BLAST_FURNACE,
    [RECIPE_PROCESS_TANNING_RACK] = ITEM_ID_TANNING_RACK,
};

/* What the crafting table may make: the stations, and nothing else. */
static const uint16_t IS_TABLE_MAKES[] = {ITEM_ID_WOODWORKING_BENCH, ITEM_ID_STONEWORKING_BENCH, ITEM_ID_SMITH_TABLE,
                                          ITEM_ID_TANNING_RACK, ITEM_ID_LEATHER_WORKBENCH};
enum : uint32_t { IS_TABLE_MAKES_COUNT = sizeof(IS_TABLE_MAKES) / sizeof(IS_TABLE_MAKES[0]) };

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

/* A recipe laid out on a grid of this side, in its top left corner. False if it does not fit. */
static bool is_lay(const recipe_def_t *recipe, uint32_t side, item_stack_t cells[RECIPE_CELLS]) {
    memset(cells, 0, sizeof(item_stack_t) * RECIPE_CELLS);
    if (recipe->kind == RECIPE_KIND_SHAPELESS) {
        if (recipe->cell_count > side * side) {
            return false;
        }
        for (uint32_t i = 0u; i < recipe->cell_count; i++) {
            cells[i] = item_stack_make(is_first_of(recipe->cells[i]), 1u);
        }
        return true;
    }
    if (recipe->width > side || recipe->height > side) {
        return false;
    }
    for (uint32_t y = 0u; y < recipe->height; y++) {
        for (uint32_t x = 0u; x < recipe->width; x++) {
            const uint16_t cell = recipe->cells[y * recipe->width + x];
            if (cell != RECIPE_CELL_EMPTY) {
                cells[y * side + x] = item_stack_make(is_first_of(cell), 1u);
            }
        }
    }
    return true;
}

static uint32_t is_side_of(uint32_t station) {
    return station == RECIPE_STATION_HANDS ? CRAFTING_SIDE_HANDS : CRAFTING_SIDE_TABLE;
}

/* Ahmed's words: "crafting table should only make the other benches". */
TEST(the_crafting_table_makes_only_the_other_stations_and_the_hands_make_the_table) {
    uint32_t table = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        if (recipe->station != RECIPE_STATION_TABLE) {
            continue;
        }
        bool station = false;
        for (uint32_t i = 0u; i < IS_TABLE_MAKES_COUNT; i++) {
            station = station || recipe->gives == IS_TABLE_MAKES[i];
        }
        if (!station) {
            CHECK_STR_EQ(recipe->name, "a station, as the table makes nothing else");
        }
        table++;
    }
    CHECK_EQ_U64(table, IS_TABLE_MAKES_COUNT);
    const uint32_t row = is_row_named(t, "crafting_table");
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].station, RECIPE_STATION_HANDS);
    item_stack_t cells[RECIPE_CELLS] = {0};
    for (uint32_t i = 0u; i < 4u; i++) {
        cells[i] = item_stack_make(i % 2u == 0u ? ITEM_ID_OAK_LOG : ITEM_ID_MANGROVE_LOG, 1u);
    }
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_HANDS), row);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
}

/* Each recipe works at its own station, and laid the same way at any other makes something else or nothing; and what any grid makes is
 * made at one station, so nothing is made in two places. */
TEST(each_recipe_is_made_at_its_own_station_only_and_nothing_in_two_places) {
    uint32_t per_station[RECIPE_STATION_COUNT] = {0};
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        const recipe_def_t *const recipe = &recipe_defs[row];
        REQUIRE(recipe->station < RECIPE_STATION_COUNT);
        per_station[recipe->station]++;
        item_stack_t cells[RECIPE_CELLS];
        REQUIRE(is_lay(recipe, is_side_of(recipe->station), cells));
        CHECK_EQ_U64(crafting_match(cells, is_side_of(recipe->station), recipe->station), row);
        for (uint32_t other = 0u; other < RECIPE_STATION_COUNT; other++) {
            if (other != recipe->station && is_lay(recipe, is_side_of(other), cells)) {
                CHECK(crafting_match(cells, is_side_of(other), other) != row);
            }
        }
        for (uint32_t row2 = 0u; row2 < RECIPE_COUNT; row2++) {
            if (recipe_defs[row2].gives == recipe->gives && recipe_defs[row2].station != recipe->station) {
                CHECK_STR_EQ(recipe_defs[row2].name, recipe->name);
            }
        }
    }
    for (uint32_t station = 0u; station < RECIPE_STATION_COUNT; station++) {
        CHECK(per_station[station] > 0u);
    }
    /* Each smelt is done by one process, and an input smelts one way. */
    for (uint32_t i = 0u; i < RECIPE_SMELT_COUNT; i++) {
        CHECK(recipe_smelts[i].process < RECIPE_PROCESS_COUNT);
    }
}

/* The walk: what the world gives is everything no recipe makes, logs and cobblestone among it, with ores, hides and the rest. What only a
 * block with a tier drops, as cobblestone from stone, comes once a tool that harvests that block has been made. The hands are there from
 * the start, and each other station once its item has been made. A smelt needs its block, and fuel if it burns. Round by round, whatever can
 * be made is, until nothing new comes. A recipe in a loop that nothing outside it feeds never comes. */
typedef struct is_walk_t {
    uint32_t have[ITEM_COUNT];                    /* 1 for what is had */
    uint32_t made[RECIPE_COUNT];                  /* the round each recipe was first made in, from 1; 0 for never */
    uint32_t smelted[RECIPE_SMELT_COUNT];         /* and each smelt */
    uint32_t alloyed[RECIPE_ALLOY_ROWS];          /* and each alloy, plan 005 phase 11 part B */
    uint32_t station_round[RECIPE_STATION_COUNT]; /* the round the station was first there, from 1; 0 for never */
    uint32_t process_round[RECIPE_PROCESS_COUNT];
    uint32_t rounds;
    uint32_t mined
        [ITEM_COUNT]; /* what the world gives, waiting for a tool: 1 if some block drops it, none of them to bare hands */
} is_walk_t;
static_assert(sizeof(is_walk_t) == 4u * (2u * (uint64_t)ITEM_COUNT + RECIPE_COUNT + RECIPE_SMELT_COUNT +
                                         RECIPE_ALLOY_ROWS + RECIPE_STATION_COUNT + RECIPE_PROCESS_COUNT + 1u));

/* Whether a block drops this item, and how it comes: by bare hands, or only by a tool of its own kind and tier. */
static bool is_block_drops(uint32_t block, uint16_t item) {
    const block_def_t *const def = &block_defs[block];
    for (uint32_t d = 0u; d < def->drop_count; d++) {
        if (block_drop_items[block_drops[def->first_drop + d].item].item == item) {
            return true;
        }
    }
    return false;
}

/* Whether what is had can bring this item out of the world: bare hands for a block of no tier, or else a tool that harvests the block. */
static bool is_minable(const is_walk_t *walk, uint16_t item) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        if (!is_block_drops(block, item)) {
            continue;
        }
        if (block_defs[block].tier == BLOCK_TIER_NONE) {
            return true;
        }
        for (uint32_t tool = 1u; tool < ITEM_COUNT; tool++) {
            if (walk->have[tool] != 0u && item_defs[tool].tool == block_defs[block].tool &&
                item_defs[tool].harvest_tier >= block_defs[block].tier) {
                return true;
            }
        }
    }
    return false;
}

/* Whether something a cell can be is had. */
static bool is_cell_had(const is_walk_t *walk, uint16_t cell) {
    const recipe_choice_t *const choice = &recipe_choices[cell - 1u];
    for (uint32_t c = 0u; c < choice->count; c++) {
        if (walk->have[recipe_choice_items[choice->first + c]] != 0u) {
            return true;
        }
    }
    return false;
}

/* without is a station whose block is never had, or RECIPE_STATION_COUNT for none. */
static void is_walk(is_walk_t *walk, uint32_t without) {
    memset(walk, 0, sizeof(*walk));
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        walk->have[item] = 1u;
    }
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        walk->have[recipe_defs[row].gives] = 0u;
    }
    for (uint32_t i = 0u; i < RECIPE_SMELT_COUNT; i++) {
        walk->have[recipe_smelts[i].gives] = 0u;
    }
    for (uint32_t i = 0u; i < RECIPE_ALLOY_COUNT; i++) {
        walk->have[recipe_alloys[i].gives] = 0u;
    }
    /* A tool or a piece of armour no recipe makes, as diamond's while they are off, is not lying about in the world either. */
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        if (walk->have[item] != 0u &&
            (item_defs[item].tool != BLOCK_TOOL_NONE || item_defs[item].armor_slot != ITEM_ARMOR_SLOT_NONE)) {
            walk->have[item] = 0u;
        }
    }
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        if (walk->have[item] != 0u && !is_minable(walk, (uint16_t)item)) {
            bool dropped = false;
            for (uint32_t block = 0u; block < BLOCK_COUNT && !dropped; block++) {
                dropped = is_block_drops(block, (uint16_t)item);
            }
            walk->mined[item] = dropped ? 1u : 0u;
            walk->have[item] = dropped ? 0u : 1u;
        }
    }
    for (bool more = true; more;) {
        more = false;
        walk->rounds++;
        for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
            if (walk->mined[item] != 0u && walk->have[item] == 0u && is_minable(walk, (uint16_t)item)) {
                walk->have[item] = 1u;
                more = true;
            }
        }
        for (uint32_t s = 0u; s < RECIPE_STATION_COUNT; s++) {
            if (walk->station_round[s] == 0u && s != without &&
                (s == RECIPE_STATION_HANDS || walk->have[IS_STATION_ITEM[s]] != 0u)) {
                walk->station_round[s] = walk->rounds;
            }
        }
        bool fuel = false;
        for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
            fuel = fuel || (walk->have[item] != 0u && recipe_fuels[item].ticks != 0u);
        }
        for (uint32_t p = 0u; p < RECIPE_PROCESS_COUNT; p++) {
            if (walk->process_round[p] == 0u && walk->have[IS_PROCESS_ITEM[p]] != 0u &&
                (fuel || recipe_process_burns[p] == 0u)) {
                walk->process_round[p] = walk->rounds;
            }
        }
        uint32_t next[ITEM_COUNT];
        memcpy(next, walk->have, sizeof(next));
        for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
            const recipe_def_t *const recipe = &recipe_defs[row];
            if (walk->made[row] != 0u || walk->station_round[recipe->station] == 0u) {
                continue;
            }
            bool all = true;
            for (uint32_t c = 0u; c < RECIPE_CELLS && all; c++) {
                all = recipe->cells[c] == RECIPE_CELL_EMPTY || is_cell_had(walk, recipe->cells[c]);
            }
            if (all) {
                walk->made[row] = walk->rounds;
                next[recipe->gives] = 1u;
                more = true;
            }
        }
        for (uint32_t i = 0u; i < RECIPE_SMELT_COUNT; i++) {
            if (walk->smelted[i] != 0u || walk->process_round[recipe_smelts[i].process] == 0u) {
                continue;
            }
            for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
                if (walk->have[item] != 0u && recipe_smelt_rows[item] == i + 1u) {
                    walk->smelted[i] = walk->rounds;
                    next[recipe_smelts[i].gives] = 1u;
                    more = true;
                    break;
                }
            }
        }
        /* An alloy at its station, with each input had, and its heat if it needs any. */
        for (uint32_t i = 0u; i < RECIPE_ALLOY_COUNT; i++) {
            const recipe_alloy_t *const alloy = &recipe_alloys[i];
            bool all = walk->alloyed[i] == 0u && walk->station_round[alloy->station] != 0u &&
                       (alloy->heat == RECIPE_HEAT_NONE || walk->process_round[alloy->heat] != 0u);
            for (uint32_t n = 0u; n < alloy->input_count && all; n++) {
                all = walk->have[alloy->inputs[n]] != 0u;
            }
            if (all) {
                walk->alloyed[i] = walk->rounds;
                next[alloy->gives] = 1u;
                more = true;
            }
        }
        memcpy(walk->have, next, sizeof(next));
    }
}

TEST(every_recipe_is_reached_from_what_the_world_gives_walking_the_stations_in_order) {
    is_walk_t *const walk = test_alloc(t, sizeof(is_walk_t));
    is_walk(walk, RECIPE_STATION_COUNT);
    /* Logs and cobblestone are what a person starts with: no recipe makes either. */
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        CHECK(recipe_defs[row].gives != ITEM_ID_OAK_LOG && recipe_defs[row].gives != ITEM_ID_COBBLESTONE);
    }
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        if (walk->made[row] == 0u) {
            CHECK_STR_EQ(recipe_defs[row].name, "reached");
        }
    }
    for (uint32_t i = 0u; i < RECIPE_SMELT_COUNT; i++) {
        if (walk->smelted[i] == 0u) {
            CHECK_STR_EQ(recipe_smelts[i].name, "reached");
        }
    }
    for (uint32_t i = 0u; i < RECIPE_ALLOY_COUNT; i++) {
        if (walk->alloyed[i] == 0u) {
            CHECK_STR_EQ(recipe_alloys[i].name, "reached");
        }
    }
    /* The order Ahmed took: hands, table, woodworking, then stone and the furnace, then the smith table from copper, and the blast furnace
     * for iron. The tanning rack and the leather workbench come from wood once there are sticks. */
    const uint32_t *const s = walk->station_round, *const p = walk->process_round;
    printf("  rounds: hands %u, table %u, woodworking %u, stoneworking %u, furnace %u, blast furnace %u, smithing %u, "
           "tanning %u, "
           "leatherworking %u; %u in all\n",
           s[RECIPE_STATION_HANDS], s[RECIPE_STATION_TABLE], s[RECIPE_STATION_WOODWORKING],
           s[RECIPE_STATION_STONEWORKING], p[RECIPE_PROCESS_FURNACE], p[RECIPE_PROCESS_BLAST_FURNACE],
           s[RECIPE_STATION_SMITHING], p[RECIPE_PROCESS_TANNING_RACK], s[RECIPE_STATION_LEATHERWORKING], walk->rounds);
    CHECK_EQ_U64(s[RECIPE_STATION_HANDS], 1u);
    CHECK(s[RECIPE_STATION_HANDS] < s[RECIPE_STATION_TABLE]);
    CHECK(s[RECIPE_STATION_TABLE] < s[RECIPE_STATION_WOODWORKING]);
    CHECK(s[RECIPE_STATION_WOODWORKING] < s[RECIPE_STATION_STONEWORKING]);
    CHECK(s[RECIPE_STATION_STONEWORKING] < p[RECIPE_PROCESS_FURNACE]);
    CHECK(p[RECIPE_PROCESS_FURNACE] < p[RECIPE_PROCESS_BLAST_FURNACE]);
    CHECK(p[RECIPE_PROCESS_FURNACE] < s[RECIPE_STATION_SMITHING]);
    /* Plan 005 phase 11: the smith table is made from copper, so copper and gold tools come first, as phase 6's proposal said, then iron
     * once the blast furnace gives it, then stainless steel from iron and chromium. */
    const uint32_t copper = walk->made[is_row_named(t, "copper_pickaxe")];
    const uint32_t gold = walk->made[is_row_named(t, "gold_pickaxe")];
    const uint32_t iron = walk->made[is_row_named(t, "iron_pickaxe")];
    const uint32_t steel = walk->made[is_row_named(t, "stainless_steel_pickaxe")];
    printf("  pickaxes: copper round %u, gold %u, iron %u, stainless steel %u\n", copper, gold, iron, steel);
    CHECK(copper != 0u && copper < iron);
    CHECK(gold != 0u && gold < iron);
    CHECK(iron != 0u && iron < steel);
    CHECK_EQ_U64(recipe_choice_items[recipe_choices[recipe_defs[is_row_named(t, "smith_table")].cells[0] - 1u].first],
                 ITEM_ID_COPPER_INGOT);
    CHECK(s[RECIPE_STATION_WOODWORKING] < p[RECIPE_PROCESS_TANNING_RACK]);
    CHECK(s[RECIPE_STATION_WOODWORKING] < s[RECIPE_STATION_LEATHERWORKING]);
}

/* Ahmed's words: "no armour can be created without leather workbench!". So with its block never had, no armour is reached; with it, all
 * of it that can be made at all is. */
TEST(no_armour_is_reached_without_the_leather_workbench) {
    is_walk_t *const walk = test_alloc(t, sizeof(is_walk_t));
    is_walk(walk, RECIPE_STATION_LEATHERWORKING);
    uint32_t armour = 0u;
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        if (item_defs[recipe_defs[row].gives].armor_slot != ITEM_ARMOR_SLOT_NONE) {
            armour++;
            if (walk->made[row] != 0u) {
                CHECK_STR_EQ(recipe_defs[row].name, "not reached without a leather workbench");
            }
        }
    }
    /* Leather, copper, iron, gold and stainless steel, four pieces each. */
    CHECK_EQ_U64(armour, 20u);
    is_walk(walk, RECIPE_STATION_COUNT);
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        if (item_defs[recipe_defs[row].gives].armor_slot != ITEM_ARMOR_SLOT_NONE) {
            CHECK(walk->made[row] != 0u);
        }
    }
    /* And no armour piece is had from the world but by its recipe. */
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        if (item_defs[item].armor_slot == ITEM_ARMOR_SLOT_NONE || strncmp(item_defs[item].name, "diamond_", 8u) == 0) {
            continue;
        }
        bool made = false;
        for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
            made = made || recipe_defs[row].gives == item;
        }
        CHECK(made);
    }
}

/* Leather armour and metal on the smith table make that metal's piece: Minecraft's ingots for the piece around the leather one. */
TEST(metal_armour_is_leather_armour_with_its_metal_at_the_smith_table) {
    item_stack_t cells[RECIPE_CELLS] = {0};
    for (uint32_t i = 0u; i < RECIPE_CELLS; i++) {
        cells[i] = item_stack_make(ITEM_ID_IRON_INGOT, 1u);
    }
    cells[1] = item_stack_make(ITEM_ID_LEATHER_CHESTPLATE, 1u);
    uint32_t row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_SMITHING);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_IRON_CHESTPLATE);
    CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
    /* The ingots alone, as Minecraft has it, make nothing anywhere. */
    cells[1] = (item_stack_t){0};
    for (uint32_t station = 0u; station < RECIPE_STATION_COUNT; station++) {
        if (station != RECIPE_STATION_HANDS) {
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, station), CRAFTING_NO_RECIPE);
        }
    }
    /* Boots: four of copper around the leather boots. */
    memset(cells, 0, sizeof(cells));
    cells[0] = cells[2] = cells[3] = cells[5] = item_stack_make(ITEM_ID_COPPER_INGOT, 1u);
    cells[1] = item_stack_make(ITEM_ID_LEATHER_BOOTS, 1u);
    row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_SMITHING);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_COPPER_BOOTS);
    /* Leather armour itself is the leather workbench's. */
    const uint32_t leather = is_row_named(t, "leather_helmet");
    REQUIRE(leather != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[leather].station, RECIPE_STATION_LEATHERWORKING);
}

/* Ahmed's words: "can't convert logs to planks also! planks to sticks!" and "I can not create a stick from 2 logs on the wood bench????". */
TEST(logs_to_planks_planks_to_sticks_and_two_logs_to_sticks_on_the_woodworking_bench_only) {
    item_stack_t cells[RECIPE_CELLS] = {0};
    cells[4] = item_stack_make(ITEM_ID_BIRCH_LOG, 1u);
    uint32_t row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_BIRCH_PLANKS);
    CHECK_EQ_U64(recipe_defs[row].count, 4u);
    cells[7] = item_stack_make(ITEM_ID_OAK_LOG, 1u);
    row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_STICK);
    CHECK_EQ_U64(recipe_defs[row].count, 16u);
    cells[4] = cells[7] = item_stack_make(ITEM_ID_SPRUCE_PLANKS, 1u);
    row = crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    REQUIRE(row != CRAFTING_NO_RECIPE);
    CHECK_EQ_U64(recipe_defs[row].gives, ITEM_ID_STICK);
    CHECK_EQ_U64(recipe_defs[row].count, 4u);
    for (uint32_t station = 0u; station < RECIPE_STATION_COUNT; station++) {
        if (station != RECIPE_STATION_WOODWORKING && station != RECIPE_STATION_HANDS) {
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, station), CRAFTING_NO_RECIPE);
        }
    }
}

TEST(from_logs_to_a_bench_and_on_it_a_pickaxe_as_a_player_would) {
    crafting_grid_t hands, table, bench;
    crafting_grid_start(&hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_HANDS);
    crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    crafting_grid_start(&bench, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);

    /* Hands: four logs to a table. A log alone is no planks there. */
    item_stack_t cursor = item_stack_make(ITEM_ID_OAK_LOG, 4u);
    crafting_grid_click_secondary(&hands, 0u, &cursor);
    CHECK(item_stack_is_empty(&hands.output));
    for (uint32_t i = 1u; i < 4u; i++) {
        crafting_grid_click_secondary(&hands, i, &cursor);
    }
    CHECK_EQ_U64(hands.output.item, ITEM_ID_CRAFTING_TABLE);
    item_stack_t made = {0};
    CHECK_EQ_U64(crafting_grid_take(&hands, &made), 1u);
    CHECK_EQ_U64(made.item, ITEM_ID_CRAFTING_TABLE);

    /* Table: three logs in a row to a woodworking bench. */
    cursor = item_stack_make(ITEM_ID_OAK_LOG, 3u);
    for (uint32_t i = 3u; i < 6u; i++) {
        crafting_grid_click_secondary(&table, i, &cursor);
    }
    CHECK_EQ_U64(table.output.item, ITEM_ID_WOODWORKING_BENCH);
    made = (item_stack_t){0};
    CHECK_EQ_U64(crafting_grid_take(&table, &made), 1u);
    CHECK_EQ_U64(made.item, ITEM_ID_WOODWORKING_BENCH);

    /* Bench: a log to planks, two planks to sticks, then the pickaxe, which the same cells do not make on the table. */
    cursor = item_stack_make(ITEM_ID_OAK_LOG, 1u);
    crafting_grid_click_secondary(&bench, 0u, &cursor);
    CHECK_EQ_U64(bench.output.item, ITEM_ID_OAK_PLANKS);
    made = (item_stack_t){0};
    CHECK_EQ_U64(crafting_grid_take(&bench, &made), 4u);
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
}

/* Each station's item places its first part, and every part drops the one item, so breaking any gives one back. */
TEST(each_station_item_places_its_first_part_and_every_part_drops_one) {
    typedef struct is_station_t {
        uint16_t item;     /* 0 */
        uint16_t parts[4]; /* 2: its block, the first part first */
        uint8_t count;     /* 10 */
        uint8_t _pad0[1];  /* 11 */
    } is_station_t;
    static_assert(sizeof(is_station_t) == 12);
    static const is_station_t stations[] = {
        {ITEM_ID_WOODWORKING_BENCH, {BLOCK_ID_WOODWORKING_BENCH_WEST, BLOCK_ID_WOODWORKING_BENCH_EAST}, 2u, {0}},
        {ITEM_ID_STONEWORKING_BENCH, {BLOCK_ID_STONEWORKING_BENCH_LEFT, BLOCK_ID_STONEWORKING_BENCH_RIGHT}, 2u, {0}},
        {ITEM_ID_TANNING_RACK, {BLOCK_ID_TANNING_RACK_LEFT, BLOCK_ID_TANNING_RACK_RIGHT}, 2u, {0}},
        {ITEM_ID_LEATHER_WORKBENCH, {BLOCK_ID_LEATHER_WORKBENCH_LEFT, BLOCK_ID_LEATHER_WORKBENCH_RIGHT}, 2u, {0}},
        {ITEM_ID_SMITH_TABLE,
         {BLOCK_ID_SMITH_TABLE_FRONT_LEFT, BLOCK_ID_SMITH_TABLE_FRONT_RIGHT, BLOCK_ID_SMITH_TABLE_BACK_LEFT,
          BLOCK_ID_SMITH_TABLE_BACK_RIGHT},
         4u,
         {0}},
        {ITEM_ID_BLAST_FURNACE, {BLOCK_ID_BLAST_FURNACE, BLOCK_ID_BLAST_FURNACE_LIT}, 2u, {0}},
    };
    for (uint32_t s = 0u; s < sizeof(stations) / sizeof(stations[0]); s++) {
        CHECK_EQ_U64(item_defs[stations[s].item].block, stations[s].parts[0]);
        for (uint32_t p = 0u; p < stations[s].count; p++) {
            const block_def_t *const def = &block_defs[stations[s].parts[p]];
            CHECK_EQ_U64(def->drop_count, 1u);
            CHECK_EQ_U64(block_drop_items[block_drops[def->first_drop].item].item, stations[s].item);
            CHECK_EQ_U64(block_drops[def->first_drop].count_min, 1u);
            CHECK_EQ_U64(block_drops[def->first_drop].count_max, 1u);
            CHECK_EQ_U64(def->shape, BLOCK_SHAPE_CUBE);
        }
    }
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_WOODWORKING_BENCH), smelting_burn_ticks(ITEM_ID_CRAFTING_TABLE));
    CHECK_EQ_U64(smelting_burn_ticks(ITEM_ID_TANNING_RACK), smelting_burn_ticks(ITEM_ID_CRAFTING_TABLE));
}

/* A furnace by its block's process: the rack tans hides with no fuel, the furnace will not tan, and iron is the blast furnace's alone. */
TEST(hides_tan_on_the_rack_with_no_fuel_and_iron_smelts_only_in_the_blast_furnace) {
    uint8_t *const raw = test_alloc(t, sizeof(arena_t) + 64u);
    arena_t *const arena = (arena_t *)(void *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(arena, "is_furnaces", test_alloc(t, SMELTING_BYTES), SMELTING_BYTES);
    smelting_store_t store;
    smelting_start(&store, arena);
    smelting_furnace_t *const rack = smelting_open_process(&store, 0, 0, 0, RECIPE_PROCESS_TANNING_RACK);
    smelting_furnace_t *const furnace = smelting_open_process(&store, 1, 0, 0, RECIPE_PROCESS_FURNACE);
    smelting_furnace_t *const blast = smelting_open_process(&store, 2, 0, 0, RECIPE_PROCESS_BLAST_FURNACE);
    REQUIRE(rack != nullptr && furnace != nullptr && blast != nullptr);
    CHECK(!smelting_burns(rack));
    rack->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_COW_HIDE, 2u);
    item_stack_t coal = item_stack_make(ITEM_ID_COAL, 1u);
    smelting_click(rack, SMELTING_SLOT_FUEL, &coal, false);
    CHECK_EQ_U64(coal.count, 1u); /* the rack takes no fuel */
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_PIG_HIDE, 1u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    for (uint32_t tick = 0u; tick < 599u; tick++) {
        CHECK(!smelting_tick(&store, rack));
        (void)smelting_tick(&store, furnace);
    }
    CHECK(item_stack_is_empty(&rack->slots[SMELTING_SLOT_OUTPUT]));
    CHECK(!smelting_tick(&store, rack));
    CHECK_EQ_U64(rack->slots[SMELTING_SLOT_OUTPUT].item, ITEM_ID_LEATHER);
    CHECK_EQ_U64(rack->slots[SMELTING_SLOT_OUTPUT].count, 1u);
    CHECK_EQ_U64(rack->slots[SMELTING_SLOT_INPUT].count, 1u);
    /* The furnace never lit for a hide, and still has its coal. */
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].count, 1u);

    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 1u);
    blast->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 1u);
    blast->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 1u);
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        (void)smelting_tick(&store, furnace);
        (void)smelting_tick(&store, blast);
    }
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_OUTPUT]));
    CHECK_EQ_U64(blast->slots[SMELTING_SLOT_OUTPUT].item, ITEM_ID_IRON_INGOT);
    /* Copper and gold are the furnace's. */
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_COPPER, 1u);
    for (uint32_t tick = 0u; tick < 200u; tick++) {
        (void)smelting_tick(&store, furnace);
    }
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_OUTPUT].item, ITEM_ID_COPPER_INGOT);
    CHECK_EQ_U64(smelting_recipe_of(ITEM_ID_RAW_GOLD)->process, RECIPE_PROCESS_FURNACE);
    CHECK_EQ_U64(smelting_recipe_of(ITEM_ID_RAW_CHROMIUM)->process, RECIPE_PROCESS_BLAST_FURNACE);
}

TEST_MAIN()
