/* ctest run_item_crafting_copper. See README.md. */
#include "harness/index.h"

#include "item/index.h"
#include "tables/items/index.h"
#include "tables/recipes/index.h"

#include <string.h>

enum : uint32_t {
    CC_PIECES = 4u,
    CC_ROWS_MAX = 3u,
    /* 5 + 8 + 7 + 4. */
    CC_INGOTS_FOR_A_SET = 24u,
};

/* A pattern, written here by hand and not read from the tables, so that the tables are what is being tested. */
typedef struct cc_piece_t {
    const char *recipe;            /* 0 */
    const char *rows[CC_ROWS_MAX]; /* 8: 'M' is an ingot; a null row is past the bottom */
    uint32_t height;               /* 32 */
    uint32_t ingots;               /* 36 */
    uint32_t slot;                 /* 40 */
    uint16_t item;                 /* 44 */
    uint16_t iron;                 /* 46: the same shape in iron */
} cc_piece_t;
static_assert(sizeof(cc_piece_t) == 48);

static const cc_piece_t CC_PIECE_ROWS[CC_PIECES] = {
    {"copper_helmet", {"MMM", "M M", nullptr}, 2u, 5u, 0u, ITEM_ID_COPPER_HELMET, ITEM_ID_IRON_HELMET},
    {"copper_chestplate", {"M M", "MMM", "MMM"}, 3u, 8u, 1u, ITEM_ID_COPPER_CHESTPLATE, ITEM_ID_IRON_CHESTPLATE},
    {"copper_leggings", {"MMM", "M M", "M M"}, 3u, 7u, 2u, ITEM_ID_COPPER_LEGGINGS, ITEM_ID_IRON_LEGGINGS},
    {"copper_boots", {"M M", "M M", nullptr}, 2u, 4u, 3u, ITEM_ID_COPPER_BOOTS, ITEM_ID_IRON_BOOTS},
};

/* Every cell of the pattern, one ingot at a time off the pointer, as the right button does, from this row of the table. */
static void cc_lay(crafting_grid_t *table, const cc_piece_t *piece, uint32_t top, item_stack_t *cursor) {
    for (uint32_t y = 0u; y < piece->height; y++) {
        for (uint32_t x = 0u; x < CRAFTING_SIDE_TABLE; x++) {
            if (piece->rows[y][x] == 'M') {
                crafting_grid_click_secondary(table, (top + y) * CRAFTING_SIDE_TABLE + x, cursor);
            }
        }
    }
}

/* The n-th ingot of a pattern, counting from the top left, as a cell of the table. */
static uint32_t cc_ingot_cell(const cc_piece_t *piece, uint32_t n) {
    uint32_t seen = 0u;
    for (uint32_t y = 0u; y < piece->height; y++) {
        for (uint32_t x = 0u; x < CRAFTING_SIDE_TABLE; x++) {
            if (piece->rows[y][x] == 'M' && seen++ == n) {
                return y * CRAFTING_SIDE_TABLE + x;
            }
        }
    }
    return RECIPE_CELLS;
}

/* What these cells make, and no item if they make nothing: a row is only there to read if there is a row. */
static uint32_t cc_makes(const item_stack_t *cells, uint32_t side) {
    const uint32_t recipe = crafting_match(cells, side, RECIPE_STATION_TABLE);
    return recipe == CRAFTING_NO_RECIPE ? ITEM_ID_NONE : recipe_defs[recipe].gives;
}

TEST(smelting_copper_gives_the_ingot_that_all_of_this_is_made_from) {
    static const uint16_t sources[] = {ITEM_ID_RAW_COPPER, ITEM_ID_COPPER_ORE, ITEM_ID_DEEPSLATE_COPPER_ORE};
    for (uint32_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); i++) {
        const recipe_smelt_t *const smelt = smelting_recipe_of(sources[i]);
        REQUIRE(smelt != nullptr);
        CHECK_EQ_U64(smelt->gives, ITEM_ID_COPPER_INGOT);
    }
}

TEST(each_pattern_of_copper_ingots_makes_its_piece_on_the_table_at_every_height_it_fits) {
    uint32_t made = 0u;
    for (uint32_t i = 0u; i < CC_PIECES; i++) {
        const cc_piece_t *const piece = &CC_PIECE_ROWS[i];
        for (uint32_t top = 0u; top + piece->height <= CRAFTING_SIDE_TABLE; top++) {
            crafting_grid_t table;
            crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
            item_stack_t cursor = item_stack_make(ITEM_ID_COPPER_INGOT, 64u);
            cc_lay(&table, piece, top, &cursor);
            CHECK_EQ_U64(cursor.count, 64u - piece->ingots);
            REQUIRE(table.recipe != CRAFTING_NO_RECIPE);
            CHECK_STR_EQ(recipe_defs[table.recipe].name, piece->recipe);
            CHECK_EQ_U64(table.output.item, piece->item);
            CHECK_EQ_U64(table.output.count, 1u);
            CHECK_EQ_U64(table.output.wear, 0u);

            /* Taking it uses every ingot, once, and leaves nothing on the table to make another. */
            item_stack_t picked = {0};
            CHECK_EQ_U64(crafting_grid_take(&table, &picked), 1u);
            CHECK_EQ_U64(picked.item, piece->item);
            CHECK(item_stack_is_valid(&picked));
            CHECK(item_stack_is_empty(&table.output));
            for (uint32_t cell = 0u; cell < RECIPE_CELLS; cell++) {
                CHECK(item_stack_is_empty(&table.cells[cell]));
            }
            made++;
        }
    }
    /* A helmet and boots fit at two heights, a chestplate and leggings at one. */
    CHECK_EQ_U64(made, 6u);
}

