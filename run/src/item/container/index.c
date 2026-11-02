/* See README.md. */
#include "item/container/index.h"

#include <string.h>

void container_start(container_store_t *store, arena_t *arena) {
    memset(store, 0, sizeof(*store));
    store->borrowed_chests =
        arena_alloc_array(arena, CONTAINER_CHESTS, sizeof(container_chest_t), alignof(container_chest_t));
}

/* A chest is looked for when a screen opens or a block goes, and no tick does either on its own: so a straight look. */
container_chest_t *container_at(container_store_t *store, int32_t x, int32_t y, int32_t z) {
    for (uint32_t i = 0u; i < store->count; i++) {
        container_chest_t *const chest = &store->borrowed_chests[i];
        if (chest->cell[0] == x && chest->cell[1] == y && chest->cell[2] == z) {
            return chest;
        }
    }
    return nullptr;
}

container_chest_t *container_open(container_store_t *store, int32_t x, int32_t y, int32_t z) {
    container_chest_t *const found = container_at(store, x, y, z);
    if (found != nullptr) {
        return found;
    }
    if (store->count == CONTAINER_CHESTS) {
        store->refused_count++;
        return nullptr;
    }
    container_chest_t *const chest = &store->borrowed_chests[store->count];
    store->count++;
    memset(chest, 0, sizeof(*chest));
    chest->cell[0] = x;
    chest->cell[1] = y;
    chest->cell[2] = z;
    return chest;
}

uint32_t container_remove(container_store_t *store, int32_t x, int32_t y, int32_t z,
                          item_stack_t out[CONTAINER_CHEST_SLOTS]) {
    container_chest_t *const chest = container_at(store, x, y, z);
    if (chest == nullptr) {
        return 0u;
    }
    uint32_t stacks = 0u;
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS; i++) {
        if (chest->slots[i].count != 0u) {
            out[stacks] = chest->slots[i];
            stacks++;
        }
    }
    store->count--;
    *chest = store->borrowed_chests[store->count];
    return stacks;
}

item_stack_t *container_slot(container_chest_t *chest, uint32_t slot) {
    return slot < CONTAINER_CHEST_SLOTS ? &chest->slots[slot] : nullptr;
}

uint32_t container_add_stack(container_chest_t *chest, item_stack_t *stack) {
    const uint32_t before = stack->count;
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS && stack->count != 0u; i++) {
        if (chest->slots[i].count != 0u) {
            (void)item_stack_move(stack, &chest->slots[i], stack->count);
        }
    }
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS && stack->count != 0u; i++) {
        if (chest->slots[i].count == 0u) {
            (void)item_stack_move(stack, &chest->slots[i], stack->count);
        }
    }
    return before - (uint32_t)stack->count;
}

uint32_t container_quick_move_in(container_chest_t *chest, inventory_t *inventory, uint32_t slot) {
    if (slot >= INVENTORY_SLOTS) {
        return 0u;
    }
    const item_stack_t *const from = inventory_slot_read(inventory, slot);
    if (from->count == 0u) {
        return 0u;
    }
    /* On stacks first, and then empty slots: the books of the inventory count what leaves it. */
    uint32_t moved = 0u;
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS && from->count != 0u; i++) {
        if (chest->slots[i].count != 0u) {
            moved += inventory_slot_move_out(inventory, slot, &chest->slots[i], from->count);
        }
    }
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS && from->count != 0u; i++) {
        if (chest->slots[i].count == 0u) {
            moved += inventory_slot_move_out(inventory, slot, &chest->slots[i], from->count);
        }
    }
    return moved;
}

uint32_t container_quick_move_out(container_chest_t *chest, inventory_t *inventory, uint32_t slot) {
    return slot < CONTAINER_CHEST_SLOTS ? inventory_add_stack(inventory, &chest->slots[slot]) : 0u;
}

uint32_t container_items_held(const container_chest_t *chest) {
    uint32_t held = 0u;
    for (uint32_t i = 0u; i < CONTAINER_CHEST_SLOTS; i++) {
        held += chest->slots[i].count;
    }
    return held;
}
