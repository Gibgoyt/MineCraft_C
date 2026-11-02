/* See run/src/item/container/README.md. */
#ifndef MC_RUN_ITEM_CONTAINER_INDEX_H
#define MC_RUN_ITEM_CONTAINER_INDEX_H

#include "arena/index.h"
#include "item/inventory/index.h"
#include "item/stack/index.h"

#include <stdint.h>

enum : uint32_t {
    CONTAINER_CHEST_SLOTS = 27u,
    /* Chests in a world. One past that stands there empty and does nothing, and container_open says so. */
    CONTAINER_CHESTS = 4096u,
};

/* One chest, by the cell it stands in. Plain numbers. */
typedef struct container_chest_t {
    int32_t cell[3];                           /* 0 */
    item_stack_t slots[CONTAINER_CHEST_SLOTS]; /* 12 */
} container_chest_t;
static_assert(sizeof(container_chest_t) == 228);
static_assert(alignof(container_chest_t) == 4);

/* What a store takes from its arena, with an alignment's slack. */
enum : uint64_t {
    CONTAINER_BYTES = (uint64_t)CONTAINER_CHESTS * sizeof(container_chest_t) + alignof(container_chest_t),
};

/* Every chest in a world, in no order that means anything. The sim's. */
typedef struct container_store_t {
    container_chest_t *borrowed_chests; /* 0 */
    uint64_t refused_count;             /* 8: chests past CONTAINER_CHESTS */
    uint32_t count;                     /* 16 */
    uint8_t _pad0[4];                   /* 20 */
} container_store_t;
static_assert(sizeof(container_store_t) == 24);

/* An empty store, from the arena. */
void container_start(container_store_t *store, arena_t *arena);

/* The chest in this cell, or nullptr if nothing has opened one there. */
container_chest_t *container_at(container_store_t *store, int32_t x, int32_t y, int32_t z);

/* The same, making an empty one if there is none. nullptr only with CONTAINER_CHESTS in the world, counted in refused_count. */
container_chest_t *container_open(container_store_t *store, int32_t x, int32_t y, int32_t z);

/* Ends the chest in this cell, if any: its stacks are written to out and it is gone. Returns how many there were. Moves the last chest into
 * the gap, so a pointer to one does not last across this. */
uint32_t container_remove(container_store_t *store, int32_t x, int32_t y, int32_t z,
                          item_stack_t out[CONTAINER_CHEST_SLOTS]);

/* A slot, or nullptr past the end. A screen's clicks are item_stack_click_primary and item_stack_click_secondary on it. */
item_stack_t *container_slot(container_chest_t *chest, uint32_t slot);

/* Puts a stack in, keeping its wear: on stacks of it from the first slot, then in the first empty one. What fits leaves it, and what is
 * left is what did not. Returns how many went. */
uint32_t container_add_stack(container_chest_t *chest, item_stack_t *stack);

/* A shift-click on an inventory's slot: as much of it as fits goes into the chest. Returns how many moved. */
uint32_t container_quick_move_in(container_chest_t *chest, inventory_t *inventory, uint32_t slot);

/* A shift-click on a chest's slot: as much of it as fits goes into the inventory. Returns how many moved. */
uint32_t container_quick_move_out(container_chest_t *chest, inventory_t *inventory, uint32_t slot);

/* How many items it holds, in all its slots. */
uint32_t container_items_held(const container_chest_t *chest);

#endif
