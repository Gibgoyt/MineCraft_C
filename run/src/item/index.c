/* See README.md. The Owner: it holds the region its parts' memory comes from, and decides when that dies. */
#include "item/index.h"

#include "tables/blocks/index.h"

#include <string.h>

enum : uint64_t {
    ITEM_REGION_BYTES = 2ull << 20,
};
static_assert((uint64_t)INVENTORY_BYTES + CONTAINER_BYTES + SMELTING_BYTES + sizeof(crafting_grid_t) +
                  alignof(crafting_grid_t) <=
              (uint64_t)ITEM_REGION_BYTES);

enum : uint32_t {
    /* Records read from the world's log at once. It goes round again until there are none. */
    ITEM_EDIT_BATCH = 64u,
};

/* Which store a block has slots in. A furnace lighting up does not change it. */
typedef enum item_block_kind_t : uint8_t {
    ITEM_BLOCK_NONE = 0,
    ITEM_BLOCK_CHEST = 1,
    ITEM_BLOCK_FURNACE = 2,
} item_block_kind_t;

static item_block_kind_t item_block_kind(uint16_t voxel) {
    /* An edit's record holds the block's state too: a chest is a chest whichever way it faces. */
    const uint16_t block = block_state_id(voxel);
    if (block == BLOCK_ID_CHEST) {
        return ITEM_BLOCK_CHEST;
    }
    return block == BLOCK_ID_FURNACE || block == BLOCK_ID_FURNACE_LIT ? ITEM_BLOCK_FURNACE : ITEM_BLOCK_NONE;
}

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
    container_start(&item->chests, &item->arena_world);
    smelting_start(&item->furnaces, &item->arena_world);
    crafting_grid_start(&item->crafting_hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE);
    crafting_grid_start(&item->crafting_table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE);
    item->borrowed_crafting_bench =
        arena_alloc_zeroed(&item->arena_world, sizeof(crafting_grid_t), alignof(crafting_grid_t));
    crafting_grid_start(item->borrowed_crafting_bench, CRAFTING_SIDE_TABLE, RECIPE_STATION_WOODWORKING);
    equipment_start(&item->equipment);
    item->edit_cursor = 0u;
    item->spilled_count = 0u;
    atomic_store_explicit(&item->hotbar_key, 0u, memory_order_relaxed);
    atomic_store_explicit(&item->hotbar_wheel, 0, memory_order_relaxed);
}

void item_close(item_t *item) {
    arena_reset(&item->arena_world);
    memset(&item->inventory, 0, sizeof(item->inventory));
    memset(&item->crafting_hands, 0, sizeof(item->crafting_hands));
    memset(&item->crafting_table, 0, sizeof(item->crafting_table));
    item->borrowed_crafting_bench = nullptr;
    memset(&item->chests, 0, sizeof(item->chests));
    memset(&item->furnaces, 0, sizeof(item->furnaces));
    memset(&item->equipment, 0, sizeof(item->equipment));
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

void item_world_begin(item_t *item, const world_edit_t *edit) {
    item->edit_cursor = world_edit_log_position(edit);
}

static void item_spill_stacks(item_t *item, const item_stack_t *stacks, uint32_t count, const int32_t cell[3],
                              item_spill_t *spill, void *borrowed_context) {
    for (uint32_t i = 0u; i < count; i++) {
        item->spilled_count += stacks[i].count;
        spill(borrowed_context, &stacks[i], cell);
    }
}

/* The chest or furnace that was in this cell has gone. */
static void item_block_gone(item_t *item, item_block_kind_t kind, const int32_t cell[3], item_spill_t *spill,
                            void *borrowed_context) {
    item_stack_t stacks[CONTAINER_CHEST_SLOTS];
    static_assert((uint32_t)CONTAINER_CHEST_SLOTS >= (uint32_t)SMELTING_SLOTS);
    const uint32_t count = kind == ITEM_BLOCK_CHEST
                               ? container_remove(&item->chests, cell[0], cell[1], cell[2], stacks)
                               : smelting_remove(&item->furnaces, cell[0], cell[1], cell[2], stacks);
    item_spill_stacks(item, stacks, count, cell, spill, borrowed_context);
}

/* A chest or furnace has come to this cell. Anything still held for the cell was in a column that has been unloaded and made anew, with
 * everything else that had been done to it, and goes the same way. */
static void item_block_came(item_t *item, item_block_kind_t kind, const int32_t cell[3]) {
    item_stack_t stacks[CONTAINER_CHEST_SLOTS];
    if (kind == ITEM_BLOCK_CHEST) {
        (void)container_remove(&item->chests, cell[0], cell[1], cell[2], stacks);
        (void)container_open(&item->chests, cell[0], cell[1], cell[2]);
    } else {
        (void)smelting_remove(&item->furnaces, cell[0], cell[1], cell[2], stacks);
        (void)smelting_open(&item->furnaces, cell[0], cell[1], cell[2]);
    }
}

/* The log has gone round past what this read, which takes far more than a tick's edits: so ask the world about each one instead. What has
 * come since is made when a screen opens it. */
static void item_world_recheck(item_t *item, const world_map_t *map, item_spill_t *spill, void *borrowed_context) {
    for (uint32_t i = item->chests.count; i > 0u; i--) {
        const int32_t cell[3] = {item->chests.borrowed_chests[i - 1u].cell[0],
                                 item->chests.borrowed_chests[i - 1u].cell[1],
                                 item->chests.borrowed_chests[i - 1u].cell[2]};
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(map, cell[0], cell[1], cell[2], &block) == WORLD_MAP_STATUS_OK &&
            item_block_kind(block) != ITEM_BLOCK_CHEST) {
            item_block_gone(item, ITEM_BLOCK_CHEST, cell, spill, borrowed_context);
        }
    }
    for (uint32_t i = item->furnaces.count; i > 0u; i--) {
        const int32_t cell[3] = {item->furnaces.borrowed_furnaces[i - 1u].cell[0],
                                 item->furnaces.borrowed_furnaces[i - 1u].cell[1],
                                 item->furnaces.borrowed_furnaces[i - 1u].cell[2]};
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(map, cell[0], cell[1], cell[2], &block) == WORLD_MAP_STATUS_OK &&
            item_block_kind(block) != ITEM_BLOCK_FURNACE) {
            item_block_gone(item, ITEM_BLOCK_FURNACE, cell, spill, borrowed_context);
        }
    }
}

