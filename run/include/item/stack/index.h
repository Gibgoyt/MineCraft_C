/* See run/src/item/stack/README.md. */
#ifndef MC_RUN_ITEM_STACK_INDEX_H
#define MC_RUN_ITEM_STACK_INDEX_H

#include "tables/items/index.h"

#include <stdint.h>

/* One slot's worth of one item. All zero bytes are the one empty stack: nothing here leaves an item or a wear with no count. */
typedef struct item_stack_t {
    uint16_t item;    /* 0: an item_id_t */
    uint16_t wear;    /* 2: uses gone, from the item's durability; 0 for what does not wear */
    uint8_t count;    /* 4: 0 to the item's max_stack */
    uint8_t _pad0[3]; /* 5 */
} item_stack_t;
static_assert(sizeof(item_stack_t) == 8);
static_assert(alignof(item_stack_t) == 2);

/* What a slot of this item holds at most. 0 for ITEM_ID_NONE, and for what is no item at all. */
uint32_t item_stack_max(uint16_t item);

/* New and unworn, as many as one slot takes of count. Empty if that is none. */
item_stack_t item_stack_make(uint16_t item, uint32_t count);

bool item_stack_is_empty(const item_stack_t *stack);

/* Whether these bytes are a stack at all: a real item, a count it allows, wear it can have, and the one empty form. What comes from
 * outside the program, like a save, is held to this. */
bool item_stack_is_valid(const item_stack_t *stack);

/* Whether what is in one could go into the other, room aside: the same item, worn the same, or either of them empty. */
bool item_stack_can_merge(const item_stack_t *left, const item_stack_t *right);

/* How many of from could go into to. */
uint32_t item_stack_room(const item_stack_t *to, const item_stack_t *from);

/* Moves as many of count as there are and as fit, and returns how many that was. None if they cannot merge. */
uint32_t item_stack_move(item_stack_t *from, item_stack_t *to, uint32_t count);

/* Takes up to count away, and returns how many went. */
uint32_t item_stack_take(item_stack_t *stack, uint32_t count);

void item_stack_swap(item_stack_t *left, item_stack_t *right);

/* The two clicks of a screen, with what the pointer carries. The first picks up all, puts down all that fits, or swaps. */
void item_stack_click_primary(item_stack_t *slot, item_stack_t *cursor);

/* The second picks up the larger half, puts down one, or swaps. */
void item_stack_click_secondary(item_stack_t *slot, item_stack_t *cursor);

#endif
