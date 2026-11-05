/* ctest run_item_equipment_copper. See README.md. */
#include "harness/index.h"

#include "item/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"

#include <stdio.h>

#define EC_DAMAGE_EPSILON 1.0e-4f64

/* The stats in plans/003-ahmed-bohemoth-initial-game-fixes-phase-06-copper-armour.md, in the order of the slots. */
typedef struct ec_piece_t {
    uint16_t item;       /* 0 */
    uint16_t durability; /* 2 */
    uint32_t points;     /* 4 */
} ec_piece_t;
static_assert(sizeof(ec_piece_t) == 8);

static const ec_piece_t EC_PIECES[EQUIPMENT_ARMOR_SLOTS] = {
    {ITEM_ID_COPPER_HELMET, 121u, 2u},
    {ITEM_ID_COPPER_CHESTPLATE, 176u, 4u},
    {ITEM_ID_COPPER_LEGGINGS, 165u, 3u},
    {ITEM_ID_COPPER_BOOTS, 143u, 1u},
};

static const uint16_t EC_GOLD[EQUIPMENT_ARMOR_SLOTS] = {ITEM_ID_GOLD_HELMET, ITEM_ID_GOLD_CHESTPLATE,
                                                        ITEM_ID_GOLD_LEGGINGS, ITEM_ID_GOLD_BOOTS};
static const uint16_t EC_IRON[EQUIPMENT_ARMOR_SLOTS] = {ITEM_ID_IRON_HELMET, ITEM_ID_IRON_CHESTPLATE,
                                                        ITEM_ID_IRON_LEGGINGS, ITEM_ID_IRON_BOOTS};

TEST(each_piece_has_the_stats_of_the_plan_and_stacks_to_one) {
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        const ec_piece_t *const piece = &EC_PIECES[slot];
        const item_def_t *const def = &item_defs[piece->item];
        CHECK_EQ_U64(def->durability, piece->durability);
        CHECK_EQ_U64(def->armor_points, piece->points);
        CHECK_EQ_U64(def->armor_toughness, 0u);
        CHECK_EQ_U64(def->max_stack, 1u);
        CHECK_EQ_U64(def->tool, BLOCK_TOOL_NONE);
        CHECK_EQ_U64(equipment_armor_slot_of(piece->item), slot);
    }
}

/* What the plan set out to do: longer than gold and shorter than iron, on every piece. */
TEST(copper_lasts_longer_than_gold_and_not_as_long_as_iron) {
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        CHECK(EC_PIECES[slot].durability > item_defs[EC_GOLD[slot]].durability);
        CHECK(EC_PIECES[slot].durability < item_defs[EC_IRON[slot]].durability);
    }
}

TEST(each_piece_goes_in_its_own_slot_only) {
    for (uint32_t piece_at = 0u; piece_at < EQUIPMENT_ARMOR_SLOTS; piece_at++) {
        for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
            equipment_t equipment;
            equipment_start(&equipment);
            item_stack_t cursor = item_stack_make(EC_PIECES[piece_at].item, 1u);
            const bool went_on = equipment_armor_click(&equipment, slot, &cursor);
            CHECK(went_on == (slot == piece_at));
            if (slot == piece_at) {
                CHECK(item_stack_is_empty(&cursor));
                CHECK_EQ_U64(equipment.armor[slot].item, EC_PIECES[piece_at].item);
            } else {
                CHECK_EQ_U64(cursor.item, EC_PIECES[piece_at].item);
                CHECK(item_stack_is_empty(&equipment.armor[slot]));
            }
        }
    }
}

