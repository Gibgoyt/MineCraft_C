/* See README.md. Plain numbers and no memory: what is worn is a field of item_t, and what is in hand is the inventory's. */
#include "item/equipment/index.h"

#include "tables/blocks/index.h"

#include <string.h>

enum : uint32_t {
    /* The published numbers. A sword is made for hitting, and everything else that wears for something else. */
    EQUIPMENT_WEAR_MINING = 1u,
    EQUIPMENT_WEAR_SWORD_MINING = 2u,
    EQUIPMENT_WEAR_SWORD_ATTACK = 1u,
    EQUIPMENT_WEAR_OTHER_ATTACK = 2u,
    EQUIPMENT_WEAR_TILL = 1u,
    /* A piece of armor loses one use for each this many half hearts of a hit, and at least one. */
    EQUIPMENT_ARMOR_DAMAGE_PER_USE = 4u,
    /* No wear is more than this, whatever hit: nothing lasts longer. */
    EQUIPMENT_WEAR_MOST = UINT16_MAX,
};

/* Past this a hit wears as much as anything can. Below it, the hit fits a uint32_t. */
#define EQUIPMENT_DAMAGE_WEARS_MOST 262140.0f32
/* The published formula's numbers. */
#define EQUIPMENT_ABSORB_DAMAGE_DIVISOR 2.0f32
#define EQUIPMENT_ABSORB_TOUGHNESS_DIVISOR 4.0f32
#define EQUIPMENT_ABSORB_POINTS_FLOOR_DIVISOR 5.0f32
#define EQUIPMENT_ABSORB_WHOLE 25.0f32

static_assert((uint32_t)EQUIPMENT_ARMOR_SLOTS == (uint32_t)ITEM_ARMOR_SLOT_FEET);

void equipment_start(equipment_t *equipment) {
    memset(equipment, 0, sizeof(*equipment));
}

/* ---- Wear ------------------------------------------------------------------------------------------------------------- */

/* Whether this many uses would leave it with none. */
static bool equipment_breaks(const item_stack_t *stack, uint32_t uses) {
    return (uint32_t)stack->wear + uses >= (uint32_t)item_defs[stack->item].durability;
}

void equipment_tool_held(const inventory_t *inventory, uint8_t *out_tool, uint8_t *out_tier) {
    const item_def_t *const def = &item_defs[inventory_selected(inventory)->item];
    *out_tool = def->tool;
    *out_tier = def->harvest_tier;
}

uint32_t equipment_wear_of_block(uint16_t item, uint16_t block) {
    const uint8_t tool = item_defs[item].tool;
    if (tool == BLOCK_TOOL_NONE) {
        return 0u;
    }
    if (block_defs[block].hardness == 0.0f32 && !(tool == BLOCK_TOOL_SHEARS && block_defs[block].tool == tool)) {
        return 0u;
    }
    return tool == BLOCK_TOOL_SWORD ? EQUIPMENT_WEAR_SWORD_MINING : EQUIPMENT_WEAR_MINING;
}

uint32_t equipment_wear_of_attack(uint16_t item) {
    const uint8_t tool = item_defs[item].tool;
    if (tool == BLOCK_TOOL_NONE || tool == BLOCK_TOOL_SHEARS) {
        return 0u;
    }
    return tool == BLOCK_TOOL_SWORD ? EQUIPMENT_WEAR_SWORD_ATTACK : EQUIPMENT_WEAR_OTHER_ATTACK;
}

uint32_t equipment_uses_left(const item_stack_t *stack) {
    const uint32_t durability = item_defs[stack->item].durability;
    return durability == 0u ? 0u : durability - (uint32_t)stack->wear;
}

uint32_t equipment_tool_speed_milli(uint16_t item) {
    /* A sword's speed is a sword's, of whatever it is made, and shears have no tier. The rest go by their tier's file. */
    const item_def_t *const def = &item_defs[item];
    if (def->tier == ITEM_TIER_NONE || def->tool == BLOCK_TOOL_SWORD || def->tool == BLOCK_TOOL_NONE ||
        def->tool == BLOCK_TOOL_SHEARS) {
        return 0u;
    }
    return item_tiers[def->tier].speed_milli;
}

float32_t equipment_attack_damage(const item_stack_t *stack) {
    const float32_t damage = item_defs[stack->item].damage;
    return damage > EQUIPMENT_HAND_DAMAGE ? damage : EQUIPMENT_HAND_DAMAGE;
}

