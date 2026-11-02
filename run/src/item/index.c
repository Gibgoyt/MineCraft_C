/* See README.md. The Owner: it holds the region its parts' memory comes from, and decides when that dies. */
#include "item/index.h"

#include <string.h>

enum : uint64_t {
    ITEM_REGION_BYTES = 2ull << 20,
};
static_assert((uint64_t)INVENTORY_BYTES <= (uint64_t)ITEM_REGION_BYTES);

bool item_init(item_t *item) {
    memset(item, 0, sizeof(*item));
    if (!heap_reserve(ITEM_REGION_BYTES, &item->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&item->arena_world, "item_world", item->owned_region.owned_base, ITEM_REGION_BYTES);
    return true;
}

void item_deinit(item_t *item) {
    arena_deinit(&item->arena_world);
    heap_release(&item->owned_region);
    memset(item, 0, sizeof(*item));
}

void item_open(item_t *item) {
    arena_reset(&item->arena_world);
    inventory_start(&item->inventory, &item->arena_world);
    atomic_store_explicit(&item->hotbar_key, 0u, memory_order_relaxed);
    atomic_store_explicit(&item->hotbar_wheel, 0, memory_order_relaxed);
}

void item_close(item_t *item) {
    arena_reset(&item->arena_world);
    memset(&item->inventory, 0, sizeof(item->inventory));
}

/* Relaxed, both of these: each is a whole request by itself, and nothing else is read because of it. */
void item_hotbar_send(item_t *item, uint32_t slot) {
    if (slot < INVENTORY_HOTBAR_SLOTS) {
        atomic_store_explicit(&item->hotbar_key, slot + 1u, memory_order_relaxed);
    }
}

void item_hotbar_scroll_send(item_t *item, int32_t slots) {
    if (slots != 0) {
        atomic_fetch_add_explicit(&item->hotbar_wheel, slots, memory_order_relaxed);
    }
}

void item_hotbar_apply(item_t *item) {
    const uint32_t key = atomic_exchange_explicit(&item->hotbar_key, 0u, memory_order_relaxed);
    if (key != 0u) {
        inventory_select(&item->inventory, key - 1u);
    }
    const int32_t wheel = atomic_exchange_explicit(&item->hotbar_wheel, 0, memory_order_relaxed);
    if (wheel != 0) {
        inventory_scroll(&item->inventory, wheel);
    }
}
