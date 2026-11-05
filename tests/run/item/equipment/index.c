/* ctest run_item_equipment. See README.md. */
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
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    IE_RANDOM_STEPS = 200000u,
    IE_WIDTH = 160u,
    IE_HEIGHT = 90u,
    IE_PROGRAM_TIMEOUT_MS = 120000u,
    /* What the threads may be off by, reading a tick count from the side. */
    IE_TICKS_SLACK = 8u,
    /* Worked out in run/src/player/interaction/README.md. */
    IE_STONE_TICKS_WOOD_PICKAXE = 23u,
    IE_STONE_TICKS_HAND = 150u,
    /* 45 ticks' work over a speed of 12. */
    IE_STONE_TICKS_GOLD_PICKAXE = 4u,
};

#define IE_DAMAGE_EPSILON 1.0e-4f64

static uint64_t ie_random(uint64_t *state) {
    /* xorshift64*: only so that a failure comes back the same. */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x9E3779B97F4A7C15ull;
}

static void ie_inventory(test_context_t *t, inventory_t *inventory, arena_t *arena) {
    arena_init(arena, "ie_inventory", test_alloc(t, INVENTORY_BYTES), INVENTORY_BYTES);
    inventory_start(inventory, arena);
}

static uint16_t ie_item_named(test_context_t *t, const char *name) {
    for (uint32_t row = 1u; row < ITEM_COUNT; row++) {
        if (strcmp(item_defs[row].name, name) == 0) {
            return (uint16_t)row;
        }
    }
    CHECK_STR_EQ("an item of this name", name);
    return ITEM_ID_NONE;
}

/* A slot, by hand: new, with this much gone, and the books told. */
static void ie_put(inventory_t *inventory, uint32_t slot, uint16_t item, uint16_t wear) {
    item_stack_t *const stack = inventory_slot(inventory, slot);
    *stack = item_stack_make(item, 1u);
    stack->wear = wear;
    inventory->added_count++;
}

static bool ie_books_balance(const inventory_t *inventory) {
    return inventory->added_count == inventory->removed_count + inventory_items_held(inventory);
}

/* ---- The tables ------------------------------------------------------------------------------------------------------ */

typedef struct ie_tier_row_t {
    const char *tier;    /* 0 */
    uint16_t durability; /* 8 */
    uint8_t _pad0[6];    /* 10 */
} ie_tier_row_t;
static_assert(sizeof(ie_tier_row_t) == 16);

TEST(every_tool_has_its_tiers_published_durability) {
    static const ie_tier_row_t tiers[] = {
        {"wood", 59u, {0}}, {"stone", 131u, {0}}, {"iron", 250u, {0}}, {"gold", 32u, {0}}, {"diamond", 1561u, {0}},
    };
    static const char *const parts[] = {"pickaxe", "axe", "shovel", "hoe", "sword"};
    for (uint32_t i = 0u; i < sizeof(tiers) / sizeof(tiers[0]); i++) {
        for (uint32_t p = 0u; p < sizeof(parts) / sizeof(parts[0]); p++) {
            char name[64];
            snprintf(name, sizeof(name), "%s_%s", tiers[i].tier, parts[p]);
            const uint16_t item = ie_item_named(t, name);
            CHECK_EQ_U64(item_defs[item].durability, tiers[i].durability);
            CHECK(item_defs[item].tool != BLOCK_TOOL_NONE);
        }
    }
    CHECK_EQ_U64(item_defs[ITEM_ID_SHEARS].durability, 238u);
}

typedef struct ie_armor_row_t {
    const char *material;                       /* 0 */
    uint16_t durability[EQUIPMENT_ARMOR_SLOTS]; /* 8 */
    uint8_t points[EQUIPMENT_ARMOR_SLOTS];      /* 16 */
    uint8_t _pad0[4];                           /* 20 */
} ie_armor_row_t;
static_assert(sizeof(ie_armor_row_t) == 24);

TEST(every_piece_of_armor_has_its_published_points_and_durability_and_its_slot) {
    static const ie_armor_row_t rows[] = {
        {"leather", {55u, 80u, 75u, 65u}, {1u, 3u, 2u, 1u}, {0}},
        {"gold", {77u, 112u, 105u, 91u}, {2u, 5u, 3u, 1u}, {0}},
        {"iron", {165u, 240u, 225u, 195u}, {2u, 6u, 5u, 2u}, {0}},
        {"diamond", {363u, 528u, 495u, 429u}, {3u, 8u, 6u, 3u}, {0}},
    };
    static const char *const parts[EQUIPMENT_ARMOR_SLOTS] = {"helmet", "chestplate", "leggings", "boots"};
    for (uint32_t i = 0u; i < sizeof(rows) / sizeof(rows[0]); i++) {
        for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
            char name[64];
            snprintf(name, sizeof(name), "%s_%s", rows[i].material, parts[slot]);
            const uint16_t item = ie_item_named(t, name);
            CHECK_EQ_U64(item_defs[item].durability, rows[i].durability[slot]);
            CHECK_EQ_U64(item_defs[item].armor_points, rows[i].points[slot]);
            CHECK_EQ_U64(equipment_armor_slot_of(item), slot);
        }
    }
}

TEST(what_wears_stacks_to_one_and_what_is_not_armor_has_no_slot) {
    uint32_t armor = 0u;
    for (uint32_t item = 0u; item < ITEM_COUNT; item++) {
        const item_def_t *const def = &item_defs[item];
        if (def->durability != 0u) {
            CHECK_EQ_U64(def->max_stack, 1u);
        }
        if (def->armor_slot == ITEM_ARMOR_SLOT_NONE) {
            CHECK_EQ_U64(equipment_armor_slot_of((uint16_t)item), EQUIPMENT_NOT_ARMOR);
        } else {
            armor++;
            CHECK(equipment_armor_slot_of((uint16_t)item) < EQUIPMENT_ARMOR_SLOTS);
        }
    }
    /* Four pieces each of leather, gold, copper, iron and diamond. Copper was the last to come, in plan 003 phase 6. */
    CHECK_EQ_U64(armor, 20u);
    CHECK_EQ_U64(equipment_armor_slot_of(ITEM_COUNT), EQUIPMENT_NOT_ARMOR);
}

/* ---- What a tool mines as --------------------------------------------------------------------------------------------- */

TEST(the_slot_in_hand_is_what_mining_goes_by_and_gold_breaks_what_wood_breaks) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    uint8_t tool = 99u, tier = 99u;
    equipment_tool_held(&inventory, &tool, &tier);
    CHECK_EQ_U64(tool, BLOCK_TOOL_NONE);
    CHECK_EQ_U64(tier, BLOCK_TIER_NONE);

    ie_put(&inventory, 0u, ITEM_ID_IRON_PICKAXE, 0u);
    equipment_tool_held(&inventory, &tool, &tier);
    CHECK_EQ_U64(tool, BLOCK_TOOL_PICKAXE);
    CHECK_EQ_U64(tier, BLOCK_TIER_IRON);
    CHECK(player_interaction_harvests(BLOCK_ID_DIAMOND_ORE, tool, tier));

    ie_put(&inventory, 0u, ITEM_ID_GOLD_PICKAXE, 0u);
    equipment_tool_held(&inventory, &tool, &tier);
    CHECK_EQ_U64(tier, BLOCK_TIER_WOOD);
    CHECK(player_interaction_harvests(BLOCK_ID_STONE, tool, tier));
    CHECK(!player_interaction_harvests(BLOCK_ID_IRON_ORE, tool, tier));

    ie_put(&inventory, 0u, ITEM_ID_COAL, 0u);
    equipment_tool_held(&inventory, &tool, &tier);
    CHECK_EQ_U64(tool, BLOCK_TOOL_NONE);
    CHECK_EQ_U64(tier, BLOCK_TIER_NONE);

    ie_put(&inventory, 0u, ITEM_ID_WOOD_PICKAXE, 0u);
    equipment_tool_held(&inventory, &tool, &tier);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, tool, tier, true, false), IE_STONE_TICKS_WOOD_PICKAXE);
    CHECK_EQ_U64(player_interaction_ticks(BLOCK_ID_STONE, BLOCK_TOOL_NONE, BLOCK_TIER_NONE, true, false),
                 IE_STONE_TICKS_HAND);
}

/* ---- Wear ------------------------------------------------------------------------------------------------------------- */

/* A world and a body at rest, with hands that are only told what they hold, and the target set by hand. */
typedef struct ie_hands_t {
    test_world_t world;         /* 0 */
    player_movement_t movement; /* 24 */
    player_target_t target;     /* 136 */
    player_interaction_t hands; /* 232 */
} ie_hands_t;
static_assert(sizeof(ie_hands_t) == 1384);

/* Holds the left button on this cell until it changes. How many ticks that took, up to a limit. */
static uint32_t ie_hands_mine(ie_hands_t *rig, int32_t x, int32_t y, int32_t z, uint32_t limit) {
    uint16_t block = BLOCK_ID_AIR;
    (void)world_map_block_get(rig->world.map, x, y, z, &block);
    memset(&rig->target, 0, sizeof(rig->target));
    rig->target.hit.result = RAYCAST_RESULT_HIT;
    rig->target.hit.cell[0] = x;
    rig->target.hit.cell[1] = y;
    rig->target.hit.cell[2] = z;
    rig->target.hit.block = block;
    rig->target.hit.axis = COLLISION_AXIS_Z;
    rig->target.hit.normal_sign = 1;
    const player_interaction_input_t input = {
        .borrowed_target = &rig->target, .borrowed_movement = &rig->movement, .attack = 1u, .place = 0u, ._pad0 = {0}};
    for (uint32_t tick = 1u; tick <= limit; tick++) {
        player_interaction_tick(&rig->hands, &rig->world.world->edit, &input);
        uint16_t now = block;
        (void)world_map_block_get(rig->world.map, x, y, z, &now);
        if (now != block) {
            return tick;
        }
    }
    return UINT32_MAX;
}

static bool ie_hands_begin(test_context_t *t, ie_hands_t *rig) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    player_movement_start(&rig->movement, 40.5f64, 40.5f64, 0.0f64);
    rig->movement.on_ground = 1u;
    player_interaction_start(&rig->hands, 4321u);
    return true;
}

