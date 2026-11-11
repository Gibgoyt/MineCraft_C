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

/* Which store a block has slots in. A furnace lighting up does not change it. The three that smelt are in the order of recipe_process_t,
 * from ITEM_BLOCK_FURNACE, and each keeps its slots in the smelting store. */
typedef enum item_block_kind_t : uint8_t {
    ITEM_BLOCK_NONE = 0,
    ITEM_BLOCK_CHEST = 1,
    ITEM_BLOCK_FURNACE = 2,
    ITEM_BLOCK_BLAST_FURNACE = 3,
    ITEM_BLOCK_TANNING_RACK = 4,
} item_block_kind_t;
static_assert(ITEM_BLOCK_BLAST_FURNACE - ITEM_BLOCK_FURNACE == RECIPE_PROCESS_BLAST_FURNACE);
static_assert(ITEM_BLOCK_TANNING_RACK - ITEM_BLOCK_FURNACE == RECIPE_PROCESS_TANNING_RACK);

/* The blocks of each process, by recipe_process_t: as it stands, and lit while it burns. One that burns nothing is never lit. A tanning
 * rack's slots are kept by its first part, the one placed where it was aimed (plan 005 phase 6). */
static const uint16_t ITEM_PROCESS_BLOCKS[RECIPE_PROCESS_COUNT][2] = {
    [RECIPE_PROCESS_FURNACE] = {BLOCK_ID_FURNACE, BLOCK_ID_FURNACE_LIT},
    [RECIPE_PROCESS_BLAST_FURNACE] = {BLOCK_ID_BLAST_FURNACE, BLOCK_ID_BLAST_FURNACE_LIT},
    [RECIPE_PROCESS_TANNING_RACK] = {BLOCK_ID_TANNING_RACK_LEFT, BLOCK_ID_TANNING_RACK_LEFT},
};

static item_block_kind_t item_block_kind(uint16_t voxel) {
    /* An edit's record holds the block's state too: a chest is a chest whichever way it faces. */
    const uint16_t block = block_state_id(voxel);
    if (block == BLOCK_ID_CHEST) {
        return ITEM_BLOCK_CHEST;
    }
    for (uint32_t p = 0u; p < RECIPE_PROCESS_COUNT; p++) {
        if (block == ITEM_PROCESS_BLOCKS[p][0] || block == ITEM_PROCESS_BLOCKS[p][1]) {
            return (item_block_kind_t)(ITEM_BLOCK_FURNACE + p);
        }
    }
    return ITEM_BLOCK_NONE;
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
    crafting_grid_start(&item->crafting_hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_HANDS);
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
        (void)smelting_open_process(&item->furnaces, cell[0], cell[1], cell[2], (uint8_t)(kind - ITEM_BLOCK_FURNACE));
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
        const item_block_kind_t kind =
            (item_block_kind_t)(ITEM_BLOCK_FURNACE + item->furnaces.borrowed_furnaces[i - 1u].process);
        if (world_map_block_get(map, cell[0], cell[1], cell[2], &block) == WORLD_MAP_STATUS_OK &&
            item_block_kind(block) != kind) {
            item_block_gone(item, kind, cell, spill, borrowed_context);
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
        const uint32_t process = furnace->process < RECIPE_PROCESS_COUNT ? furnace->process : RECIPE_PROCESS_FURNACE;
        if (item_block_kind(block) != (item_block_kind_t)(ITEM_BLOCK_FURNACE + process)) {
            /* Its column was unloaded and made anew. The log says nothing of that, and its stacks went with the rest. */
            item_stack_t stacks[SMELTING_SLOTS];
            (void)smelting_remove(store, x, y, z, stacks);
            continue;
        }
        const uint16_t want = ITEM_PROCESS_BLOCKS[process][smelting_tick(store, furnace) ? 1u : 0u];
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
