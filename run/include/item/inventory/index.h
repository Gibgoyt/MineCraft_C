/* See run/src/item/inventory/README.md. */
#ifndef MC_RUN_ITEM_INVENTORY_INDEX_H
#define MC_RUN_ITEM_INVENTORY_INDEX_H

#include "arena/index.h"
#include "item/stack/index.h"

#include <stdint.h>

enum : uint32_t {
    /* The first INVENTORY_HOTBAR_SLOTS of these are the hotbar, and the rest the three rows above it. */
    INVENTORY_SLOTS = 36u,
    INVENTORY_HOTBAR_SLOTS = 9u,
};

/* What an inventory takes from its arena, with an alignment's slack. */
enum : uint64_t {
    INVENTORY_BYTES = (uint64_t)INVENTORY_SLOTS * sizeof(item_stack_t) + alignof(item_stack_t),
};

/* What a player carries. Plain numbers, so that the same world and buttons give the same bytes. The sim's. */
typedef struct inventory_t {
    item_stack_t *borrowed_slots; /* 0: INVENTORY_SLOTS */
    /* Counted in items, so that these add up: added is removed, plus what is held. Only through the functions below. */
    uint64_t added_count;   /* 8 */
    uint64_t removed_count; /* 16 */
    uint64_t refused_count; /* 24: had no room, and stayed where it was */
    uint8_t selected;       /* 32: the hotbar slot in hand */
    uint8_t _pad0[7];       /* 33 */
} inventory_t;
static_assert(sizeof(inventory_t) == 40);
static_assert(alignof(inventory_t) == 8);

/* Empty, from the arena, with the first hotbar slot in hand. */
void inventory_start(inventory_t *inventory, arena_t *arena);

/* A slot, by number. Writing to one directly is how a screen moves stacks, and goes past the books above. */
item_stack_t *inventory_slot(inventory_t *inventory, uint32_t slot);
const item_stack_t *inventory_slot_read(const inventory_t *inventory, uint32_t slot);

/* What is in hand. */
const item_stack_t *inventory_selected(const inventory_t *inventory);

/* Which hotbar slot is in hand. A slot that is not on the hotbar changes nothing. */
void inventory_select(inventory_t *inventory, uint32_t slot);

/* By this many slots to the right, or to the left if negative, round the ends. */
void inventory_scroll(inventory_t *inventory, int32_t slots);

/* Puts in new items, wherever they go first: on the stack in hand if it is the same, on any other stack of it from the first slot,
 * and then in the first empty slot. Returns how many of count fit, and what does not stays with the caller. */
uint32_t inventory_add(inventory_t *inventory, uint16_t item, uint32_t count);

/* The same for a stack that may be worn, which keeps its wear. What fits leaves it, and what is left is what did not. */
uint32_t inventory_add_stack(inventory_t *inventory, item_stack_t *stack);

/* How many of count would fit, with nothing moved. */
uint32_t inventory_room(const inventory_t *inventory, uint16_t item, uint32_t count);

/* How many of this item there are, in all slots. */
uint32_t inventory_count(const inventory_t *inventory, uint16_t item);

/* Takes away up to count of an item, from the last slot back, so that what is on the hotbar goes last. Returns how many went. */
uint32_t inventory_remove(inventory_t *inventory, uint16_t item, uint32_t count);

/* Moves up to count of a slot out of the inventory, on to another stack that is not one of its own, as far as they merge. Returns how many
 * went, which the books count as removed. A slot that does not exist moves nothing. */
uint32_t inventory_slot_move_out(inventory_t *inventory, uint32_t slot, item_stack_t *to, uint32_t count);

/* Takes away up to count of what is in hand, and returns how many went. */
uint32_t inventory_selected_take(inventory_t *inventory, uint32_t count);

/* Swaps two slots. A slot that does not exist changes nothing. */
void inventory_swap(inventory_t *inventory, uint32_t left, uint32_t right);

/* A shift-click: as much of a slot as fits goes across, from the hotbar to the rows or from the rows to the hotbar, on to stacks of it
 * and then into the first empty slot. Returns how many moved. */
uint32_t inventory_quick_move(inventory_t *inventory, uint32_t slot);

/* How many items are held, in all slots, and in how many stacks. */
uint64_t inventory_items_held(const inventory_t *inventory);
uint32_t inventory_stacks_held(const inventory_t *inventory);

#endif