static void item_edits_read(item_t *item, world_edit_t *edit, item_spill_t *spill, void *borrowed_context) {
    world_edit_record_t records[ITEM_EDIT_BATCH];
    uint64_t lost = 0u;
    bool any_lost = false;
    for (;;) {
        const uint64_t count = world_edit_log_read(edit, &item->edit_cursor, records, ITEM_EDIT_BATCH, &lost);
        any_lost = any_lost || lost != 0u;
        for (uint64_t i = 0u; i < count; i++) {
            const world_edit_record_t *const record = &records[i];
            const item_block_kind_t before = item_block_kind(record->before), after = item_block_kind(record->after);
            if (before == after) {
                continue; /* most things, and a furnace lighting up */
            }
            const int32_t cell[3] = {record->x, record->y, record->z};
            if (before != ITEM_BLOCK_NONE) {
                item_block_gone(item, before, cell, spill, borrowed_context);
            }
            if (after != ITEM_BLOCK_NONE) {
                item_block_came(item, after, cell);
            }
        }
        if (count < ITEM_EDIT_BATCH) {
            break;
        }
    }
    if (any_lost) {
        item_world_recheck(item, edit->borrowed_map, spill, borrowed_context);
    }
}

/* From the last back, since one that has gone is replaced by the last. */
static void item_furnaces_tick(item_t *item, world_edit_t *edit) {
    smelting_store_t *const store = &item->furnaces;
    for (uint32_t i = store->count; i > 0u; i--) {
        smelting_furnace_t *const furnace = &store->borrowed_furnaces[i - 1u];
        const int32_t x = furnace->cell[0], y = furnace->cell[1], z = furnace->cell[2];
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(edit->borrowed_map, x, y, z, &block) != WORLD_MAP_STATUS_OK) {
            continue; /* it waits for its column */
        }
        if (item_block_kind(block) != ITEM_BLOCK_FURNACE) {
            /* Its column was unloaded and made anew. The log says nothing of that, and its stacks went with the rest. */
            item_stack_t stacks[SMELTING_SLOTS];
            (void)smelting_remove(store, x, y, z, stacks);
            continue;
        }
        const uint16_t want = smelting_tick(store, furnace) ? BLOCK_ID_FURNACE_LIT : BLOCK_ID_FURNACE;
        /* If the pool has no room this tick, the next one sees the same difference and tries again. */
        if (block != want) {
            (void)world_edit_block_set(edit, x, y, z, want);
        }
    }
}

void item_world_edits_read(item_t *item, world_edit_t *edit, item_spill_t *spill, void *borrowed_context) {
    item_edits_read(item, edit, spill, borrowed_context);
}

void item_world_tick(item_t *item, world_edit_t *edit, item_spill_t *spill, void *borrowed_context) {
    item_edits_read(item, edit, spill, borrowed_context);
    item_furnaces_tick(item, edit);
}