/* All four are three wide, as they are in iron. */
TEST(a_grid_of_two_makes_none_of_them) {
    for (uint32_t i = 0u; i < CC_PIECES; i++) {
        const cc_piece_t *const piece = &CC_PIECE_ROWS[i];
        for (uint32_t row = 0u; row < RECIPE_COUNT; row++) {
            if (recipe_defs[row].gives == piece->item) {
                CHECK_EQ_U64(recipe_defs[row].width, CRAFTING_SIDE_TABLE);
                CHECK_EQ_U64(recipe_defs[row].height, piece->height);
            }
        }
        item_stack_t cells[RECIPE_CELLS];
        memset(cells, 0, sizeof(cells));
        for (uint32_t cell = 0u; cell < 4u; cell++) {
            cells[cell] = item_stack_make(ITEM_ID_COPPER_INGOT, 1u);
            CHECK(cc_makes(cells, CRAFTING_SIDE_HANDS) != piece->item);
        }
    }
}

TEST(one_ingot_short_is_not_the_piece_and_one_of_iron_is_nothing) {
    for (uint32_t i = 0u; i < CC_PIECES; i++) {
        const cc_piece_t *const piece = &CC_PIECE_ROWS[i];
        for (uint32_t n = 0u; n < piece->ingots; n++) {
            const uint32_t changed = cc_ingot_cell(piece, n);
            REQUIRE(changed < RECIPE_CELLS);

            item_stack_t cells[RECIPE_CELLS];
            memset(cells, 0, sizeof(cells));
            for (uint32_t m = 0u; m < piece->ingots; m++) {
                cells[cc_ingot_cell(piece, m)] = item_stack_make(ITEM_ID_COPPER_INGOT, 1u);
            }
            CHECK_EQ_U64(cc_makes(cells, CRAFTING_SIDE_TABLE), piece->item);

            cells[changed] = (item_stack_t){0};
            CHECK(cc_makes(cells, CRAFTING_SIDE_TABLE) != piece->item);

            cells[changed] = item_stack_make(ITEM_ID_IRON_INGOT, 1u);
            CHECK_EQ_U64(crafting_match(cells, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE), CRAFTING_NO_RECIPE);
        }
    }
    /* A helmet without the middle of its top row is the shape of boots, as in iron. */
    item_stack_t cells[RECIPE_CELLS];
    memset(cells, 0, sizeof(cells));
    cells[0] = cells[2] = cells[3] = cells[5] = item_stack_make(ITEM_ID_COPPER_INGOT, 1u);
    CHECK_EQ_U64(cc_makes(cells, CRAFTING_SIDE_TABLE), ITEM_ID_COPPER_BOOTS);
}

/* Copper did not stop iron being what its own ingots make. */
TEST(the_same_shapes_in_iron_still_make_iron) {
    for (uint32_t i = 0u; i < CC_PIECES; i++) {
        const cc_piece_t *const piece = &CC_PIECE_ROWS[i];
        crafting_grid_t table;
        crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
        item_stack_t cursor = item_stack_make(ITEM_ID_IRON_INGOT, 64u);
        cc_lay(&table, piece, 0u, &cursor);
        CHECK_EQ_U64(table.output.item, piece->iron);
    }
}

TEST(a_whole_set_from_a_stack_of_ingots_goes_on_and_counts) {
    crafting_grid_t table;
    crafting_grid_start(&table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    equipment_t equipment;
    equipment_start(&equipment);
    item_stack_t ingots = item_stack_make(ITEM_ID_COPPER_INGOT, (uint16_t)CC_INGOTS_FOR_A_SET);
    for (uint32_t i = 0u; i < CC_PIECES; i++) {
        const cc_piece_t *const piece = &CC_PIECE_ROWS[i];
        cc_lay(&table, piece, 0u, &ingots);
        item_stack_t picked = {0};
        CHECK_EQ_U64(crafting_grid_take(&table, &picked), 1u);
        CHECK(equipment_armor_click(&equipment, piece->slot, &picked));
        CHECK(item_stack_is_empty(&picked));
    }
    CHECK(item_stack_is_empty(&ingots));
    CHECK_EQ_U64(table.crafted_count, CC_PIECES);
    CHECK_EQ_U64(equipment_armor_points(&equipment), 10u);
}

TEST_MAIN()