/* Published: 12, so stone at 1.5 * 30 / 12 and rounded up. As wood in what it harvests, and a sword stays a sword. */
TEST(gold_mines_faster_than_diamond_and_harvests_only_what_wood_does) {
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_GOLD_PICKAXE), 12000u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_GOLD_SHOVEL), 12000u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_GOLD_AXE), 12000u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_GOLD_HOE), 12000u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_GOLD_SWORD), 0u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_DIAMOND_PICKAXE), 0u);
    CHECK_EQ_U64(equipment_tool_speed_milli(ITEM_ID_NONE), 0u);

    ie_hands_t *const rig = test_alloc(t, sizeof(ie_hands_t));
    REQUIRE(ie_hands_begin(t, rig));
    test_world_fill(t, &rig->world, 0, 0, 10, 4, 0, 10, BLOCK_ID_STONE);
    player_interaction_hold_tool(&rig->hands, BLOCK_TOOL_PICKAXE, BLOCK_TIER_WOOD);
    CHECK_EQ_U64(ie_hands_mine(rig, 0, 0, 10, 100u), IE_STONE_TICKS_WOOD_PICKAXE);
    rig->hands.mine_delay = 0u;
    player_interaction_hold_speed(&rig->hands, equipment_tool_speed_milli(ITEM_ID_GOLD_PICKAXE));
    CHECK_EQ_U64(ie_hands_mine(rig, 1, 0, 10, 100u), IE_STONE_TICKS_GOLD_PICKAXE);
    /* Off its own blocks it is a hand, like any other pickaxe. */
    world_edit_block_set(&rig->world.world->edit, 2, 0, 10, BLOCK_ID_DIRT);
    rig->hands.mine_delay = 0u;
    CHECK_EQ_U64(ie_hands_mine(rig, 2, 0, 10, 100u), 15u);
    /* What wood cannot harvest takes its 3 * 100 ticks over 12, and gives nothing. */
    world_edit_block_set(&rig->world.world->edit, 3, 0, 10, BLOCK_ID_IRON_ORE);
    const uint32_t drops = rig->hands.drop_count;
    rig->hands.mine_delay = 0u;
    CHECK_EQ_U64(ie_hands_mine(rig, 3, 0, 10, 100u), 25u);
    CHECK_EQ_U64(rig->hands.drop_count, drops);
    test_world_end(&rig->world);
}

/* A table of chance 1 in 8 would come up many times in this many, if it were rolled. */
TEST(shears_roll_nothing_of_a_table_and_every_block_of_theirs_has_an_item_to_give) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        if (block_defs[block].tool == BLOCK_TOOL_SHEARS) {
            CHECK(equipment_own_drop(ITEM_ID_SHEARS, (uint16_t)block) != ITEM_ID_NONE);
        }
    }
    ie_hands_t *const rig = test_alloc(t, sizeof(ie_hands_t));
    REQUIRE(ie_hands_begin(t, rig));
    enum : uint32_t { CUTS = 200u };
    player_interaction_hold_tool(&rig->hands, BLOCK_TOOL_SHEARS, BLOCK_TIER_NONE);
    test_world_fill(t, &rig->world, 0, 0, 10, 0, 0, 10, BLOCK_ID_STONE);
    for (uint32_t cut = 0u; cut < CUTS; cut++) {
        world_edit_block_set(&rig->world.world->edit, 0, 0, 11, BLOCK_ID_TALL_GRASS);
        CHECK_EQ_U64(ie_hands_mine(rig, 0, 0, 11, 2u), 1u);
        CHECK_EQ_U64(rig->hands.drop_count, 0u);
    }
    /* The same grass by hand does roll, so that the loop above can be believed. */
    player_interaction_hold_tool(&rig->hands, BLOCK_TOOL_NONE, BLOCK_TIER_NONE);
    uint32_t rolled = 0u;
    for (uint32_t cut = 0u; cut < CUTS; cut++) {
        world_edit_block_set(&rig->world.world->edit, 0, 0, 11, BLOCK_ID_TALL_GRASS);
        CHECK_EQ_U64(ie_hands_mine(rig, 0, 0, 11, 2u), 1u);
        player_drop_t drop;
        while (player_interaction_drop_next(&rig->hands, &drop)) {
            rolled++;
        }
    }
    CHECK(rolled > 0u);
    test_world_end(&rig->world);
}

TEST(what_a_block_costs_worked_out_by_hand) {
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_WOOD_PICKAXE, BLOCK_ID_STONE), 1u);
    /* Its own block or not, it is used. */
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_WOOD_PICKAXE, BLOCK_ID_DIRT), 1u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_IRON_AXE, BLOCK_ID_STONE), 1u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_DIAMOND_SHOVEL, BLOCK_ID_DIRT), 1u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_STONE_HOE, BLOCK_ID_DIRT), 1u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_IRON_SWORD, BLOCK_ID_DIRT), 2u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_SHEARS, BLOCK_ID_OAK_LEAVES), 1u);
    /* At a touch: nothing, unless it is shears on their own. */
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_WOOD_PICKAXE, BLOCK_ID_TALL_GRASS), 0u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_IRON_SWORD, BLOCK_ID_TALL_GRASS), 0u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_SHEARS, BLOCK_ID_TALL_GRASS), 1u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_SHEARS, BLOCK_ID_TORCH), 0u);
    /* No tool. */
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_NONE, BLOCK_ID_STONE), 0u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_COAL, BLOCK_ID_STONE), 0u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_BOW, BLOCK_ID_STONE), 0u);
    CHECK_EQ_U64(equipment_wear_of_block(ITEM_ID_IRON_HELMET, BLOCK_ID_STONE), 0u);
}

TEST(what_a_hit_costs_and_does) {
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_IRON_SWORD), 1u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_IRON_PICKAXE), 2u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_IRON_AXE), 2u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_IRON_SHOVEL), 2u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_IRON_HOE), 2u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_SHEARS), 0u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_NONE), 0u);
    CHECK_EQ_U64(equipment_wear_of_attack(ITEM_ID_COAL), 0u);

    const item_stack_t empty = {0}, coal = item_stack_make(ITEM_ID_COAL, 1u);
    const item_stack_t sword = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u), shears = item_stack_make(ITEM_ID_SHEARS, 1u);
    CHECK_NEAR((float64_t)equipment_attack_damage(&empty), 1.0f64, IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_attack_damage(&coal), 1.0f64, IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_attack_damage(&shears), 1.0f64, IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_attack_damage(&sword), 7.0f64, IE_DAMAGE_EPSILON);
}

TEST(every_tool_lasts_exactly_its_durability_and_then_is_gone_by_the_books) {
    uint32_t tested = 0u;
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        const uint32_t durability = item_defs[item].durability;
        if (durability == 0u) {
            continue;
        }
        tested++;
        inventory_t inventory;
        arena_t arena;
        ie_inventory(t, &inventory, &arena);
        equipment_t equipment;
        equipment_start(&equipment);
        ie_put(&inventory, 0u, (uint16_t)item, 0u);
        const item_stack_t *const held = inventory_selected(&inventory);
        CHECK_EQ_U64(equipment_uses_left(held), durability);
        for (uint32_t use = 1u; use < durability; use++) {
            CHECK(!equipment_tool_wear(&equipment, &inventory, 1u));
            CHECK_EQ_U64(held->wear, use);
            CHECK_EQ_U64(equipment_uses_left(held), durability - use);
            CHECK(item_stack_is_valid(held));
        }
        CHECK_EQ_U64(equipment.tools_broken_count, 0u);
        CHECK(equipment_tool_wear(&equipment, &inventory, 1u));
        CHECK(item_stack_is_empty(held));
        CHECK(item_stack_is_valid(held));
        CHECK_EQ_U64(equipment.tools_broken_count, 1u);
        CHECK_EQ_U64(inventory.removed_count, 1u);
        CHECK(ie_books_balance(&inventory));
        /* Nothing left, and nothing more breaks. */
        CHECK(!equipment_tool_wear(&equipment, &inventory, 1u));
        CHECK_EQ_U64(equipment.tools_broken_count, 1u);
    }
    CHECK(tested >= 44u);
}

TEST(two_uses_with_one_left_break_it_and_none_cost_nothing) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    ie_put(&inventory, 0u, ITEM_ID_WOOD_SWORD, 58u);
    CHECK(!equipment_tool_wear(&equipment, &inventory, 0u));
    CHECK_EQ_U64(inventory_selected(&inventory)->wear, 58u);
    CHECK(equipment_tool_block_broken(&equipment, &inventory, BLOCK_ID_DIRT));
    CHECK(item_stack_is_empty(inventory_selected(&inventory)));
    CHECK_EQ_U64(equipment.tools_broken_count, 1u);

    /* Hitting too. */
    ie_put(&inventory, 0u, ITEM_ID_WOOD_PICKAXE, 57u);
    CHECK(equipment_tool_attacked(&equipment, &inventory));
    CHECK_EQ_U64(equipment.tools_broken_count, 2u);
    ie_put(&inventory, 0u, ITEM_ID_WOOD_SWORD, 57u);
    CHECK(!equipment_tool_attacked(&equipment, &inventory));
    CHECK_EQ_U64(inventory_selected(&inventory)->wear, 58u);
}

TEST(only_the_slot_in_hand_wears_and_a_stack_of_what_does_not_wear_is_left_alone) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    ie_put(&inventory, 0u, ITEM_ID_IRON_PICKAXE, 0u);
    ie_put(&inventory, 1u, ITEM_ID_IRON_PICKAXE, 0u);
    (void)inventory_add(&inventory, ITEM_ID_COAL, 5u);
    inventory_select(&inventory, 1u);
    CHECK(!equipment_tool_block_broken(&equipment, &inventory, BLOCK_ID_STONE));
    CHECK_EQ_U64(inventory_slot_read(&inventory, 0u)->wear, 0u);
    CHECK_EQ_U64(inventory_slot_read(&inventory, 1u)->wear, 1u);
    /* A worn one and a new one are not the same to anything that merges them. */
    CHECK(!item_stack_can_merge(inventory_slot_read(&inventory, 0u), inventory_slot_read(&inventory, 1u)));

    inventory_select(&inventory, 2u);
    const item_stack_t coal = *inventory_selected(&inventory);
    CHECK_EQ_U64(coal.item, ITEM_ID_COAL);
    CHECK(!equipment_tool_wear(&equipment, &inventory, 3u));
    CHECK_MEM_EQ(inventory_selected(&inventory), &coal, sizeof(coal));

    inventory_select(&inventory, 8u);
    CHECK(!equipment_tool_wear(&equipment, &inventory, 1u));
    CHECK(ie_books_balance(&inventory));
}

/* ---- Shears ----------------------------------------------------------------------------------------------------------- */

