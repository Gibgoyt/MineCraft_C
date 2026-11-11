/* See run/src/item/README.md. */
#ifndef MC_RUN_ITEM_INDEX_H
#define MC_RUN_ITEM_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "item/container/index.h"
#include "item/crafting/index.h"
#include "item/equipment/index.h"
#include "item/inventory/index.h"
#include "item/smelting/index.h"
#include "item/stack/index.h"
#include "world/edit/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* What a player has. There is one, under app_t. Zero bytes mean not running. */
typedef struct item_t {
    arena_t arena_world;        /* 0: over owned_region; World: reset at each item_open and item_close */
    heap_region_t owned_region; /* 64 */
    inventory_t inventory;      /* 80: the sim's */
    /* The 3x3 at a station's own block, the sim's, from arena_world: main's stack has no room for it in this struct (plan 003 phase 4, issue 1).
     * One for them all: a station's screen gives its cells back when it closes, and sets its station when it opens (plan 005 phase 6). */
    crafting_grid_t *borrowed_crafting_bench; /* 120 */
    /* A line of its own: main writes these when a key goes down, and the sim reads them each tick. */
    _Atomic uint32_t hotbar_key;  /* 128: main to sim: the slot asked for, plus one, and 0 for none */
    _Atomic int32_t hotbar_wheel; /* 132: main to sim: slots to the right, not yet taken */
    uint8_t _pad1[56];            /* 136 */
    /* The sim's, from here on, as is what is at the far end of the two stores. */
    crafting_grid_t crafting_hands; /* 192: the 2x2 a player always has */
    crafting_grid_t crafting_table; /* 288: the 3x3 at a crafting table */
    container_store_t chests;       /* 384 */
    smelting_store_t furnaces;      /* 408 */
    uint64_t edit_cursor;           /* 448: how far into the world's edit log item_world_tick has read */
    uint64_t spilled_count;         /* 456: items that came out of a chest or a furnace that went */
    equipment_t equipment;          /* 464: what is worn, and how tools have gone */
} item_t;
static_assert(sizeof(item_t) == 512);
static_assert(alignof(item_t) == 64);
static_assert(offsetof(item_t, inventory) == 80);
static_assert(offsetof(item_t, hotbar_key) == 128);
static_assert(offsetof(item_t, equipment) == 464);

/* Something that went spills: a stack, whose cell it comes out of, for whoever puts it in the world. On the sim thread. */
typedef void item_spill_t(void *borrowed_context, const item_stack_t *stack, const int32_t cell[3]);

/* Reserves the region. On failure logs why, leaves nothing behind and returns false. */
[[nodiscard]] bool item_init(item_t *item);

/* Gives it back. Safe on a zeroed item_t. */
void item_deinit(item_t *item);

/* Nothing carried yet, with the first hotbar slot in hand. Before the threads start. */
void item_open(item_t *item);

/* What was carried goes: run/src/save/state/ has kept it, if it is to be kept. The threads must have stopped. Safe if none is open. */
void item_close(item_t *item);

/* main asks for a hotbar slot, or to scroll by some slots. Neither waits. Any thread, one at a time. A slot past the hotbar is ignored. */
void item_hotbar_send(item_t *item, uint32_t slot);
void item_hotbar_scroll_send(item_t *item, int32_t slots);

/* From here on it follows this world's edits. Before the threads start, with the world open. */
void item_world_begin(item_t *item, const world_edit_t *edit);

/* One tick of what stands in the world. A chest or a furnace made since the last tick gets its slots, and one that went spills what it held
 * through spill; then every furnace in a loaded column burns, with its block made furnace or furnace_lit to match. The sim's, holding the
 * map. Never waits and never touches the heap. */
void item_world_tick(item_t *item, world_edit_t *edit, item_spill_t *spill, void *borrowed_context);

/* Only the first half of a tick: what the log says of chests and furnaces, up to now. For a save, whose blocks would otherwise say a chest has gone while its
 * record still holds what should have spilled, and no log comes after a reload to say so. */
void item_world_edits_read(item_t *item, world_edit_t *edit, item_spill_t *spill, void *borrowed_context);

/* The sim takes what has been asked for since it last looked: the slot, and then the scroll on from there. */
void item_hotbar_apply(item_t *item);

#endif
