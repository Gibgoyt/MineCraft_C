/* See README.md. Thirty-six stacks and a number saying which is in hand. Nothing here waits or touches the heap. */
#include "item/inventory/index.h"

#include "assert/index.h"

#include <string.h>

void inventory_start(inventory_t *inventory, arena_t *arena) {
    memset(inventory, 0, sizeof(*inventory));
    inventory->borrowed_slots =
        arena_alloc_zeroed(arena, (uint64_t)INVENTORY_SLOTS * sizeof(item_stack_t), alignof(item_stack_t));
}

item_stack_t *inventory_slot(inventory_t *inventory, uint32_t slot) {
    ASSERT_ALWAYS(slot < INVENTORY_SLOTS);
    return &inventory->borrowed_slots[slot];
}

const item_stack_t *inventory_slot_read(const inventory_t *inventory, uint32_t slot) {
    ASSERT_ALWAYS(slot < INVENTORY_SLOTS);
    return &inventory->borrowed_slots[slot];
}

const item_stack_t *inventory_selected(const inventory_t *inventory) {
    return &inventory->borrowed_slots[inventory->selected];
}

void inventory_select(inventory_t *inventory, uint32_t slot) {
    if (slot < INVENTORY_HOTBAR_SLOTS) {
        inventory->selected = (uint8_t)slot;
    }
}

void inventory_scroll(inventory_t *inventory, int32_t slots) {
    const int32_t size = (int32_t)INVENTORY_HOTBAR_SLOTS;
    const int32_t next = ((int32_t)inventory->selected + slots % size + size) % size;
    inventory->selected = (uint8_t)next;
}

/* The one order things go in by. Filling and looking for room must agree on it, or room would promise what adding cannot do. */
static uint32_t inventory_fill(item_stack_t *slots, uint32_t selected, item_stack_t *stack) {
    const uint32_t before = stack->count;
    /* A stack of it in hand first, then any other, and only then an empty slot. */
    if (slots[selected].count != 0u) {
        (void)item_stack_move(stack, &slots[selected], stack->count);
    }
    for (uint32_t i = 0u; i < INVENTORY_SLOTS && stack->count != 0u; i++) {
        if (slots[i].count != 0u) {
            (void)item_stack_move(stack, &slots[i], stack->count);
        }
    }
    for (uint32_t i = 0u; i < INVENTORY_SLOTS && stack->count != 0u; i++) {
        if (slots[i].count == 0u) {
            (void)item_stack_move(stack, &slots[i], stack->count);
        }
    }
    return before - (uint32_t)stack->count;
}

uint32_t inventory_add_stack(inventory_t *inventory, item_stack_t *stack) {
    const uint32_t taken = inventory_fill(inventory->borrowed_slots, inventory->selected, stack);
    inventory->added_count += taken;
    inventory->refused_count += stack->count;
    return taken;
}

uint32_t inventory_add(inventory_t *inventory, uint16_t item, uint32_t count) {
    uint32_t taken = 0u;
    /* One stack's worth at a time, as count may be more than one stack holds. */
    while (taken < count) {
        item_stack_t part = item_stack_make(item, count - taken);
        if (part.count == 0u) {
            break;
        }
        const uint32_t took = inventory_fill(inventory->borrowed_slots, inventory->selected, &part);
        taken += took;
        if (part.count != 0u) {
            break;
        }
    }
    inventory->added_count += taken;
    inventory->refused_count += (uint64_t)count - taken;
    return taken;
}

uint32_t inventory_room(const inventory_t *inventory, uint16_t item, uint32_t count) {
    const uint32_t most = item_stack_max(item);
    if (most == 0u) {
        return 0u;
    }
    uint64_t room = 0u;
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        const item_stack_t *const slot = &inventory->borrowed_slots[i];
        if (slot->count == 0u) {
            room += most;
        } else if (slot->item == item && slot->wear == 0u) {
            room += most - (uint32_t)slot->count;
        }
    }
    return room < count ? (uint32_t)room : count;
}

uint32_t inventory_count(const inventory_t *inventory, uint16_t item) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        const item_stack_t *const slot = &inventory->borrowed_slots[i];
        if (slot->count != 0u && slot->item == item) {
            count += slot->count;
        }
    }
    return count;
}

uint32_t inventory_remove(inventory_t *inventory, uint16_t item, uint32_t count) {
    uint32_t removed = 0u;
    for (uint32_t i = INVENTORY_SLOTS; i > 0u && removed < count; i--) {
        item_stack_t *const slot = &inventory->borrowed_slots[i - 1u];
        if (slot->count != 0u && slot->item == item) {
            removed += item_stack_take(slot, count - removed);
        }
    }
    inventory->removed_count += removed;
    return removed;
}

uint32_t inventory_slot_move_out(inventory_t *inventory, uint32_t slot, item_stack_t *to, uint32_t count) {
    if (slot >= INVENTORY_SLOTS) {
        return 0u;
    }
    const uint32_t moved = item_stack_move(&inventory->borrowed_slots[slot], to, count);
    inventory->removed_count += moved;
    return moved;
}

uint32_t inventory_selected_take(inventory_t *inventory, uint32_t count) {
    const uint32_t taken = item_stack_take(&inventory->borrowed_slots[inventory->selected], count);
    inventory->removed_count += taken;
    return taken;
}

void inventory_swap(inventory_t *inventory, uint32_t left, uint32_t right) {
    if (left < INVENTORY_SLOTS && right < INVENTORY_SLOTS) {
        item_stack_swap(&inventory->borrowed_slots[left], &inventory->borrowed_slots[right]);
    }
}

uint32_t inventory_quick_move(inventory_t *inventory, uint32_t slot) {
    if (slot >= INVENTORY_SLOTS) {
        return 0u;
    }
    item_stack_t *const from = &inventory->borrowed_slots[slot];
    const uint32_t first = slot < INVENTORY_HOTBAR_SLOTS ? INVENTORY_HOTBAR_SLOTS : 0u;
    const uint32_t last = slot < INVENTORY_HOTBAR_SLOTS ? INVENTORY_SLOTS : INVENTORY_HOTBAR_SLOTS;
    const uint32_t before = from->count;
    for (uint32_t i = first; i < last && from->count != 0u; i++) {
        if (inventory->borrowed_slots[i].count != 0u) {
            (void)item_stack_move(from, &inventory->borrowed_slots[i], from->count);
        }
    }
    for (uint32_t i = first; i < last && from->count != 0u; i++) {
        if (inventory->borrowed_slots[i].count == 0u) {
            (void)item_stack_move(from, &inventory->borrowed_slots[i], from->count);
        }
    }
    return before - (uint32_t)from->count;
}

uint64_t inventory_items_held(const inventory_t *inventory) {
    uint64_t held = 0u;
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        held += inventory->borrowed_slots[i].count;
    }
    return held;
}

uint32_t inventory_stacks_held(const inventory_t *inventory) {
    uint32_t held = 0u;
    for (uint32_t i = 0u; i < INVENTORY_SLOTS; i++) {
        held += inventory->borrowed_slots[i].count != 0u ? 1u : 0u;
    }
    return held;
}