TEST(shears_bring_out_what_they_broke_and_nothing_else_does) {
    CHECK_EQ_U64(equipment_own_drop(ITEM_ID_SHEARS, BLOCK_ID_OAK_LEAVES), ITEM_ID_OAK_LEAVES);
    CHECK_EQ_U64(equipment_own_drop(ITEM_ID_SHEARS, BLOCK_ID_TALL_GRASS), ITEM_ID_TALL_GRASS);
    CHECK_EQ_U64(equipment_own_drop(ITEM_ID_SHEARS, BLOCK_ID_STONE), ITEM_ID_NONE);
    CHECK_EQ_U64(equipment_own_drop(ITEM_ID_IRON_SWORD, BLOCK_ID_OAK_LEAVES), ITEM_ID_NONE);
    CHECK_EQ_U64(equipment_own_drop(ITEM_ID_NONE, BLOCK_ID_OAK_LEAVES), ITEM_ID_NONE);
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        const uint16_t own = equipment_own_drop(ITEM_ID_SHEARS, (uint16_t)block);
        if (own == ITEM_ID_NONE) {
            continue;
        }
        CHECK_EQ_U64(block_defs[block].tool, BLOCK_TOOL_SHEARS);
        CHECK_EQ_U64(item_defs[own].block, block);
        /* What its table always gives anyway would come out twice. */
        const block_def_t *const def = &block_defs[block];
        for (uint32_t row = 0u; row < def->drop_count; row++) {
            const block_drop_t *const drop = &block_drops[def->first_drop + row];
            CHECK(block_drop_items[drop->item].item != own || drop->chance < 1.0f32);
        }
    }
}

/* ---- A hoe ------------------------------------------------------------------------------------------------------------ */

static uint16_t ie_block_at(test_context_t *t, test_world_t *world, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    CHECK_EQ_U64(world_map_block_get(world->map, x, y, z, &block), WORLD_MAP_STATUS_OK);
    return block;
}

TEST(a_hoe_tills_what_it_can_with_air_above_for_one_use) {
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_DIRT), BLOCK_ID_FARMLAND);
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_GRASS_BLOCK), BLOCK_ID_FARMLAND);
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_COARSE_DIRT), BLOCK_ID_DIRT);
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_FARMLAND), BLOCK_ID_AIR);
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_STONE), BLOCK_ID_AIR);
    CHECK_EQ_U64(equipment_till_result(BLOCK_ID_AIR), BLOCK_ID_AIR);

    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    test_world_fill(t, &world, 0, 0, 10, 8, 0, 10, BLOCK_ID_DIRT);
    world_edit_block_set(edit, 1, 0, 10, BLOCK_ID_GRASS_BLOCK);
    world_edit_block_set(edit, 2, 0, 10, BLOCK_ID_COARSE_DIRT);
    world_edit_block_set(edit, 3, 0, 10, BLOCK_ID_STONE);
    world_edit_block_set(edit, 4, 0, 11, BLOCK_ID_STONE);
    world_edit_block_set(edit, 5, 0, 11, BLOCK_ID_TALL_GRASS);

    /* In hand there is nothing that tills. */
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 0, 0, 10));
    ie_put(&inventory, 8u, ITEM_ID_WOOD_PICKAXE, 0u);
    inventory_select(&inventory, 8u);
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 0, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 0, 0, 10), BLOCK_ID_DIRT);
    CHECK_EQ_U64(inventory_selected(&inventory)->wear, 0u);

    inventory_select(&inventory, 0u);
    ie_put(&inventory, 0u, ITEM_ID_IRON_HOE, 0u);
    const item_stack_t *const hoe = inventory_selected(&inventory);
    CHECK(equipment_hoe_till(&equipment, &inventory, edit, 0, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 0, 0, 10), BLOCK_ID_FARMLAND);
    CHECK_EQ_U64(hoe->wear, 1u);
    CHECK(equipment_hoe_till(&equipment, &inventory, edit, 1, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 1, 0, 10), BLOCK_ID_FARMLAND);
    CHECK(equipment_hoe_till(&equipment, &inventory, edit, 2, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 2, 0, 10), BLOCK_ID_DIRT);
    CHECK_EQ_U64(hoe->wear, 3u);

    /* Nothing, and nothing lost: what does not till, already tilled, something above, and a column not loaded. */
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 3, 0, 10));
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 0, 0, 10));
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 4, 0, 10));
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 5, 0, 10));
    CHECK(!equipment_hoe_till(&equipment, &inventory, edit, 100000, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 4, 0, 10), BLOCK_ID_DIRT);
    CHECK_EQ_U64(ie_block_at(t, &world, 5, 0, 10), BLOCK_ID_DIRT);
    CHECK_EQ_U64(hoe->wear, 3u);

    /* Its last use tills, and breaks it. */
    inventory_slot(&inventory, 0u)->wear = 249u;
    CHECK(equipment_hoe_till(&equipment, &inventory, edit, 6, 0, 10));
    CHECK_EQ_U64(ie_block_at(t, &world, 6, 0, 10), BLOCK_ID_FARMLAND);
    CHECK(item_stack_is_empty(hoe));
    CHECK_EQ_U64(equipment.tools_broken_count, 1u);
    test_world_end(&world);
}

TEST(the_button_held_tills_every_four_ticks_and_a_press_does_not_have_to_wait) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    world_edit_t *const edit = &world.world->edit;
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    test_world_fill(t, &world, 0, 0, 10, 15, 0, 10, BLOCK_ID_DIRT);
    ie_put(&inventory, 0u, ITEM_ID_IRON_HOE, 0u);
    const item_stack_t *const hoe = inventory_selected(&inventory);

    /* A new cell each tick, as if sweeping along them: it is the time that holds it back. */
    uint32_t tilled_at[4] = {0u};
    uint32_t tilled = 0u;
    for (int32_t tick = 0; tick < 13; tick++) {
        const int32_t cell[3] = {tick, 0, 10};
        const uint32_t before = hoe->wear;
        equipment_use_tick(&equipment, &inventory, edit, true, cell);
        if (hoe->wear != before && tilled < 4u) {
            tilled_at[tilled++] = (uint32_t)tick;
        }
    }
    CHECK_EQ_U64(tilled, 4u);
    CHECK_EQ_U64(tilled_at[0], 0u);
    CHECK_EQ_U64(tilled_at[1], 4u);
    CHECK_EQ_U64(tilled_at[2], 8u);
    CHECK_EQ_U64(tilled_at[3], 12u);
    CHECK_EQ_U64(hoe->wear, 4u);

    /* Let go, nothing happens, and nothing aimed at nothing either. */
    const int32_t next[3] = {13, 0, 10};
    for (uint32_t i = 0u; i < 8u; i++) {
        equipment_use_tick(&equipment, &inventory, edit, false, next);
    }
    CHECK_EQ_U64(ie_block_at(t, &world, 13, 0, 10), BLOCK_ID_DIRT);
    equipment_use_tick(&equipment, &inventory, edit, true, nullptr);
    CHECK_EQ_U64(hoe->wear, 4u);
    equipment_use_tick(&equipment, &inventory, edit, true, next);
    CHECK_EQ_U64(hoe->wear, 5u);
    test_world_end(&world);
}

/* ---- Armor on and off ------------------------------------------------------------------------------------------------- */

TEST(the_pointer_puts_armor_on_only_where_it_goes) {
    equipment_t equipment;
    equipment_start(&equipment);
    item_stack_t cursor = {0};
    /* Nothing either side, and slots that do not exist. */
    CHECK(!equipment_armor_click(&equipment, 0u, &cursor));
    CHECK(equipment_armor_worn(&equipment, EQUIPMENT_ARMOR_SLOTS) == nullptr);
    CHECK(equipment_armor_worn(&equipment, 3u) == &equipment.armor[3]);
    cursor = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    CHECK(!equipment_armor_click(&equipment, EQUIPMENT_ARMOR_SLOTS, &cursor));
    /* The wrong slot, and what is no armor. */
    CHECK(!equipment_armor_click(&equipment, 1u, &cursor));
    CHECK_EQ_U64(cursor.item, ITEM_ID_IRON_HELMET);
    item_stack_t coal = item_stack_make(ITEM_ID_COAL, 3u);
    CHECK(!equipment_armor_click(&equipment, 0u, &coal));
    CHECK_EQ_U64(coal.count, 3u);
    /* The right one, a swap keeping its wear, and off again. */
    CHECK(equipment_armor_click(&equipment, 0u, &cursor));
    CHECK(item_stack_is_empty(&cursor));
    CHECK_EQ_U64(equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    cursor = item_stack_make(ITEM_ID_DIAMOND_HELMET, 1u);
    cursor.wear = 7u;
    equipment.armor[0].wear = 5u;
    CHECK(equipment_armor_click(&equipment, 0u, &cursor));
    CHECK_EQ_U64(equipment.armor[0].item, ITEM_ID_DIAMOND_HELMET);
    CHECK_EQ_U64(equipment.armor[0].wear, 7u);
    CHECK_EQ_U64(cursor.item, ITEM_ID_IRON_HELMET);
    CHECK_EQ_U64(cursor.wear, 5u);
    cursor = (item_stack_t){0};
    CHECK(equipment_armor_click(&equipment, 0u, &cursor));
    CHECK_EQ_U64(cursor.item, ITEM_ID_DIAMOND_HELMET);
    CHECK(item_stack_is_empty(&equipment.armor[0]));
}

TEST(what_is_in_hand_goes_on_and_swaps_with_the_books_kept) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    /* An empty hand, and what is no armor. */
    CHECK(!equipment_armor_wear_held(&equipment, &inventory));
    (void)inventory_add(&inventory, ITEM_ID_COAL, 2u);
    CHECK(!equipment_armor_wear_held(&equipment, &inventory));
    CHECK_EQ_U64(inventory_selected(&inventory)->count, 2u);

    inventory_select(&inventory, 3u);
    ie_put(&inventory, 3u, ITEM_ID_IRON_BOOTS, 11u);
    CHECK(equipment_armor_wear_held(&equipment, &inventory));
    CHECK(item_stack_is_empty(inventory_selected(&inventory)));
    CHECK_EQ_U64(equipment.armor[3].item, ITEM_ID_IRON_BOOTS);
    CHECK_EQ_U64(equipment.armor[3].wear, 11u);
    CHECK(ie_books_balance(&inventory));

    /* What was on comes into the very slot in hand, even with an earlier slot free. */
    ie_put(&inventory, 3u, ITEM_ID_LEATHER_BOOTS, 2u);
    CHECK(equipment_armor_wear_held(&equipment, &inventory));
    CHECK_EQ_U64(equipment.armor[3].item, ITEM_ID_LEATHER_BOOTS);
    CHECK_EQ_U64(equipment.armor[3].wear, 2u);
    CHECK_EQ_U64(inventory_selected(&inventory)->item, ITEM_ID_IRON_BOOTS);
    CHECK_EQ_U64(inventory_selected(&inventory)->wear, 11u);
    CHECK(ie_books_balance(&inventory));
}

