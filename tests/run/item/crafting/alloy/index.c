/* ctest run_item_crafting_alloy. See README.md. */
#include "harness/index.h"

#include "item/crafting/index.h"
#include "item/smelting/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/recipes/index.h"

#include <stdint.h>
#include <string.h>

/* The tools and armour of stainless steel, and iron's of the same kind, written here by hand so that the files are what is tested. */
typedef struct ia_pair_t {
    uint16_t steel;   /* 0 */
    uint16_t iron;    /* 2 */
    uint16_t leather; /* 4: the leather piece it is riveted on, or ITEM_ID_NONE for a tool */
    uint8_t _pad0[2]; /* 6 */
} ia_pair_t;
static_assert(sizeof(ia_pair_t) == 8);

static const ia_pair_t IA_PAIRS[] = {
    {ITEM_ID_STAINLESS_STEEL_PICKAXE, ITEM_ID_IRON_PICKAXE, ITEM_ID_NONE, {0}},
    {ITEM_ID_STAINLESS_STEEL_SWORD, ITEM_ID_IRON_SWORD, ITEM_ID_NONE, {0}},
    {ITEM_ID_STAINLESS_STEEL_AXE, ITEM_ID_IRON_AXE, ITEM_ID_NONE, {0}},
    {ITEM_ID_STAINLESS_STEEL_SHOVEL, ITEM_ID_IRON_SHOVEL, ITEM_ID_NONE, {0}},
    {ITEM_ID_STAINLESS_STEEL_HOE, ITEM_ID_IRON_HOE, ITEM_ID_NONE, {0}},
    {ITEM_ID_STAINLESS_STEEL_HELMET, ITEM_ID_IRON_HELMET, ITEM_ID_LEATHER_HELMET, {0}},
    {ITEM_ID_STAINLESS_STEEL_CHESTPLATE, ITEM_ID_IRON_CHESTPLATE, ITEM_ID_LEATHER_CHESTPLATE, {0}},
    {ITEM_ID_STAINLESS_STEEL_LEGGINGS, ITEM_ID_IRON_LEGGINGS, ITEM_ID_LEATHER_LEGGINGS, {0}},
    {ITEM_ID_STAINLESS_STEEL_BOOTS, ITEM_ID_IRON_BOOTS, ITEM_ID_LEATHER_BOOTS, {0}},
};
enum : uint32_t { IA_PAIR_COUNT = sizeof(IA_PAIRS) / sizeof(IA_PAIRS[0]) };

/* What two inputs make at the smith table with nothing burning by it, and no item if nothing. */
static uint16_t ia_makes(uint16_t a, uint32_t a_count, uint16_t b, uint32_t b_count) {
    const item_stack_t inputs[CRAFTING_ALLOY_INPUTS] = {
        a_count == 0u ? (item_stack_t){0} : item_stack_make(a, a_count),
        b_count == 0u ? (item_stack_t){0} : item_stack_make(b, b_count),
    };
    const uint32_t row = crafting_alloy_match(inputs, RECIPE_STATION_SMITHING, 0u);
    return row == CRAFTING_NO_RECIPE ? ITEM_ID_NONE : recipe_alloys[row].gives;
}

