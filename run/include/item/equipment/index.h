/* See run/src/item/equipment/README.md. */
#ifndef MC_RUN_ITEM_EQUIPMENT_INDEX_H
#define MC_RUN_ITEM_EQUIPMENT_INDEX_H

#include "item/inventory/index.h"
#include "item/stack/index.h"
#include "types/index.h"
#include "world/edit/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Head, chest, legs and feet, as ITEM_ARMOR_SLOT_* less one. */
    EQUIPMENT_ARMOR_SLOTS = 4u,
    /* What equipment_armor_slot_of says of what goes on no slot. */
    EQUIPMENT_NOT_ARMOR = UINT32_MAX,
    /* The published numbers: with the button held, a hoe works this often; what armor stops at most. */
    EQUIPMENT_USE_EVERY_TICKS = 4u,
    EQUIPMENT_ARMOR_POINTS_MOST = 20u,
};

/* A bare hand's hit, in half hearts. */
#define EQUIPMENT_HAND_DAMAGE 1.0f32

/* What a player wears, and how a player's tools have gone. Plain numbers, so that the same world and buttons give the same bytes. The
 * sim's. */
typedef struct equipment_t {
    item_stack_t armor[EQUIPMENT_ARMOR_SLOTS]; /* 0 */
    uint64_t tools_broken_count;               /* 32 */
    uint32_t armor_broken_count;               /* 40 */
    uint8_t use_held;                          /* 44: the button was down last tick */
    uint8_t use_delay;                         /* 45: ticks until the hoe works again */
    uint8_t _pad0[2];                          /* 46 */
} equipment_t;
static_assert(sizeof(equipment_t) == 48);
static_assert(alignof(equipment_t) == 8);

/* Nothing worn and nothing under way. */
void equipment_start(equipment_t *equipment);

/* ---- Tools ---- */

/* What mining with the slot in hand counts as: a block_tool_t and a block_tier_t. A hand, and anything that is no tool, is none and none. */
void equipment_tool_held(const inventory_t *inventory, uint8_t *out_tool, uint8_t *out_tier);

/* Uses this item loses by breaking this block. None for what does not wear, or for what breaks at a touch, unless it is shears on their
 * own. */
uint32_t equipment_wear_of_block(uint16_t item, uint16_t block);

/* Uses this item loses by hitting something. */
uint32_t equipment_wear_of_attack(uint16_t item);

/* Uses left in a stack, for a bar on a slot. 0 for what does not wear. */
uint32_t equipment_uses_left(const item_stack_t *stack);

/* What this item mines its own blocks at, in thousandths, if that is not its tier's: gold, which breaks what wood breaks, at 12. 0 for the
 * rest. */
uint32_t equipment_tool_speed_milli(uint16_t item);

/* What a hit with this does, in half hearts. */
float32_t equipment_attack_damage(const item_stack_t *stack);

/* The slot in hand loses this many uses. One with none left breaks, and is gone, by the inventory's books. Returns whether it broke. */
bool equipment_tool_wear(equipment_t *equipment, inventory_t *inventory, uint32_t uses);

/* The block the slot in hand just broke, and something it just hit. Each returns whether that broke it. */
bool equipment_tool_block_broken(equipment_t *equipment, inventory_t *inventory, uint16_t block);
bool equipment_tool_attacked(equipment_t *equipment, inventory_t *inventory);

/* What shears give on top of a block's own drops: the item that places the block, if it is their block and has one. ITEM_ID_NONE
 * otherwise. */
uint16_t equipment_own_drop(uint16_t item, uint16_t block);

/* What a hoe makes of this block. BLOCK_ID_AIR if nothing. */
uint16_t equipment_till_result(uint16_t block);

/* With a hoe in hand, tills this cell, if it can be and has air above it, for one use. Returns whether it did. */
bool equipment_hoe_till(equipment_t *equipment, inventory_t *inventory, world_edit_t *edit, int32_t x, int32_t y,
                        int32_t z);

/* One tick of the right button, called every tick on the thread holding the map. Armor in hand goes on when the button goes down; a hoe
 * tills the aimed-at cell when it goes down and every EQUIPMENT_USE_EVERY_TICKS while it is held. borrowed_cell is what is aimed at, three
 * numbers, or nullptr for nothing. */
void equipment_use_tick(equipment_t *equipment, inventory_t *inventory, world_edit_t *edit, bool held,
                        const int32_t *borrowed_cell);

/* ---- Armor ---- */

/* Which slot this item goes on, or EQUIPMENT_NOT_ARMOR. */
uint32_t equipment_armor_slot_of(uint16_t item);

/* What is worn on a slot. nullptr for a slot that does not exist. */
const item_stack_t *equipment_armor_worn(const equipment_t *equipment, uint32_t slot);

/* A screen's click on an armor slot, with what the pointer carries. It swaps if the pointer is empty or carries what goes there, and
 * otherwise does nothing. Returns whether anything moved. */
bool equipment_armor_click(equipment_t *equipment, uint32_t slot, item_stack_t *cursor);

/* What is in hand goes on, swapping with what was there, which comes into the hand. Returns whether anything moved. */
bool equipment_armor_wear_held(equipment_t *equipment, inventory_t *inventory);

/* Shift-clicks: armor from an inventory slot on to its own slot if that is free; and from an armor slot into the inventory if there is
 * room. Each returns how many moved. */
uint32_t equipment_armor_quick_move_in(equipment_t *equipment, inventory_t *inventory, uint32_t inventory_slot_number);
uint32_t equipment_armor_quick_move_out(equipment_t *equipment, inventory_t *inventory, uint32_t slot);

/* All that is worn, in half shirts. */
uint32_t equipment_armor_points(const equipment_t *equipment);

/* The toughness of all that is worn: what keeps hard hits from getting through as easily. */
uint32_t equipment_armor_toughness(const equipment_t *equipment);

/* What is left of a hit against this many points and this toughness. Nothing here wears. */
float32_t equipment_armor_absorb(uint32_t points, uint32_t toughness, float32_t damage);

/* A hit armor stops: what is left of it, and every piece worn loses uses for it. What goes past armor, like falling, does not call this.
 * What has no uses left breaks and comes off. */
float32_t equipment_armor_hurt(equipment_t *equipment, float32_t damage);

#endif