TEST(the_button_puts_armor_on_at_the_press_and_holding_it_does_not_swap_back) {
    test_world_t world;
    REQUIRE(test_world_begin(t, &world));
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    ie_put(&inventory, 0u, ITEM_ID_IRON_HELMET, 0u);
    equipment.armor[0] = item_stack_make(ITEM_ID_GOLD_HELMET, 1u);
    world_edit_t *const edit = &world.world->edit;
    for (uint32_t i = 0u; i < 20u; i++) {
        equipment_use_tick(&equipment, &inventory, edit, true, nullptr);
    }
    CHECK_EQ_U64(equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    CHECK_EQ_U64(inventory_selected(&inventory)->item, ITEM_ID_GOLD_HELMET);
    equipment_use_tick(&equipment, &inventory, edit, false, nullptr);
    equipment_use_tick(&equipment, &inventory, edit, true, nullptr);
    CHECK_EQ_U64(equipment.armor[0].item, ITEM_ID_GOLD_HELMET);
    test_world_end(&world);
}

TEST(a_shift_click_goes_across_when_there_is_room) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    ie_put(&inventory, 20u, ITEM_ID_IRON_CHESTPLATE, 9u);
    ie_put(&inventory, 21u, ITEM_ID_GOLD_CHESTPLATE, 0u);
    (void)inventory_add(&inventory, ITEM_ID_COAL, 1u);
    CHECK_EQ_U64(equipment_armor_quick_move_in(&equipment, &inventory, INVENTORY_SLOTS), 0u);
    CHECK_EQ_U64(equipment_armor_quick_move_in(&equipment, &inventory, 0u), 0u); /* the coal */
    CHECK_EQ_U64(equipment_armor_quick_move_in(&equipment, &inventory, 1u), 0u); /* nothing */
    CHECK_EQ_U64(equipment_armor_quick_move_in(&equipment, &inventory, 20u), 1u);
    CHECK_EQ_U64(equipment.armor[1].wear, 9u);
    CHECK(item_stack_is_empty(inventory_slot_read(&inventory, 20u)));
    /* That slot is taken now. */
    CHECK_EQ_U64(equipment_armor_quick_move_in(&equipment, &inventory, 21u), 0u);
    CHECK_EQ_U64(inventory_slot_read(&inventory, 21u)->count, 1u);
    CHECK(ie_books_balance(&inventory));

    CHECK_EQ_U64(equipment_armor_quick_move_out(&equipment, &inventory, EQUIPMENT_ARMOR_SLOTS), 0u);
    CHECK_EQ_U64(equipment_armor_quick_move_out(&equipment, &inventory, 0u), 0u);
    CHECK_EQ_U64(equipment_armor_quick_move_out(&equipment, &inventory, 1u), 1u);
    CHECK(item_stack_is_empty(&equipment.armor[1]));
    /* It kept its wear, wherever it went. */
    uint32_t found = 0u;
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        const item_stack_t *const stack = inventory_slot_read(&inventory, slot);
        if (stack->item == ITEM_ID_IRON_CHESTPLATE) {
            CHECK_EQ_U64(stack->wear, 9u);
            found++;
        }
    }
    CHECK_EQ_U64(found, 1u);
    CHECK(ie_books_balance(&inventory));

    /* No room: it stays on. */
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        if (item_stack_is_empty(inventory_slot_read(&inventory, slot))) {
            ie_put(&inventory, slot, ITEM_ID_SHEARS, 0u);
        }
    }
    equipment.armor[2] = item_stack_make(ITEM_ID_IRON_LEGGINGS, 1u);
    CHECK_EQ_U64(equipment_armor_quick_move_out(&equipment, &inventory, 2u), 0u);
    CHECK_EQ_U64(equipment.armor[2].count, 1u);
}

/* ---- What armor stops ------------------------------------------------------------------------------------------------- */

TEST(what_a_hit_leaves_worked_out_by_hand) {
    /* left = damage * (1 - min(20, max(points / 5, points - damage / 2)) / 25) */
    CHECK_NEAR((float64_t)equipment_armor_absorb(0u, 0u, 10.0f32), 10.0f64, IE_DAMAGE_EPSILON);
    /* max(4, 17.5) = 17.5, so 5 * (1 - 0.7) */
    CHECK_NEAR((float64_t)equipment_armor_absorb(20u, 0u, 5.0f32), 1.5f64, IE_DAMAGE_EPSILON);
    /* max(2, 5) = 5, so 10 * 0.8 */
    CHECK_NEAR((float64_t)equipment_armor_absorb(10u, 0u, 10.0f32), 8.0f64, IE_DAMAGE_EPSILON);
    /* max(0.6, -47) = 0.6: a hard hit goes through all but a fifth of the points. 100 * (1 - 0.024) */
    CHECK_NEAR((float64_t)equipment_armor_absorb(3u, 0u, 100.0f32), 97.6f64, IE_DAMAGE_EPSILON);
    /* min(20, 79.5): more than a full set stops no more. 1 * 0.2 */
    CHECK_NEAR((float64_t)equipment_armor_absorb(80u, 0u, 1.0f32), 0.2f64, IE_DAMAGE_EPSILON);
    /* Exactly at the floor: 7 - 28 / 2 is below 7 / 5. */
    CHECK_NEAR((float64_t)equipment_armor_absorb(7u, 0u, 28.0f32), 28.0f64 * (1.0f64 - 1.4f64 / 25.0f64),
               IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_armor_absorb(20u, 0u, 0.0f32), 0.0f64, IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_armor_absorb(20u, 0u, -3.0f32), 0.0f64, IE_DAMAGE_EPSILON);
}

TEST(toughness_keeps_hard_hits_out_and_both_sums_are_one_sum) {
    /* The published sum: 20 points against 20 is 20 - 20 / (2 + 8 / 4) = 15 of 25. With no toughness, 10 of 25. */
    CHECK_NEAR((float64_t)equipment_armor_absorb(20u, 8u, 20.0f32), 8.0f64, IE_DAMAGE_EPSILON);
    CHECK_NEAR((float64_t)equipment_armor_absorb(20u, 0u, 20.0f32), 12.0f64, IE_DAMAGE_EPSILON);
    /* What survival works out in a running game is what this says, for anything worn and any hit. */
    for (uint32_t points = 0u; points <= 24u; points++) {
        for (uint32_t toughness = 0u; toughness <= 8u; toughness += 2u) {
            for (uint32_t half = 1u; half <= 100u; half++) {
                const float32_t damage = (float32_t)half / 2.0f32;
                CHECK_NEAR((float64_t)equipment_armor_absorb(points, toughness, damage),
                           (float64_t)player_survival_after_armor(damage, (float32_t)points, (float32_t)toughness),
                           IE_DAMAGE_EPSILON);
            }
        }
    }
    /* Only diamond has any. */
    equipment_t equipment;
    equipment_start(&equipment);
    CHECK_EQ_U64(equipment_armor_toughness(&equipment), 0u);
    equipment.armor[0] = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    CHECK_EQ_U64(equipment_armor_toughness(&equipment), 0u);
    equipment.armor[0] = item_stack_make(ITEM_ID_DIAMOND_HELMET, 1u);
    equipment.armor[1] = item_stack_make(ITEM_ID_DIAMOND_CHESTPLATE, 1u);
    equipment.armor[2] = item_stack_make(ITEM_ID_DIAMOND_LEGGINGS, 1u);
    equipment.armor[3] = item_stack_make(ITEM_ID_DIAMOND_BOOTS, 1u);
    CHECK_EQ_U64(equipment_armor_toughness(&equipment), 8u);
}

TEST(more_points_never_let_more_through_and_a_hit_never_grows) {
    for (uint32_t tenths = 1u; tenths < 2000u; tenths += 7u) {
        const float32_t damage = (float32_t)tenths / 10.0f32;
        float32_t last = damage;
        for (uint32_t points = 0u; points <= 30u; points++) {
            const float32_t left = equipment_armor_absorb(points, 0u, damage);
            CHECK(left <= last);
            CHECK(left > 0.0f32);
            last = left;
        }
        /* Never less than a fifth of it. */
        CHECK((float64_t)last >= (float64_t)damage * 0.2f64 - IE_DAMAGE_EPSILON);
    }
}

TEST(all_four_pieces_add_up_and_a_full_set_is_its_published_total) {
    equipment_t equipment;
    equipment_start(&equipment);
    CHECK_EQ_U64(equipment_armor_points(&equipment), 0u);
    equipment.armor[0] = item_stack_make(ITEM_ID_DIAMOND_HELMET, 1u);
    equipment.armor[1] = item_stack_make(ITEM_ID_DIAMOND_CHESTPLATE, 1u);
    equipment.armor[2] = item_stack_make(ITEM_ID_DIAMOND_LEGGINGS, 1u);
    equipment.armor[3] = item_stack_make(ITEM_ID_DIAMOND_BOOTS, 1u);
    CHECK_EQ_U64(equipment_armor_points(&equipment), 20u);
    equipment.armor[1] = (item_stack_t){0};
    CHECK_EQ_U64(equipment_armor_points(&equipment), 12u);
    equipment.armor[1] = item_stack_make(ITEM_ID_LEATHER_CHESTPLATE, 1u);
    CHECK_EQ_U64(equipment_armor_points(&equipment), 15u);
}

TEST(a_hit_wears_every_piece_by_a_quarter_of_itself_and_at_least_one) {
    equipment_t equipment;
    equipment_start(&equipment);
    equipment.armor[0] = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    equipment.armor[3] = item_stack_make(ITEM_ID_IRON_BOOTS, 1u);
    equipment.armor[3].wear = 10u;
    /* 4 points. */
    float32_t left = equipment_armor_hurt(&equipment, 1.0f32);
    CHECK_NEAR((float64_t)left, (float64_t)equipment_armor_absorb(4u, 0u, 1.0f32), IE_DAMAGE_EPSILON);
    CHECK_EQ_U64(equipment.armor[0].wear, 1u);
    CHECK_EQ_U64(equipment.armor[3].wear, 11u);
    (void)equipment_armor_hurt(&equipment, 3.9f32); /* still one */
    CHECK_EQ_U64(equipment.armor[0].wear, 2u);
    (void)equipment_armor_hurt(&equipment, 8.0f32); /* 2 */
    CHECK_EQ_U64(equipment.armor[0].wear, 4u);
    (void)equipment_armor_hurt(&equipment, 40.0f32); /* 10 */
    CHECK_EQ_U64(equipment.armor[0].wear, 14u);
    CHECK_EQ_U64(equipment.armor[3].wear, 24u);
    CHECK(item_stack_is_empty(&equipment.armor[1]));
    /* Nothing comes of no hit. */
    left = equipment_armor_hurt(&equipment, 0.0f32);
    CHECK_NEAR((float64_t)left, 0.0f64, IE_DAMAGE_EPSILON);
    (void)equipment_armor_hurt(&equipment, -1.0f32);
    CHECK_EQ_U64(equipment.armor[0].wear, 14u);
}