/* Ahmed's words: "stainless steel ... that is 9xiron and 1xchromium". Ten in, ten out, in either slot, and only what the recipe takes. */
TEST(nine_iron_and_one_chromium_make_ten_stainless_steel_in_either_slot) {
    crafting_alloy_t alloy;
    crafting_alloy_start(&alloy, RECIPE_STATION_SMITHING);
    item_stack_t cursor = item_stack_make(ITEM_ID_IRON_INGOT, 9u);
    crafting_alloy_click(&alloy, 0u, &cursor, false);
    CHECK(item_stack_is_empty(&alloy.slots[CRAFTING_ALLOY_OUTPUT]));
    cursor = item_stack_make(ITEM_ID_CHROMIUM_INGOT, 1u);
    crafting_alloy_click(&alloy, 1u, &cursor, false);
    CHECK_EQ_U64(alloy.slots[CRAFTING_ALLOY_OUTPUT].item, ITEM_ID_STAINLESS_STEEL_INGOT);
    CHECK_EQ_U64(alloy.slots[CRAFTING_ALLOY_OUTPUT].count, 10u);
    item_stack_t made = {0};
    crafting_alloy_click(&alloy, CRAFTING_ALLOY_OUTPUT, &made, false);
    CHECK_EQ_U64(made.item, ITEM_ID_STAINLESS_STEEL_INGOT);
    CHECK_EQ_U64(made.count, 10u);
    CHECK_EQ_U64(made.wear, 0u);
    CHECK(item_stack_is_empty(&alloy.slots[0]));
    CHECK(item_stack_is_empty(&alloy.slots[1]));
    CHECK(item_stack_is_empty(&alloy.slots[CRAFTING_ALLOY_OUTPUT]));
    CHECK_EQ_U64(alloy.alloyed_count, 10u);
    /* The other way round, with more than one making's worth: what is over stays. */
    alloy.slots[0] = item_stack_make(ITEM_ID_CHROMIUM_INGOT, 3u);
    alloy.slots[1] = item_stack_make(ITEM_ID_IRON_INGOT, 20u);
    crafting_alloy_changed(&alloy);
    CHECK_EQ_U64(alloy.slots[CRAFTING_ALLOY_OUTPUT].count, 10u);
    /* A pointer that cannot take all of a making takes none. */
    cursor = item_stack_make(ITEM_ID_STAINLESS_STEEL_INGOT, 60u);
    CHECK_EQ_U64(crafting_alloy_take(&alloy, &cursor), 0u);
    CHECK_EQ_U64(alloy.slots[1].count, 20u);
    cursor = (item_stack_t){0};
    CHECK_EQ_U64(crafting_alloy_take(&alloy, &cursor), 10u);
    CHECK_EQ_U64(alloy.slots[0].count, 2u);
    CHECK_EQ_U64(alloy.slots[1].count, 11u);
    CHECK_EQ_U64(crafting_alloy_take(&alloy, &cursor), 10u);
    CHECK_EQ_U64(cursor.count, 20u);
    /* Two iron and a chromium are left: no more. */
    CHECK_EQ_U64(alloy.slots[0].count, 1u);
    CHECK_EQ_U64(alloy.slots[1].count, 2u);
    CHECK(item_stack_is_empty(&alloy.slots[CRAFTING_ALLOY_OUTPUT]));
    CHECK_EQ_U64(crafting_alloy_take(&alloy, &cursor), 0u);
}

/* A shift-click on the output makes as many as the inputs have for, into the inventory, and closing gives the rest back. */
TEST(a_shift_click_makes_all_it_can_and_closing_gives_back_what_is_left) {
    uint8_t *const raw = test_alloc(t, sizeof(arena_t) + 64u);
    arena_t *const arena = (arena_t *)(void *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(arena, "ia_inventory", test_alloc(t, INVENTORY_BYTES), INVENTORY_BYTES);
    inventory_t inventory;
    inventory_start(&inventory, arena);
    crafting_alloy_t alloy;
    crafting_alloy_start(&alloy, RECIPE_STATION_SMITHING);
    alloy.slots[0] = item_stack_make(ITEM_ID_IRON_INGOT, 64u);
    alloy.slots[1] = item_stack_make(ITEM_ID_CHROMIUM_INGOT, 5u);
    crafting_alloy_changed(&alloy);
    CHECK_EQ_U64(crafting_alloy_take_all(&alloy, &inventory), 50u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_STAINLESS_STEEL_INGOT), 50u);
    CHECK_EQ_U64(alloy.slots[0].count, 64u - 45u);
    CHECK(item_stack_is_empty(&alloy.slots[1]));
    CHECK_EQ_U64(crafting_alloy_give_back(&alloy, &inventory), 0u);
    CHECK_EQ_U64(inventory_count(&inventory, ITEM_ID_IRON_INGOT), 19u);
    CHECK(item_stack_is_empty(&alloy.slots[0]));
}

/* "A stack of 8 iron makes nothing", and nothing but 9 iron and 1 chromium makes stainless steel: every pair of items, each at the most a
 * slot holds, and every item alone, at the smith table. */