bool equipment_tool_wear(equipment_t *equipment, inventory_t *inventory, uint32_t uses) {
    const item_stack_t *const held = inventory_selected(inventory);
    if (uses == 0u || held->count == 0u || item_defs[held->item].durability == 0u) {
        return false;
    }
    if (equipment_breaks(held, uses)) {
        (void)inventory_selected_take(inventory, 1u);
        equipment->tools_broken_count++;
        return true;
    }
    inventory_slot(inventory, inventory->selected)->wear = (uint16_t)((uint32_t)held->wear + uses);
    return false;
}

bool equipment_tool_block_broken(equipment_t *equipment, inventory_t *inventory, uint16_t block) {
    return equipment_tool_wear(equipment, inventory,
                               equipment_wear_of_block(inventory_selected(inventory)->item, block));
}

bool equipment_tool_attacked(equipment_t *equipment, inventory_t *inventory) {
    return equipment_tool_wear(equipment, inventory, equipment_wear_of_attack(inventory_selected(inventory)->item));
}

/* ---- What tools do ---------------------------------------------------------------------------------------------------- */

uint16_t equipment_own_drop(uint16_t item, uint16_t block) {
    if (item_defs[item].tool != BLOCK_TOOL_SHEARS || block_defs[block].tool != BLOCK_TOOL_SHEARS) {
        return ITEM_ID_NONE;
    }
    /* A walk, but only when shears have just broken something. */
    for (uint32_t row = 1u; row < ITEM_COUNT; row++) {
        if (item_defs[row].block == block) {
            return (uint16_t)row;
        }
    }
    return ITEM_ID_NONE;
}

uint16_t equipment_till_result(uint16_t block) {
    if (block == BLOCK_ID_DIRT || block == BLOCK_ID_GRASS_BLOCK) {
        return BLOCK_ID_FARMLAND;
    }
    return block == BLOCK_ID_COARSE_DIRT ? BLOCK_ID_DIRT : BLOCK_ID_AIR;
}

bool equipment_hoe_till(equipment_t *equipment, inventory_t *inventory, world_edit_t *edit, int32_t x, int32_t y,
                        int32_t z) {
    if (item_defs[inventory_selected(inventory)->item].tool != BLOCK_TOOL_HOE) {
        return false;
    }
    uint16_t block = BLOCK_ID_AIR, above = BLOCK_ID_STONE;
    if (world_map_block_get(edit->borrowed_map, x, y, z, &block) != WORLD_MAP_STATUS_OK ||
        world_map_block_get(edit->borrowed_map, x, y, z + 1, &above) != WORLD_MAP_STATUS_OK || above != BLOCK_ID_AIR) {
        return false;
    }
    const uint16_t result = equipment_till_result(block);
    /* With the section pool full it stays as it was, and costs nothing: the button is still down, so the next tick tries again. */
    if (result == BLOCK_ID_AIR || world_edit_block_set(edit, x, y, z, result) != WORLD_EDIT_STATUS_CHANGED) {
        return false;
    }
    (void)equipment_tool_wear(equipment, inventory, EQUIPMENT_WEAR_TILL);
    return true;
}

void equipment_use_tick(equipment_t *equipment, inventory_t *inventory, world_edit_t *edit, bool held,
                        const int32_t *borrowed_cell) {
    const bool went_down = held && equipment->use_held == 0u;
    equipment->use_held = held ? 1u : 0u;
    if (equipment->use_delay != 0u) {
        equipment->use_delay--;
    }
    if (!held) {
        return;
    }
    if (went_down) {
        /* On the press alone: held, a swap would go back and forth. */
        (void)equipment_armor_wear_held(equipment, inventory);
    }
    if (equipment->use_delay == 0u && borrowed_cell != nullptr &&
        equipment_hoe_till(equipment, inventory, edit, borrowed_cell[0], borrowed_cell[1], borrowed_cell[2])) {
        equipment->use_delay = EQUIPMENT_USE_EVERY_TICKS;
    }
}

/* ---- Armor ------------------------------------------------------------------------------------------------------------ */

uint32_t equipment_armor_slot_of(uint16_t item) {
    const uint8_t slot = item < ITEM_COUNT ? item_defs[item].armor_slot : (uint8_t)ITEM_ARMOR_SLOT_NONE;
    return slot == ITEM_ARMOR_SLOT_NONE ? EQUIPMENT_NOT_ARMOR : (uint32_t)slot - (uint32_t)ITEM_ARMOR_SLOT_HEAD;
}

const item_stack_t *equipment_armor_worn(const equipment_t *equipment, uint32_t slot) {
    return slot < EQUIPMENT_ARMOR_SLOTS ? &equipment->armor[slot] : nullptr;
}