TEST(a_piece_breaks_on_its_last_use_after_the_hit_it_helped_to_stop) {
    equipment_t equipment;
    equipment_start(&equipment);
    equipment.armor[0] = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    equipment.armor[0].wear = 164u;
    equipment.armor[1] = item_stack_make(ITEM_ID_IRON_CHESTPLATE, 1u);
    const float32_t left = equipment_armor_hurt(&equipment, 1.0f32);
    /* 8 points stopped this one: the helmet was still on. */
    CHECK_NEAR((float64_t)left, (float64_t)equipment_armor_absorb(8u, 0u, 1.0f32), IE_DAMAGE_EPSILON);
    CHECK(item_stack_is_empty(&equipment.armor[0]));
    CHECK(item_stack_is_valid(&equipment.armor[0]));
    CHECK_EQ_U64(equipment.armor_broken_count, 1u);
    CHECK_EQ_U64(equipment.armor[1].wear, 1u);
    CHECK_EQ_U64(equipment_armor_points(&equipment), 6u);

    /* A hit past anything that can be worn takes all of it at once, with nothing overflowing. */
    (void)equipment_armor_hurt(&equipment, 1.0e30f32);
    CHECK(item_stack_is_empty(&equipment.armor[1]));
    CHECK_EQ_U64(equipment.armor_broken_count, 2u);
    /* Nothing worn: it goes through whole, and nothing happens. */
    CHECK_NEAR((float64_t)equipment_armor_hurt(&equipment, 6.0f32), 6.0f64, IE_DAMAGE_EPSILON);
    CHECK_EQ_U64(equipment.armor_broken_count, 2u);
}

/* ---- At random -------------------------------------------------------------------------------------------------------- */

TEST(at_random_every_stack_stays_valid_and_every_item_is_accounted_for) {
    inventory_t inventory;
    arena_t arena;
    ie_inventory(t, &inventory, &arena);
    equipment_t equipment;
    equipment_start(&equipment);
    item_stack_t cursor = {0};
    uint16_t wearing[ITEM_COUNT];
    uint32_t wearing_count = 0u;
    for (uint32_t item = 1u; item < ITEM_COUNT; item++) {
        if (item_defs[item].durability != 0u) {
            wearing[wearing_count++] = (uint16_t)item;
        }
    }
    uint64_t state = 0x5EED5EEDu;
    uint64_t given = 0u;
    uint64_t went = 0u;
    for (uint32_t step = 0u; step < IE_RANDOM_STEPS; step++) {
        const uint64_t roll = ie_random(&state);
        const uint32_t which = (uint32_t)(roll >> 32u);
        switch (roll % 9u) {
        case 0u: {
            item_stack_t stack = item_stack_make(wearing[which % wearing_count], 1u);
            stack.wear = (uint16_t)((which >> 8u) % item_defs[stack.item].durability);
            given += inventory_add_stack(&inventory, &stack);
            break;
        }
        case 1u:
            inventory_select(&inventory, which % INVENTORY_HOTBAR_SLOTS);
            break;
        case 2u:
            (void)equipment_tool_block_broken(&equipment, &inventory, (uint16_t)(which % BLOCK_COUNT));
            break;
        case 3u:
            (void)equipment_tool_attacked(&equipment, &inventory);
            break;
        case 4u:
            (void)equipment_armor_wear_held(&equipment, &inventory);
            break;
        case 5u:
            (void)equipment_armor_quick_move_in(&equipment, &inventory, which % (INVENTORY_SLOTS + 1u));
            break;
        case 6u:
            (void)equipment_armor_quick_move_out(&equipment, &inventory, which % (EQUIPMENT_ARMOR_SLOTS + 1u));
            break;
        case 7u:
            (void)equipment_armor_hurt(&equipment, (float32_t)(which % 400u) / 10.0f32);
            break;
        default:
            if ((which & 256u) != 0u) {
                (void)equipment_armor_click(&equipment, which % (EQUIPMENT_ARMOR_SLOTS + 1u), &cursor);
            } else if (cursor.count != 0u) {
                /* Back through the way a screen would: this is the item leaving the game. */
                went += cursor.count;
                cursor = (item_stack_t){0};
            }
            break;
        }
        uint64_t worn = 0u;
        for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
            const item_stack_t *const stack = &equipment.armor[slot];
            worn += stack->count;
            if (!item_stack_is_valid(stack) || (stack->count != 0u && equipment_armor_slot_of(stack->item) != slot)) {
                CHECK(false);
            }
        }
        if (!item_stack_is_valid(&cursor) || !ie_books_balance(&inventory)) {
            CHECK(false);
        }
        const uint64_t held = inventory_items_held(&inventory);
        const uint64_t all =
            held + worn + cursor.count + equipment.tools_broken_count + equipment.armor_broken_count + went;
        if (all != given) {
            REQUIRE(CHECK_EQ_U64(all, given));
        }
    }
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        CHECK(item_stack_is_valid(inventory_slot_read(&inventory, slot)));
    }
    /* The walk has really been through all of it. */
    CHECK(equipment.tools_broken_count > 0u);
    CHECK(equipment.armor_broken_count > 0u);
    CHECK(went > 0u);
}

TEST(the_same_steps_give_the_same_bytes_and_no_heap) {
    equipment_t results[2];
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t run = 0u; run < 2u; run++) {
        inventory_t inventory;
        arena_t arena;
        ie_inventory(t, &inventory, &arena);
        equipment_start(&results[run]);
        uint64_t state = 77u;
        for (uint32_t i = 0u; i < 20000u; i++) {
            const uint64_t roll = ie_random(&state);
            if (roll % 11u == 0u) {
                ie_put(&inventory, 0u, ITEM_ID_IRON_PICKAXE, 0u);
                ie_put(&inventory, 1u, ITEM_ID_IRON_HELMET, 0u);
            }
            inventory_select(&inventory, (uint32_t)(roll >> 40u) % 2u);
            (void)equipment_armor_wear_held(&results[run], &inventory);
            (void)equipment_tool_block_broken(&results[run], &inventory, BLOCK_ID_STONE);
            (void)equipment_armor_hurt(&results[run], 12.0f32);
        }
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    CHECK_MEM_EQ(&results[0], &results[1], sizeof(results[0]));
}

/* ---- The Owner -------------------------------------------------------------------------------------------------------- */

TEST(item_open_starts_with_nothing_worn_and_item_close_forgets_it) {
    item_t *const item = test_world_aligned(t, sizeof(item_t));
    REQUIRE(item_init(item));
    item_open(item);
    CHECK_EQ_U64(equipment_armor_points(&item->equipment), 0u);
    item->equipment.armor[0] = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    item->equipment.tools_broken_count = 3u;
    CHECK_EQ_U64(equipment_armor_points(&item->equipment), 2u);
    item_close(item);
    CHECK(item_stack_is_empty(&item->equipment.armor[0]));
    item_open(item);
    CHECK_EQ_U64(equipment_armor_points(&item->equipment), 0u);
    CHECK_EQ_U64(item->equipment.tools_broken_count, 0u);
    item_close(item);
    item_deinit(item);
}

/* ---- A real game ------------------------------------------------------------------------------------------------------ */

typedef struct ie_game_t {
    game_t *game;             /* 0 */
    player_t *player;         /* 8 */
    item_t *item;             /* 16 */
    display_buffer_t *target; /* 24 */
    test_context_t *context;  /* 32 */
} ie_game_t;
static_assert(sizeof(ie_game_t) == 40);

static bool ie_landed_on(const ie_game_t *rig, int32_t z) {
    const player_t *const player = rig->player;
    return player->movement.on_ground != 0u && player->target.hit.result == RAYCAST_RESULT_HIT &&
           player->target.hit.cell[2] == z;
}

/* One frame of the main thread, with these buttons. */
static void ie_frame(const ie_game_t *rig, bool attack, bool place) {
    test_context_t *const t = rig->context;
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    game_walk_send(rig->game, &none, false, false, time_now_ns());
    game_hands_send(rig->game, attack, place);
    CHECK(game_loop_frame(rig->game, &none, 0.0f32, rig->target));
    time_sleep_ns(4000000);
}

/* Frames with nothing held, for this long. */
static void ie_idle(const ie_game_t *rig, int64_t ns) {
    const int64_t until = time_now_ns() + ns;
    while (time_now_ns() < until) {
        ie_frame(rig, false, false);
    }
}

static bool ie_wait_landed(const ie_game_t *rig, int32_t z) {
    const int64_t give_up = time_now_ns() + 30000000000;
    while (time_now_ns() < give_up && !ie_landed_on(rig, z)) {
        ie_frame(rig, false, false);
    }
    return ie_landed_on(rig, z);
}

/* The left button, until one more block has gone. How many ticks that took comes back through ticks. */
static bool ie_mine_one(const ie_game_t *rig, uint64_t *ticks) {
    const uint64_t mined = rig->player->interaction.mined_count;
    const uint64_t start = rig->player->tick_count;
    const int64_t give_up = time_now_ns() + 30000000000;
    while (time_now_ns() < give_up && rig->player->interaction.mined_count == mined) {
        ie_frame(rig, true, false);
    }
    *ticks = rig->player->tick_count - start;
    ie_frame(rig, false, false);
    return rig->player->interaction.mined_count == mined + 1u;
}

static bool ie_hotbar(const ie_game_t *rig, uint32_t slot) {
    item_hotbar_send(rig->item, slot);
    const int64_t give_up = time_now_ns() + 5000000000;
    while (time_now_ns() < give_up && rig->item->inventory.selected != slot) {
        ie_frame(rig, false, false);
    }
    return rig->item->inventory.selected == slot;
}