TEST(eight_iron_and_one_chromium_make_nothing_and_nothing_else_makes_stainless_steel) {
    CHECK_EQ_U64(ia_makes(ITEM_ID_IRON_INGOT, 8u, ITEM_ID_CHROMIUM_INGOT, 1u), ITEM_ID_NONE);
    CHECK_EQ_U64(ia_makes(ITEM_ID_CHROMIUM_INGOT, 1u, ITEM_ID_IRON_INGOT, 8u), ITEM_ID_NONE);
    CHECK_EQ_U64(ia_makes(ITEM_ID_IRON_INGOT, 9u, ITEM_ID_CHROMIUM_INGOT, 1u), ITEM_ID_STAINLESS_STEEL_INGOT);
    CHECK_EQ_U64(ia_makes(ITEM_ID_CHROMIUM_INGOT, 1u, ITEM_ID_IRON_INGOT, 9u), ITEM_ID_STAINLESS_STEEL_INGOT);
    CHECK_EQ_U64(ia_makes(ITEM_ID_IRON_INGOT, 64u, ITEM_ID_NONE, 0u), ITEM_ID_NONE);
    CHECK_EQ_U64(ia_makes(ITEM_ID_NONE, 0u, ITEM_ID_CHROMIUM_INGOT, 64u), ITEM_ID_NONE);
    CHECK_EQ_U64(ia_makes(ITEM_ID_CHROMIUM_INGOT, 9u, ITEM_ID_IRON_INGOT, 1u), ITEM_ID_NONE);
    uint32_t stainless = 0u;
    for (uint32_t a = 1u; a < ITEM_COUNT; a++) {
        for (uint32_t b = 0u; b < ITEM_COUNT; b++) {
            const uint32_t b_count = b == 0u ? 0u : item_defs[b].max_stack;
            if (ia_makes((uint16_t)a, item_defs[a].max_stack, (uint16_t)b, b_count) == ITEM_ID_STAINLESS_STEEL_INGOT) {
                stainless++;
                CHECK((a == ITEM_ID_IRON_INGOT && b == ITEM_ID_CHROMIUM_INGOT) ||
                      (a == ITEM_ID_CHROMIUM_INGOT && b == ITEM_ID_IRON_INGOT));
            }
        }
    }
    CHECK_EQ_U64(stainless, 2u);
    /* Nor anywhere but the smith table, nor by any grid or furnace. */
    const item_stack_t inputs[CRAFTING_ALLOY_INPUTS] = {item_stack_make(ITEM_ID_IRON_INGOT, 9u),
                                                        item_stack_make(ITEM_ID_CHROMIUM_INGOT, 1u)};
    for (uint32_t station = 0u; station < RECIPE_STATION_COUNT; station++) {
        CHECK_EQ_U64(crafting_alloy_match(inputs, station, 0xFFu) != CRAFTING_NO_RECIPE,
                     station == RECIPE_STATION_SMITHING);
    }
    for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
        CHECK(recipe_defs[row].gives != ITEM_ID_STAINLESS_STEEL_INGOT);
    }
    for (uint32_t i = 0u; i < RECIPE_SMELT_COUNT; i++) {
        CHECK(recipe_smelts[i].gives != ITEM_ID_STAINLESS_STEEL_INGOT);
    }
}

/* The choice taken: alloying is at the smith table with no lit blast furnace by it. An alloy that names heat needs a block of that process
 * burning within reach, which smelting_lit_near finds. */