TEST(from_the_hand_each_piece_finds_its_own_slot_and_a_worn_one_comes_back_as_worn) {
    arena_t arena;
    inventory_t inventory;
    arena_init(&arena, "ec_inventory", test_alloc(t, INVENTORY_BYTES), INVENTORY_BYTES);
    inventory_start(&inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        item_stack_t stack = item_stack_make(EC_PIECES[slot].item, 1u);
        stack.wear = (uint16_t)(slot + 1u);
        *inventory_slot(&inventory, 0u) = stack;
        inventory.added_count++;
        CHECK(equipment_armor_wear_held(&equipment, &inventory));
        CHECK(item_stack_is_empty(inventory_selected(&inventory)));
        CHECK_EQ_U64(equipment.armor[slot].item, EC_PIECES[slot].item);
        CHECK_EQ_U64(equipment.armor[slot].wear, slot + 1u);
    }
    /* Iron goes on over copper, and the copper comes to the hand with its wear. */
    *inventory_slot(&inventory, 0u) = item_stack_make(ITEM_ID_IRON_BOOTS, 1u);
    inventory.added_count++;
    CHECK(equipment_armor_wear_held(&equipment, &inventory));
    CHECK_EQ_U64(equipment.armor[3].item, ITEM_ID_IRON_BOOTS);
    CHECK_EQ_U64(inventory_selected(&inventory)->item, ITEM_ID_COPPER_BOOTS);
    CHECK_EQ_U64(inventory_selected(&inventory)->wear, 4u);
    CHECK_EQ_U64(inventory.added_count, inventory.removed_count + inventory_items_held(&inventory));
}

TEST(each_piece_gives_its_points_and_a_full_set_gives_ten) {
    equipment_t equipment;
    equipment_start(&equipment);
    uint32_t total = 0u;
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        equipment_t alone;
        equipment_start(&alone);
        alone.armor[slot] = item_stack_make(EC_PIECES[slot].item, 1u);
        CHECK_EQ_U64(equipment_armor_points(&alone), EC_PIECES[slot].points);

        equipment.armor[slot] = alone.armor[slot];
        total += EC_PIECES[slot].points;
        CHECK_EQ_U64(equipment_armor_points(&equipment), total);
    }
    CHECK_EQ_U64(total, 10u);
}

/* max(2, 10 - 10 / 2) = 5, so 10 * (1 - 5 / 25). It is one hit, and it wears each piece by a quarter of it. */
TEST(a_full_set_stops_a_hit_worked_out_by_hand) {
    equipment_t equipment;
    equipment_start(&equipment);
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        equipment.armor[slot] = item_stack_make(EC_PIECES[slot].item, 1u);
    }
    CHECK_NEAR((float64_t)equipment_armor_hurt(&equipment, 10.0f32), 8.0f64, EC_DAMAGE_EPSILON);
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        CHECK_EQ_U64(equipment.armor[slot].wear, 2u);
    }
}

TEST(each_piece_lasts_exactly_its_uses_and_stops_the_hit_that_breaks_it) {
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        const ec_piece_t *const piece = &EC_PIECES[slot];
        equipment_t equipment;
        equipment_start(&equipment);
        equipment.armor[slot] = item_stack_make(piece->item, 1u);
        const float32_t stopped = equipment_armor_absorb(piece->points, 0u, 1.0f32);
        for (uint32_t use = 1u; use < piece->durability; use++) {
            CHECK_NEAR((float64_t)equipment_armor_hurt(&equipment, 1.0f32), (float64_t)stopped, EC_DAMAGE_EPSILON);
            CHECK_EQ_U64(equipment.armor[slot].wear, use);
            CHECK_EQ_U64(equipment_uses_left(&equipment.armor[slot]), (uint32_t)piece->durability - use);
        }
        CHECK_EQ_U64(equipment.armor_broken_count, 0u);
        CHECK_EQ_U64(equipment_armor_points(&equipment), piece->points);

        /* The last one. */
        CHECK_NEAR((float64_t)equipment_armor_hurt(&equipment, 1.0f32), (float64_t)stopped, EC_DAMAGE_EPSILON);
        CHECK(item_stack_is_empty(&equipment.armor[slot]));
        CHECK(item_stack_is_valid(&equipment.armor[slot]));
        CHECK_EQ_U64(equipment.armor_broken_count, 1u);
        CHECK_EQ_U64(equipment_armor_points(&equipment), 0u);

        /* Gone: the next goes through whole. */
        CHECK_NEAR((float64_t)equipment_armor_hurt(&equipment, 1.0f32), 1.0f64, EC_DAMAGE_EPSILON);
        CHECK_EQ_U64(equipment.armor_broken_count, 1u);
    }
}

TEST_MAIN()