/* Everything through game_sim_tick's road, down a shaft built for it: dirt on stone, with leaves further down, straight under the body. */
TEST(in_a_real_game_tools_mine_and_wear_and_break_a_hoe_tills_and_armor_goes_on) {
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
    item_t *const item = test_world_aligned(t, sizeof(item_t));
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
    REQUIRE(item_init(item));
    player_init(player, physics);
    game_init(game, world, terrain, render, job, texture, topology);
    game_player_set(game, player);
    game_entity_set(game, entity);
    game_item_set(game, item);
    static const game_config_t small = {.render_distance = 5u, .width = IE_WIDTH, .height = IE_HEIGHT, .show_date = 0u};
    REQUIRE(game_open(game, &small));
    REQUIRE(game_settle(game));

    /* The ground where it starts, whatever that is, and a shaft of what this needs. */
    const int32_t x = (int32_t)floor(player->movement.position[0]);
    const int32_t y = (int32_t)floor(player->movement.position[1]);
    int32_t top = (int32_t)floor(player->movement.position[2]) + 40;
    uint16_t found = BLOCK_ID_AIR;
    while (top > -200) {
        REQUIRE(world_map_block_get(&world->map, x, y, top, &found) == WORLD_MAP_STATUS_OK);
        if ((block_defs[found].flags & BLOCK_FLAG_SOLID) != 0u) {
            break;
        }
        top--;
    }
    world_edit_t *const edit = &world->edit;
    for (int32_t dx = -1; dx <= 1; dx++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t z = top - 9; z <= top - 1; z++) {
                REQUIRE(world_edit_block_set(edit, x + dx, y + dy, z, BLOCK_ID_STONE) != WORLD_EDIT_STATUS_NO_MEMORY);
            }
            for (int32_t z = top + 1; z <= top + 3; z++) {
                REQUIRE(world_edit_block_set(edit, x + dx, y + dy, z, BLOCK_ID_AIR) != WORLD_EDIT_STATUS_NO_MEMORY);
            }
        }
    }
    REQUIRE(world_edit_block_set(edit, x, y, top, BLOCK_ID_DIRT) != WORLD_EDIT_STATUS_NO_MEMORY);
    REQUIRE(world_edit_block_set(edit, x, y, top - 3, BLOCK_ID_OAK_LEAVES) != WORLD_EDIT_STATUS_NO_MEMORY);
    /* So that these leaves have a reason to stay. */
    REQUIRE(world_edit_block_set(edit, x + 1, y, top - 3, BLOCK_ID_OAK_LOG) != WORLD_EDIT_STATUS_NO_MEMORY);
    player_spawn(player, (float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)top + 1.0f64);

    inventory_t *const inventory = &item->inventory;
    enum : uint32_t {
        SLOT_PICKAXE = 0u,
        SLOT_HELMET = 1u,
        SLOT_HOE = 2u,
        SLOT_SHEARS = 3u,
        SLOT_EMPTY = 4u,
        SLOT_LAST_LEGS = 5u,
    };
    ie_put(inventory, SLOT_PICKAXE, ITEM_ID_WOOD_PICKAXE, 0u);
    ie_put(inventory, SLOT_HELMET, ITEM_ID_IRON_HELMET, 0u);
    ie_put(inventory, SLOT_HOE, ITEM_ID_STONE_HOE, 0u);
    ie_put(inventory, SLOT_SHEARS, ITEM_ID_SHEARS, 0u);
    ie_put(inventory, SLOT_LAST_LEGS, ITEM_ID_WOOD_PICKAXE, 58u);
    const uint64_t bytes = post_xrgb_bytes(IE_WIDTH, IE_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = IE_WIDTH * 4u,
                               .width = IE_WIDTH,
                               .height = IE_HEIGHT};
    /* Straight down. */
    game->camera.pitch = -1.55f32;
    const ie_game_t rig = {.game = game, .player = player, .item = item, .target = &target, .context = t};
    REQUIRE(game_loop_start(game, nullptr));
    const player_interaction_t *const hands = &player->interaction;
    REQUIRE(ie_wait_landed(&rig, top));
    uint64_t ticks = 0u;

    /* The hoe: once, and then it has nothing more to do here however long the button stays down. */
    REQUIRE(ie_hotbar(&rig, SLOT_HOE));
    int64_t give_up = time_now_ns() + 10000000000;
    while (time_now_ns() < give_up && inventory_slot_read(inventory, SLOT_HOE)->wear == 0u) {
        ie_frame(&rig, false, true);
    }
    const int64_t held_until = time_now_ns() + 500000000;
    while (time_now_ns() < held_until) {
        ie_frame(&rig, false, true);
    }
    ie_idle(&rig, 100000000);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_HOE)->wear, 1u);
    CHECK_EQ_U64(hands->placed_count, 0u);

    /* The helmet: held a while, and it stays on. */
    REQUIRE(ie_hotbar(&rig, SLOT_HELMET));
    give_up = time_now_ns() + 10000000000;
    while (time_now_ns() < give_up && item->equipment.armor[0].count == 0u) {
        ie_frame(&rig, false, true);
    }
    const int64_t worn_until = time_now_ns() + 300000000;
    while (time_now_ns() < worn_until) {
        ie_frame(&rig, false, true);
    }
    ie_idle(&rig, 100000000);
    CHECK_EQ_U64(item->equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    CHECK(item_stack_is_empty(inventory_slot_read(inventory, SLOT_HELMET)));

    /* Wood on what the hoe left: not its own block, but it was in hand, and it comes out as dirt. */
    REQUIRE(ie_hotbar(&rig, SLOT_PICKAXE));
    CHECK(ie_mine_one(&rig, &ticks));
    CHECK_EQ_U64(hands->last_mined, BLOCK_ID_FARMLAND);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_PICKAXE)->wear, 1u);

    /* Wood on stone: its time, one use, and what stone gives. */
    REQUIRE(ie_wait_landed(&rig, top - 1));
    CHECK(ie_mine_one(&rig, &ticks));
    CHECK_EQ_U64(hands->last_mined, BLOCK_ID_STONE);
    CHECK(ticks + IE_TICKS_SLACK >= IE_STONE_TICKS_WOOD_PICKAXE);
    CHECK(ticks <= IE_STONE_TICKS_WOOD_PICKAXE + IE_TICKS_SLACK);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_PICKAXE)->wear, 2u);
    REQUIRE(ie_wait_landed(&rig, top - 2));
    ie_idle(&rig, 1000000000); /* a pickup waits 10 ticks */
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COBBLESTONE), 1u);

    /* A bare hand on stone: its far longer time, nothing worn, and nothing to show. */
    REQUIRE(ie_hotbar(&rig, SLOT_EMPTY));
    CHECK(ie_mine_one(&rig, &ticks));
    CHECK(ticks + IE_TICKS_SLACK >= IE_STONE_TICKS_HAND);
    CHECK(ticks <= IE_STONE_TICKS_HAND + IE_TICKS_SLACK);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_PICKAXE)->wear, 2u);

    /* Shears on leaves: the leaves themselves, and a use. */
    REQUIRE(ie_wait_landed(&rig, top - 3));
    REQUIRE(ie_hotbar(&rig, SLOT_SHEARS));
    CHECK(ie_mine_one(&rig, &ticks));
    CHECK_EQ_U64(hands->last_mined, BLOCK_ID_OAK_LEAVES);
    REQUIRE(ie_wait_landed(&rig, top - 4));
    ie_idle(&rig, 1000000000);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_SHEARS)->wear, 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_OAK_LEAVES), 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COBBLESTONE), 1u);

    /* Started with a pickaxe and carried on by hand: the block starts again, so all of a hand's time is still to come. */
    REQUIRE(ie_hotbar(&rig, SLOT_PICKAXE));
    const int64_t begun_until = time_now_ns() + 500000000;
    while (time_now_ns() < begun_until) {
        ie_frame(&rig, true, false);
    }
    item_hotbar_send(item, SLOT_EMPTY);
    ie_frame(&rig, true, false);
    CHECK(ie_mine_one(&rig, &ticks));
    CHECK(ticks + IE_TICKS_SLACK >= IE_STONE_TICKS_HAND);
    CHECK_EQ_U64(inventory_slot_read(inventory, SLOT_PICKAXE)->wear, 2u);

    /* Its last use: the block breaks and gives what it gives, and the tool is gone. */
    REQUIRE(ie_wait_landed(&rig, top - 5));
    REQUIRE(ie_hotbar(&rig, SLOT_LAST_LEGS));
    CHECK(ie_mine_one(&rig, &ticks));
    REQUIRE(ie_wait_landed(&rig, top - 6));
    ie_idle(&rig, 1000000000);
    CHECK(game_loop_stop(game));

    /* The threads have stopped: all is whole to read. */
    CHECK(item_stack_is_empty(inventory_slot_read(inventory, SLOT_LAST_LEGS)));
    CHECK_EQ_U64(item->equipment.tools_broken_count, 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COBBLESTONE), 2u);
    CHECK_EQ_U64(item->equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    CHECK_EQ_U64(equipment_armor_points(&item->equipment), 2u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_DIRT), 1u);
    CHECK_EQ_U64(hands->placed_count, 0u);
    CHECK(ie_books_balance(inventory));

    game_close(game);
    game_deinit(game);
    render_deinit(render);
    CHECK(gpu_bridge_exit_check(bridge));
    item_deinit(item);
    entity_deinit(entity);
    player_deinit(player);
    physics_deinit(physics);
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
}

/* ---- A real game by script: what tools and armor do with everything else ---------------------------------------------- */

typedef struct ie_script_game_t {
    gpu_bridge_t *bridge;           /* 0 */
    job_system_t *job;              /* 8 */
    world_t *world;                 /* 16 */
    terrain_t *terrain;             /* 24 */
    render_t *render;               /* 32 */
    texture_t *texture;             /* 40 */
    game_t *game;                   /* 48 */
    physics_t *physics;             /* 56 */
    player_t *player;               /* 64 */
    entity_t *entity;               /* 72 */
    item_t *item;                   /* 80 */
    thread_topology_t *topology;    /* 88 */
    display_headless_scene_t scene; /* 96 */
    display_script_state_t state;   /* 120 */
    uint32_t tick;                  /* 272 */
    int32_t pad_cell[3];            /* 276: the cell of the floor under its feet */
} ie_script_game_t;
static_assert(sizeof(ie_script_game_t) == 288);