TEST(stainless_steel_needs_no_heat_and_heat_is_a_lit_block_within_reach) {
    REQUIRE(RECIPE_ALLOY_COUNT >= 1u);
    for (uint32_t row = 0u; row < RECIPE_ALLOY_COUNT; row++) {
        if (recipe_alloys[row].gives == ITEM_ID_STAINLESS_STEEL_INGOT) {
            CHECK_EQ_U64(recipe_alloys[row].heat, RECIPE_HEAT_NONE);
            CHECK_EQ_U64(recipe_alloys[row].station, RECIPE_STATION_SMITHING);
            CHECK_EQ_U64(recipe_alloys[row].count, 10u);
        }
    }
    uint8_t *const raw = test_alloc(t, sizeof(arena_t) + 64u);
    arena_t *const arena = (arena_t *)(void *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(arena, "ia_furnaces", test_alloc(t, SMELTING_BYTES), SMELTING_BYTES);
    smelting_store_t store;
    smelting_start(&store, arena);
    const int32_t table[3] = {10, 10, 5};
    smelting_furnace_t *const blast = smelting_open_process(&store, 14, 6, 5, RECIPE_PROCESS_BLAST_FURNACE);
    smelting_furnace_t *const far = smelting_open_process(&store, 15, 10, 5, RECIPE_PROCESS_FURNACE);
    REQUIRE(blast != nullptr && far != nullptr);
    const int32_t reach = (int32_t)CRAFTING_ALLOY_HEAT_REACH;
    CHECK_EQ_U64(smelting_lit_near(&store, table, reach), 0u);
    blast->burn_ticks = 10u;
    blast->burn_total = 1600u;
    far->burn_ticks = 10u;
    far->burn_total = 1600u;
    CHECK_EQ_U64(smelting_lit_near(&store, table, reach), 1u << RECIPE_PROCESS_BLAST_FURNACE);
    CHECK_EQ_U64(smelting_lit_near(&store, table, reach + 1),
                 (1u << RECIPE_PROCESS_BLAST_FURNACE) | (1u << RECIPE_PROCESS_FURNACE));
    /* With heat or without, stainless steel is made the same. */
    crafting_alloy_t alloy;
    crafting_alloy_start(&alloy, RECIPE_STATION_SMITHING);
    alloy.slots[0] = item_stack_make(ITEM_ID_IRON_INGOT, 9u);
    alloy.slots[1] = item_stack_make(ITEM_ID_CHROMIUM_INGOT, 1u);
    crafting_alloy_changed(&alloy);
    CHECK_EQ_U64(alloy.slots[CRAFTING_ALLOY_OUTPUT].count, 10u);
    crafting_alloy_heat_set(&alloy, 1u << RECIPE_PROCESS_BLAST_FURNACE);
    CHECK_EQ_U64(alloy.slots[CRAFTING_ALLOY_OUTPUT].count, 10u);
}

/* Each tool and piece of stainless steel is iron's pattern in stainless steel at the smith table, and nowhere else. */
TEST(stainless_steel_tools_and_armour_are_irons_patterns_at_the_smith_table) {
    for (uint32_t p = 0u; p < IA_PAIR_COUNT; p++) {
        uint32_t steel_row = CRAFTING_NO_RECIPE, iron_row = CRAFTING_NO_RECIPE;
        for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
            steel_row = recipe_defs[row].gives == IA_PAIRS[p].steel ? row : steel_row;
            iron_row = recipe_defs[row].gives == IA_PAIRS[p].iron ? row : iron_row;
        }
        REQUIRE(steel_row != CRAFTING_NO_RECIPE && iron_row != CRAFTING_NO_RECIPE);
        const recipe_def_t *const steel = &recipe_defs[steel_row], *const iron = &recipe_defs[iron_row];
        CHECK_EQ_U64(steel->station, RECIPE_STATION_SMITHING);
        CHECK_EQ_U64(steel->width, iron->width);
        CHECK_EQ_U64(steel->height, iron->height);
        /* Lay iron's pattern with stainless ingots where iron's are. */
        item_stack_t cells[RECIPE_CELLS] = {0};
        uint32_t ingots = 0u;
        for (uint32_t y = 0u; y < iron->height; y++) {
            for (uint32_t x = 0u; x < iron->width; x++) {
                const uint16_t cell = iron->cells[y * iron->width + x];
                if (cell == RECIPE_CELL_EMPTY) {
                    continue;
                }
                const uint16_t first = recipe_choice_items[recipe_choices[cell - 1u].first];
                const uint16_t item = first == ITEM_ID_IRON_INGOT ? ITEM_ID_STAINLESS_STEEL_INGOT : first;
                ingots += first == ITEM_ID_IRON_INGOT ? 1u : 0u;
                cells[y * CRAFTING_SIDE_TABLE + x] = item_stack_make(item, 1u);
                if (IA_PAIRS[p].leather != ITEM_ID_NONE && first != ITEM_ID_IRON_INGOT) {
                    CHECK_EQ_U64(first, IA_PAIRS[p].leather);
                }
            }
        }
        CHECK(ingots > 0u);
        CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_SMITHING), steel_row);
        for (uint32_t station = 0u; station < RECIPE_STATION_COUNT; station++) {
            if (station != RECIPE_STATION_SMITHING && station != RECIPE_STATION_HANDS) {
                CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, station), CRAFTING_NO_RECIPE);
            }
        }
    }
}

/* A tier above iron: it harvests what iron does, works faster, lasts longer, and its armour guards more. */
TEST(stainless_steel_is_a_tier_above_iron) {
    const item_tier_def_t *const steel = &item_tiers[ITEM_TIER_STAINLESS_STEEL];
    const item_tier_def_t *const iron = &item_tiers[ITEM_TIER_IRON];
    CHECK_STR_EQ(steel->name, "stainless_steel");
    CHECK(steel->harvest_tier >= iron->harvest_tier);
    CHECK(steel->speed_milli > iron->speed_milli);
    uint32_t steel_points = 0u, iron_points = 0u;
    for (uint32_t p = 0u; p < IA_PAIR_COUNT; p++) {
        const item_def_t *const s = &item_defs[IA_PAIRS[p].steel], *const i = &item_defs[IA_PAIRS[p].iron];
        CHECK(s->durability > i->durability);
        CHECK_EQ_U64(s->tool, i->tool);
        CHECK_EQ_U64(s->armor_slot, i->armor_slot);
        CHECK(s->armor_points >= i->armor_points);
        CHECK(s->armor_toughness > i->armor_toughness || s->armor_slot == ITEM_ARMOR_SLOT_NONE);
        CHECK(s->damage >= i->damage);
        if (s->tool != BLOCK_TOOL_NONE) {
            CHECK_EQ_U64(s->tier, ITEM_TIER_STAINLESS_STEEL);
            CHECK_EQ_U64(s->harvest_tier, i->harvest_tier);
        }
        steel_points += s->armor_points;
        iron_points += i->armor_points;
    }
    CHECK(steel_points > iron_points);
}

TEST_MAIN()
