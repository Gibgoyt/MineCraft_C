/* See README.md. Plain numbers and no memory: a stack is 8 bytes held by whoever holds it. */
#include "item/stack/index.h"

uint32_t item_stack_max(uint16_t item) {
    return item < ITEM_COUNT ? item_defs[item].max_stack : 0u;
}

uint16_t item_stack_drop_row(uint16_t item) {
    /* Both tables sort by name, and block_drop_items has no row for none. run_item_crafting holds them to it. */
    return (uint16_t)(item - 1u);
}

item_stack_t item_stack_make(uint16_t item, uint32_t count) {
    const uint32_t most = item_stack_max(item);
    const uint32_t held = count < most ? count : most;
    if (held == 0u) {
        return (item_stack_t){0};
    }
    return (item_stack_t){.item = item, .count = (uint8_t)held};
}

bool item_stack_is_empty(const item_stack_t *stack) {
    return stack->count == 0u;
}

bool item_stack_is_valid(const item_stack_t *stack) {
    if (stack->_pad0[0] != 0u || stack->_pad0[1] != 0u || stack->_pad0[2] != 0u) {
        return false;
    }
    if (stack->count == 0u) {
        return stack->item == ITEM_ID_NONE && stack->wear == 0u;
    }
    if (stack->item == ITEM_ID_NONE || stack->item >= ITEM_COUNT || stack->count > item_defs[stack->item].max_stack) {
        return false;
    }
    /* A tool at its last use has one gone less than it had. One with none left has broken, and is no stack. */
    const uint16_t durability = item_defs[stack->item].durability;
    return durability == 0u ? stack->wear == 0u : stack->wear < durability;
}

bool item_stack_can_merge(const item_stack_t *left, const item_stack_t *right) {
    return left->count == 0u || right->count == 0u || (left->item == right->item && left->wear == right->wear);
}

uint32_t item_stack_room(const item_stack_t *to, const item_stack_t *from) {
    if (from->count == 0u || !item_stack_can_merge(to, from)) {
        return 0u;
    }
    return item_stack_max(from->item) - (uint32_t)to->count;
}

/* All that goes in the one way out of a stack: a count of none has no item or wear either. */
static void item_stack_lower(item_stack_t *stack, uint32_t count) {
    stack->count = (uint8_t)((uint32_t)stack->count - count);
    if (stack->count == 0u) {
        *stack = (item_stack_t){0};
    }
}

uint32_t item_stack_move(item_stack_t *from, item_stack_t *to, uint32_t count) {
    const uint32_t room = item_stack_room(to, from);
    uint32_t moved = count < room ? count : room;
    moved = moved < from->count ? moved : from->count;
    if (moved == 0u || from == to) {
        return 0u;
    }
    to->item = from->item;
    to->wear = from->wear;
    to->count = (uint8_t)((uint32_t)to->count + moved);
    item_stack_lower(from, moved);
    return moved;
}

uint32_t item_stack_take(item_stack_t *stack, uint32_t count) {
    const uint32_t taken = count < stack->count ? count : stack->count;
    item_stack_lower(stack, taken);
    return taken;
}

void item_stack_swap(item_stack_t *left, item_stack_t *right) {
    const item_stack_t held = *left;
    *left = *right;
    *right = held;
}

void item_stack_click_primary(item_stack_t *slot, item_stack_t *cursor) {
    if (cursor->count == 0u) {
        (void)item_stack_move(slot, cursor, slot->count);
    } else if (item_stack_can_merge(slot, cursor)) {
        (void)item_stack_move(cursor, slot, cursor->count);
    } else {
        item_stack_swap(slot, cursor);
    }
}

void item_stack_click_secondary(item_stack_t *slot, item_stack_t *cursor) {
    if (cursor->count == 0u) {
        (void)item_stack_move(slot, cursor, ((uint32_t)slot->count + 1u) / 2u);
    } else if (item_stack_can_merge(slot, cursor)) {
        (void)item_stack_move(cursor, slot, 1u);
    } else {
        item_stack_swap(slot, cursor);
    }
}