/* A game on the real card with its own floor to work on, as the seed's ground falls away steeply where it starts. Ticks by hand. */
static bool ie_script_begin(test_context_t *t, ie_script_game_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->world = test_world_aligned(t, sizeof(world_t));
    rig->terrain = test_world_aligned(t, sizeof(terrain_t));
    rig->render = test_world_aligned(t, sizeof(render_t));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->game = test_world_aligned(t, sizeof(game_t));
    rig->physics = test_world_aligned(t, sizeof(physics_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(entity_init(rig->entity, rig->physics)) || !CHECK(item_init(rig->item)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_item_set(rig->game, rig->item);
    static const game_config_t small = {.render_distance = 5u, .width = IE_WIDTH, .height = IE_HEIGHT, .show_date = 0u};
    if (!CHECK(game_open(rig->game, &small)) || !CHECK(game_settle(rig->game))) {
        return false;
    }
    game_headless_scene(rig->game, &rig->scene);
    const int32_t pad_x = (int32_t)floor(rig->player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig->player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig->player->movement.position[2]);
    for (int32_t x = pad_x - 6; x <= pad_x + 6; x++) {
        for (int32_t y = pad_y - 6; y <= pad_y + 6; y++) {
            for (int32_t z = pad_z - 9; z <= pad_z + 8; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig->world->edit, x, y, z, z < pad_z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    rig->pad_cell[0] = pad_x;
    rig->pad_cell[1] = pad_y;
    rig->pad_cell[2] = pad_z - 1;
    return true;
}

static void ie_script_end(test_context_t *t, ie_script_game_t *rig) {
    game_close(rig->game);
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    item_deinit(rig->item);
    entity_deinit(rig->entity);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static void ie_script_key(ie_script_game_t *rig, uint32_t code, bool down) {
    const uint64_t bit = 1ull << (code % 64u);
    rig->state.down[code / 64u] = down ? rig->state.down[code / 64u] | bit : rig->state.down[code / 64u] & ~bit;
}

static bool ie_script_ticks(ie_script_game_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        rig->tick++;
        const display_headless_tick_t tick = {
            .events = nullptr, .state = &rig->state, .event_count = 0u, .tick = rig->tick};
        if (!rig->scene.tick(rig->scene.borrowed_context, &tick)) {
            return false;
        }
    }
    return true;
}

/* A press and a let go, one tick each. */
static bool ie_script_press(ie_script_game_t *rig, uint32_t code) {
    ie_script_key(rig, code, true);
    const bool down = ie_script_ticks(rig, 1u);
    ie_script_key(rig, code, false);
    return down && ie_script_ticks(rig, 1u);
}

TEST(in_a_real_game_armor_stops_hits_and_wears_and_what_goes_past_it_does_not_touch_it) {
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    player_survival_t *const survival = &rig->player->survival;
    item_t *const item = rig->item;
    REQUIRE(ie_script_ticks(rig, 10u));
    REQUIRE(survival->armor_points == 0.0f32);

    /* In hand, then on by the button: the same armor that goes on is what the next tick counts. */
    ie_put(&item->inventory, 0u, ITEM_ID_DIAMOND_CHESTPLATE, 0u);
    rig->game->camera.pitch = 1.5f32; /* the sky: only the armor has anything to do */
    REQUIRE(ie_script_press(rig, BTN_RIGHT));
    CHECK_EQ_U64(item->equipment.armor[1].item, ITEM_ID_DIAMOND_CHESTPLATE);
    CHECK(survival->armor_points == 8.0f32);
    CHECK(survival->armor_toughness == 2.0f32);

    /* A hit from outside: what comes through is the sum's, and a hit of 10 costs 2 uses. */
    const float32_t expected = player_survival_after_armor(10.0f32, 8.0f32, 2.0f32);
    CHECK(expected < 10.0f32);
    const float32_t before = survival->health;
    (void)player_survival_hurt(survival, 10.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    CHECK_NEAR((float64_t)survival->health, (float64_t)(before - expected), IE_DAMAGE_EPSILON);
    REQUIRE(ie_script_ticks(rig, 1u));
    CHECK_EQ_U64(item->equipment.armor[1].wear, 2u);
    REQUIRE(ie_script_ticks(rig, 1u));
    CHECK_EQ_U64(item->equipment.armor[1].wear, 2u); /* once for a hit */

    /* Being alight goes past armor: it hurts, and costs no uses. */
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_INVULNERABLE_TICKS));
    const uint64_t hits = survival->hurt_count;
    survival->burn_ticks = PLAYER_SURVIVAL_BURN_EVERY_TICKS;
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_BURN_EVERY_TICKS + 1u));
    CHECK_EQ_U64(survival->hurt_count, hits + 1u);
    CHECK_EQ_U64(item->equipment.armor[1].wear, 2u);

    /* Real lava, which armor does stop, wears it through the world's own road. */
    const int32_t x = (int32_t)floor(rig->player->movement.position[0]);
    const int32_t y = (int32_t)floor(rig->player->movement.position[1]);
    const int32_t z = (int32_t)floor(rig->player->movement.position[2]);
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_INVULNERABLE_TICKS));
    survival->health = 20.0f32;
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, z, BLOCK_ID_LAVA) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(ie_script_ticks(rig, 2u));
    CHECK(survival->hurt_count > hits + 1u);
    CHECK(item->equipment.armor[1].wear > 2u);
    CHECK(survival->health > 20.0f32 - 4.0f32);

    /* The piece that has run out comes off, and from the next tick nothing is worn. */
    (void)world_edit_block_set(&rig->world->edit, x, y, z, BLOCK_ID_AIR);
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_INVULNERABLE_TICKS));
    survival->burn_ticks = 0u;
    survival->health = 20.0f32;
    item->equipment.armor[1].wear = (uint16_t)(item_defs[ITEM_ID_DIAMOND_CHESTPLATE].durability - 1u);
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_INVULNERABLE_TICKS));
    (void)player_survival_hurt(survival, 1.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(ie_script_ticks(rig, 2u));
    CHECK(item_stack_is_empty(&item->equipment.armor[1]));
    CHECK_EQ_U64(item->equipment.armor_broken_count, 1u);
    CHECK(survival->armor_points == 0.0f32);
    CHECK(survival->armor_toughness == 0.0f32);
    ie_script_end(t, rig);
}

TEST(in_a_real_game_dying_spills_what_was_worn_and_all_of_it_comes_back_as_worn_as_it_was) {
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    player_survival_t *const survival = &rig->player->survival;
    item_t *const item = rig->item;
    inventory_t *const inventory = &item->inventory;
    REQUIRE(ie_script_ticks(rig, 10u));
    ie_put(inventory, 0u, ITEM_ID_IRON_PICKAXE, 37u);
    equipment_t *const equipment = &item->equipment;
    equipment->armor[0] = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    equipment->armor[0].wear = 11u;
    REQUIRE(ie_script_ticks(rig, 1u));

    /* A hit that kills costs its uses first, as any hit does. */
    (void)player_survival_hurt(survival, 100.0f32, PLAYER_SURVIVAL_CAUSE_ATTACK);
    REQUIRE(ie_script_ticks(rig, 1u));
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK(item_stack_is_empty(&equipment->armor[0]));
    CHECK(item_stack_is_empty(inventory_slot_read(inventory, 0u)));
    const item_drop_store_t *const drops = &rig->entity->item_drops;
    uint32_t found = 0u;
    for (uint32_t i = 0u; i < drops->count; i++) {
        const item_drop_t *const drop = &drops->borrowed_items[i];
        if (block_drop_items[drop->item].item == ITEM_ID_IRON_PICKAXE) {
            CHECK_EQ_U64(drop->wear, 37u);
            found++;
        } else if (block_drop_items[drop->item].item == ITEM_ID_IRON_HELMET) {
            CHECK_EQ_U64(drop->wear, 11u + 25u);
            found++;
        }
    }
    CHECK_EQ_U64(found, 2u);

    /* Back, and they are picked up as they were: one more use will not make the pick new. */
    REQUIRE(ie_script_ticks(rig, PLAYER_SURVIVAL_RESPAWN_WAIT_TICKS + 1u));
    REQUIRE(ie_script_press(rig, KEY_SPACE));
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK(survival->armor_points == 0.0f32);
    REQUIRE(ie_script_ticks(rig, 60u));
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_IRON_PICKAXE), 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_IRON_HELMET), 1u);
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        const item_stack_t *const stack = inventory_slot_read(inventory, slot);
        if (stack->item == ITEM_ID_IRON_PICKAXE) {
            CHECK_EQ_U64(stack->wear, 37u);
        } else if (stack->item == ITEM_ID_IRON_HELMET) {
            CHECK_EQ_U64(stack->wear, 11u + 25u);
        }
    }
    ie_script_end(t, rig);
}

TEST(in_a_real_game_a_chest_spills_a_worn_tool_as_worn_and_gold_mines_at_its_own_speed) {
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    item_t *const item = rig->item;
    inventory_t *const inventory = &item->inventory;
    REQUIRE(ie_script_ticks(rig, 10u));
    ie_put(inventory, 0u, ITEM_ID_GOLD_PICKAXE, 0u);
    /* Straight down, at the chest let into the floor. */
    rig->game->camera.pitch = -1.55f32;
    const int32_t *const pad = rig->pad_cell;
    REQUIRE(world_edit_block_set(&rig->world->edit, pad[0], pad[1], pad[2], BLOCK_ID_CHEST) ==
            WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(ie_script_ticks(rig, 3u));
    container_chest_t *const chest = container_at(&item->chests, pad[0], pad[1], pad[2]);
    REQUIRE(chest != nullptr);
    item_stack_t worn = item_stack_make(ITEM_ID_IRON_SHOVEL, 1u);
    worn.wear = 99u;
    CHECK_EQ_U64(container_add_stack(chest, &worn), 1u);
    REQUIRE(rig->player->target.hit.result == RAYCAST_RESULT_HIT);
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_CHEST);

    /* A chest is an axe's block: a pickaxe of any kind is a hand on it, at a hand's time. */
    const uint64_t mined = rig->player->interaction.mined_count;
    ie_script_key(rig, BTN_LEFT, true);
    for (uint32_t i = 0u; i < 200u && rig->player->interaction.mined_count == mined; i++) {
        REQUIRE(ie_script_ticks(rig, 1u));
    }
    ie_script_key(rig, BTN_LEFT, false);
    CHECK_EQ_U64(rig->player->interaction.mined_count, mined + 1u);
    REQUIRE(ie_script_ticks(rig, 60u));
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_IRON_SHOVEL), 1u);
    bool seen = false;
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        const item_stack_t *const stack = inventory_slot_read(inventory, slot);
        if (stack->item == ITEM_ID_IRON_SHOVEL) {
            CHECK_EQ_U64(stack->wear, 99u);
            seen = true;
        }
    }
    CHECK(seen);

    /* It has fallen through to the stone below, and stone goes in gold's own ticks, and is harvested. */
    const uint64_t now_mined = rig->player->interaction.mined_count;
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_STONE);
    const uint32_t cobble = inventory_count(inventory, ITEM_ID_COBBLESTONE);
    ie_script_key(rig, BTN_LEFT, true);
    uint32_t ticks = 0u;
    while (ticks < 200u && rig->player->interaction.mined_count == now_mined) {
        REQUIRE(ie_script_ticks(rig, 1u));
        ticks++;
    }
    ie_script_key(rig, BTN_LEFT, false);
    CHECK_EQ_U64(ticks, IE_STONE_TICKS_GOLD_PICKAXE);
    REQUIRE(ie_script_ticks(rig, 60u));
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_COBBLESTONE), cobble + 1u);
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->wear, 2u);
    ie_script_end(t, rig);
}