bool equipment_armor_click(equipment_t *equipment, uint32_t slot, item_stack_t *cursor) {
    if (slot >= EQUIPMENT_ARMOR_SLOTS) {
        return false;
    }
    if (cursor->count == 0u) {
        if (equipment->armor[slot].count == 0u) {
            return false;
        }
    } else if (equipment_armor_slot_of(cursor->item) != slot) {
        return false;
    }
    item_stack_swap(&equipment->armor[slot], cursor);
    return true;
}

bool equipment_armor_wear_held(equipment_t *equipment, inventory_t *inventory) {
    const uint32_t selected = inventory->selected;
    const item_stack_t *const held = inventory_slot_read(inventory, selected);
    const uint32_t slot = equipment_armor_slot_of(held->item);
    if (held->count == 0u || slot == EQUIPMENT_NOT_ARMOR) {
        return false;
    }
    if (equipment->armor[slot].count == 0u) {
        return inventory_slot_move_out(inventory, selected, &equipment->armor[slot], 1u) == 1u;
    }
    /* One for one, since armor stacks to one: the inventory holds as many as before, and its books still add up. */
    item_stack_swap(inventory_slot(inventory, selected), &equipment->armor[slot]);
    return true;
}

uint32_t equipment_armor_quick_move_in(equipment_t *equipment, inventory_t *inventory, uint32_t inventory_slot_number) {
    if (inventory_slot_number >= INVENTORY_SLOTS) {
        return 0u;
    }
    const item_stack_t *const stack = inventory_slot_read(inventory, inventory_slot_number);
    const uint32_t slot = equipment_armor_slot_of(stack->item);
    if (stack->count == 0u || slot == EQUIPMENT_NOT_ARMOR || equipment->armor[slot].count != 0u) {
        return 0u;
    }
    return inventory_slot_move_out(inventory, inventory_slot_number, &equipment->armor[slot], 1u);
}

uint32_t equipment_armor_quick_move_out(equipment_t *equipment, inventory_t *inventory, uint32_t slot) {
    if (slot >= EQUIPMENT_ARMOR_SLOTS) {
        return 0u;
    }
    return inventory_add_stack(inventory, &equipment->armor[slot]);
}

uint32_t equipment_armor_points(const equipment_t *equipment) {
    uint32_t points = 0u;
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        points += item_defs[equipment->armor[slot].item].armor_points;
    }
    return points;
}

uint32_t equipment_armor_toughness(const equipment_t *equipment) {
    uint32_t toughness = 0u;
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        toughness += item_defs[equipment->armor[slot].item].armor_toughness;
    }
    return toughness;
}

float32_t equipment_armor_absorb(uint32_t points, uint32_t toughness, float32_t damage) {
    if (!(damage > 0.0f32)) {
        return 0.0f32;
    }
    const float32_t have = (float32_t)points;
    const float32_t least = have / EQUIPMENT_ABSORB_POINTS_FLOOR_DIVISOR;
    const float32_t divisor =
        EQUIPMENT_ABSORB_DAMAGE_DIVISOR + (float32_t)toughness / EQUIPMENT_ABSORB_TOUGHNESS_DIVISOR;
    const float32_t against = have - damage / divisor;
    const float32_t share = against > least ? against : least;
    const float32_t most = (float32_t)EQUIPMENT_ARMOR_POINTS_MOST;
    return damage * (1.0f32 - (share < most ? share : most) / EQUIPMENT_ABSORB_WHOLE);
}

float32_t equipment_armor_hurt(equipment_t *equipment, float32_t damage) {
    const float32_t left =
        equipment_armor_absorb(equipment_armor_points(equipment), equipment_armor_toughness(equipment), damage);
    if (!(damage > 0.0f32)) {
        return left;
    }
    /* Whole half hearts, rounded down. */
    uint32_t uses = EQUIPMENT_WEAR_MOST;
    if (damage < EQUIPMENT_DAMAGE_WEARS_MOST) {
        uses = (uint32_t)damage / EQUIPMENT_ARMOR_DAMAGE_PER_USE;
    }
    uses = uses == 0u ? 1u : uses;
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        item_stack_t *const stack = &equipment->armor[slot];
        if (stack->count == 0u) {
            continue;
        }
        if (equipment_breaks(stack, uses)) {
            *stack = (item_stack_t){0};
            equipment->armor_broken_count++;
        } else {
            stack->wear = (uint16_t)((uint32_t)stack->wear + uses);
        }
    }
    return left;
}