/* A trunk one cell ahead of the body, the way it looks with a yaw of 0, on the floor, with leaves on top. */
static void ie_tree(test_context_t *t, ie_script_game_t *rig, int32_t y, int32_t tall) {
    world_edit_t *const edit = &rig->world->edit;
    const int32_t x = rig->pad_cell[0] + 1, floor_above = rig->pad_cell[2] + 1;
    for (int32_t z = floor_above; z < floor_above + tall; z++) {
        CHECK(world_edit_block_set(edit, x, y, z, BLOCK_ID_OAK_LOG) == WORLD_EDIT_STATUS_CHANGED);
    }
    CHECK(world_edit_block_set(edit, x + 1, y, floor_above + tall - 1, BLOCK_ID_OAK_LEAVES) ==
          WORLD_EDIT_STATUS_CHANGED);
}

static uint32_t ie_logs_in(const ie_script_game_t *rig, int32_t y, int32_t tall) {
    uint32_t logs = 0u;
    const int32_t x = rig->pad_cell[0] + 1, floor_above = rig->pad_cell[2] + 1;
    for (int32_t z = floor_above; z < floor_above + tall; z++) {
        uint16_t block = BLOCK_ID_AIR;
        (void)world_map_block_get(&rig->world->map, x, y, z, &block);
        logs += block == BLOCK_ID_OAK_LOG ? 1u : 0u;
    }
    return logs;
}

/* Holds the left button until the hands have cut something. */
static bool ie_chop(ie_script_game_t *rig) {
    const uint64_t mined = rig->player->interaction.mined_count;
    ie_script_key(rig, BTN_LEFT, true);
    bool cut = false;
    for (uint32_t i = 0u; i < 400u && !cut; i++) {
        cut = ie_script_ticks(rig, 1u) && rig->player->interaction.mined_count != mined;
    }
    ie_script_key(rig, BTN_LEFT, false);
    return cut;
}

TEST(in_a_real_game_an_axe_fells_a_tree_wears_a_use_a_log_and_stops_when_it_breaks) {
    enum : int32_t { TALL = 14, CUT = 2 }; /* the eye is on the second log up, so one stays under it */
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    item_t *const item = rig->item;
    inventory_t *const inventory = &item->inventory;
    const uint32_t logs_before = inventory_count(inventory, ITEM_ID_OAK_LOG);
    rig->game->camera.yaw = 0.0f32;
    rig->game->camera.pitch = 0.0f32;
    const int32_t row = rig->pad_cell[1];
    ie_tree(t, rig, row, TALL);
    ie_put(inventory, 0u, ITEM_ID_WOOD_AXE, 0u);
    REQUIRE(ie_script_ticks(rig, 3u));
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_OAK_LOG);
    CHECK_EQ_I64(rig->player->target.hit.cell[2], rig->pad_cell[2] + CUT);

    REQUIRE(ie_chop(rig));
    REQUIRE(ie_script_ticks(rig, 4u));
    /* All of it but the one under the eye, a use for every log, and the axe still in hand. */
    CHECK_EQ_U64(ie_logs_in(rig, row, TALL), 1u);
    CHECK_EQ_U64(rig->game->borrowed_felling->felled_count, (uint64_t)(TALL - CUT));
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->wear, (uint64_t)(TALL - CUT + 1));
    CHECK_EQ_U64(rig->player->interaction.drops_lost_count, 0u);
    /* What fell is all there, on the ground or taken in by the body as it lands. The leaves go too, in their time. */
    REQUIRE(ie_script_ticks(rig, 120u));
    uint32_t on_ground = 0u;
    const item_drop_store_t *const drops = &rig->entity->item_drops;
    for (uint32_t i = 0u; i < drops->count; i++) {
        on_ground += drops->borrowed_items[i].item == item_stack_drop_row(ITEM_ID_OAK_LOG)
                         ? (uint32_t)drops->borrowed_items[i].count
                         : 0u;
    }
    CHECK(inventory_count(inventory, ITEM_ID_OAK_LOG) > logs_before);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_OAK_LOG) + on_ground, logs_before + (uint32_t)(TALL - CUT + 1));
    uint16_t leaf = BLOCK_ID_OAK_LEAVES;
    (void)world_map_block_get(&rig->world->map, rig->pad_cell[0] + 2, row, rig->pad_cell[2] + TALL, &leaf);
    CHECK_EQ_U64(leaf, BLOCK_ID_AIR);

    const uint64_t felled = rig->game->borrowed_felling->felled_count;
    /* Three uses left: the cut and two more, and it is gone with the rest of the tree still up. */
    const uint32_t broken_before = (uint32_t)item->equipment.tools_broken_count;
    CHECK_EQ_U64(inventory_selected_take(inventory, 1u), 1u);
    ie_put(inventory, 0u, ITEM_ID_WOOD_AXE, 56u);
    ie_tree(t, rig, row + 2, TALL);
    REQUIRE(world_edit_block_set(&rig->world->edit, rig->pad_cell[0] + 1, row, rig->pad_cell[2] + 1, BLOCK_ID_AIR) ==
            WORLD_EDIT_STATUS_CHANGED);
    /* Across to the second tree, over the second log of it. */
    rig->game->camera.yaw = 1.1071487f32;
    REQUIRE(ie_script_ticks(rig, 3u));
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_OAK_LOG);
    REQUIRE(ie_chop(rig));
    REQUIRE(ie_script_ticks(rig, 10u));
    CHECK_EQ_U64(rig->game->borrowed_felling->felled_count, felled + 2u);
    CHECK_EQ_U64(ie_logs_in(rig, row + 2, TALL), (uint32_t)TALL - 3u);
    CHECK(item_stack_is_empty(inventory_slot_read(inventory, 0u)));
    CHECK_EQ_U64(item->equipment.tools_broken_count, broken_before + 1u);
    CHECK(ie_books_balance(inventory));
    ie_script_end(t, rig);
}

TEST(in_a_real_game_a_sneaking_body_and_bare_hands_cut_one_log) {
    enum : int32_t { TALL = 8 };
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    inventory_t *const inventory = &rig->item->inventory;
    rig->game->camera.yaw = 0.0f32;
    rig->game->camera.pitch = 0.0f32;
    const int32_t row = rig->pad_cell[1];
    const uint64_t felled = rig->game->borrowed_felling->felled_count;
    /* The left shift, as it comes from a keyboard, and an axe in hand. */
    ie_tree(t, rig, row, TALL);
    ie_put(inventory, 0u, ITEM_ID_WOOD_AXE, 0u);
    ie_script_key(rig, KEY_LEFTSHIFT, true);
    REQUIRE(ie_script_ticks(rig, 6u));
    REQUIRE(rig->player->movement.crouched != 0u);
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_OAK_LOG);
    REQUIRE(ie_chop(rig));
    REQUIRE(ie_script_ticks(rig, 20u));
    CHECK_EQ_U64(ie_logs_in(rig, row, TALL), (uint32_t)TALL - 1u);
    CHECK_EQ_U64(rig->game->borrowed_felling->felled_count, felled);
    CHECK_EQ_U64(inventory_slot_read(inventory, 0u)->wear, 1u);
    ie_script_key(rig, KEY_LEFTSHIFT, false);
    /* Nothing in hand, on a tree of its own, across from the first as in the test above. */
    CHECK_EQ_U64(inventory_selected_take(inventory, 1u), 1u);
    ie_tree(t, rig, row + 2, TALL);
    REQUIRE(world_edit_block_set(&rig->world->edit, rig->pad_cell[0] + 1, row, rig->pad_cell[2] + 1, BLOCK_ID_AIR) ==
            WORLD_EDIT_STATUS_CHANGED);
    rig->game->camera.yaw = 1.1071487f32;
    REQUIRE(ie_script_ticks(rig, 6u));
    REQUIRE(rig->player->movement.crouched == 0u);
    REQUIRE(rig->player->target.hit.block == BLOCK_ID_OAK_LOG);
    REQUIRE(rig->player->target.hit.cell[1] == row + 2);
    REQUIRE(ie_chop(rig));
    REQUIRE(ie_script_ticks(rig, 20u));
    CHECK_EQ_U64(ie_logs_in(rig, row + 2, TALL), (uint32_t)TALL - 1u);
    CHECK_EQ_U64(rig->game->borrowed_felling->felled_count, felled);
    ie_script_end(t, rig);
}

TEST(a_press_that_wakes_or_brings_back_a_body_does_nothing_else) {
    ie_script_game_t *const rig = test_alloc(t, sizeof(ie_script_game_t));
    REQUIRE(ie_script_begin(t, rig));
    player_survival_t *const survival = &rig->player->survival;
    item_t *const item = rig->item;
    REQUIRE(ie_script_ticks(rig, 10u));
    /* Asleep, with armor in hand. Made so by hand, as beds are run_player_survival's. */
    ie_put(&item->inventory, 0u, ITEM_ID_IRON_HELMET, 0u);
    survival->state = PLAYER_SURVIVAL_SLEEPING;
    survival->state_ticks = 0u;
    survival->has_bed = 1u;
    survival->bed_cell[0] = rig->pad_cell[0];
    survival->bed_cell[1] = rig->pad_cell[1];
    survival->bed_cell[2] = rig->pad_cell[2];
    REQUIRE(world_edit_block_set(&rig->world->edit, rig->pad_cell[0], rig->pad_cell[1], rig->pad_cell[2],
                                 BLOCK_ID_BED) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(ie_script_press(rig, BTN_RIGHT));
    CHECK_EQ_U64(survival->state, PLAYER_SURVIVAL_ALIVE);
    CHECK(item_stack_is_empty(&item->equipment.armor[0]));
    CHECK_EQ_U64(inventory_slot_read(&item->inventory, 0u)->item, ITEM_ID_IRON_HELMET);
    /* The next press is an ordinary one. */
    rig->game->camera.pitch = 1.5f32;
    REQUIRE(ie_script_ticks(rig, 2u));
    REQUIRE(ie_script_press(rig, BTN_RIGHT));
    CHECK_EQ_U64(item->equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    ie_script_end(t, rig);
}

/* ---- The program ------------------------------------------------------------------------------------------------------ */

static void ie_temp_dir_remove(const char *dir) {
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
    char dir[] = "/tmp/mc_c_item_equipment_XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    char script[128];
    snprintf(script, sizeof(script), "%s/walk.script", dir);
    /* Nothing in hand and nothing worn: the right button and the left do what they always did. */
    static const char text[] = "1 move 0 500\n5 button right down\n10 button right up\n20 end\n";
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
    test_process_run(t, argv, IE_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "game: 0 armor points worn; 0 tools and 0 pieces of armor have broken");
    ie_temp_dir_remove(dir);
}

TEST_MAIN()
