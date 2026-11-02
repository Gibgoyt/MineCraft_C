/* See run/src/item/smelting/README.md. */
#ifndef MC_RUN_ITEM_SMELTING_INDEX_H
#define MC_RUN_ITEM_SMELTING_INDEX_H

#include "arena/index.h"
#include "item/inventory/index.h"
#include "item/stack/index.h"
#include "tables/recipes/index.h"

#include <stdint.h>

enum : uint32_t {
    /* Furnaces in a world. One past that stands there and does nothing, and smelting_open says so. */
    SMELTING_FURNACES = 1024u,
    /* A furnace's three slots, as a spilled furnace gives them. */
    SMELTING_SLOTS = 3u,
};

typedef enum smelting_slot_t : uint8_t {
    SMELTING_SLOT_INPUT = 0,
    SMELTING_SLOT_FUEL = 1,
    SMELTING_SLOT_OUTPUT = 2,
} smelting_slot_t;

/* One furnace, by the cell it stands in. Plain numbers, so that the same world and clicks give the same bytes. */
typedef struct smelting_furnace_t {
    int32_t cell[3];                    /* 0 */
    item_stack_t slots[SMELTING_SLOTS]; /* 12: by smelting_slot_t */
    uint32_t burn_ticks;                /* 36: left of what is burning */
    uint32_t burn_total;                /* 40: what that fuel burns for */
    uint32_t cook_ticks;                /* 44: on what is cooking */
    uint16_t cooking;                   /* 48: the item that is, so that swapping it starts over */
    uint8_t _pad0[14];                  /* 50 */
} smelting_furnace_t;
static_assert(sizeof(smelting_furnace_t) == 64);
static_assert(alignof(smelting_furnace_t) == 4);

/* What a store takes from its arena, with an alignment's slack. */
enum : uint64_t {
    SMELTING_BYTES = (uint64_t)SMELTING_FURNACES * sizeof(smelting_furnace_t) + alignof(smelting_furnace_t),
};

/* Every furnace in a world, in no order that means anything. The sim's. */
typedef struct smelting_store_t {
    smelting_furnace_t *borrowed_furnaces; /* 0 */
    uint64_t smelted_count;                /* 8: items that have come out */
    uint64_t burned_count;                 /* 16: items of fuel gone */
    uint64_t refused_count;                /* 24: furnaces past SMELTING_FURNACES */
    uint32_t count;                        /* 32 */
    uint8_t _pad0[4];                      /* 36 */
} smelting_store_t;
static_assert(sizeof(smelting_store_t) == 40);

/* An empty store, from the arena. */
void smelting_start(smelting_store_t *store, arena_t *arena);

/* The furnace in this cell, or nullptr if nothing has opened one there. */
smelting_furnace_t *smelting_at(smelting_store_t *store, int32_t x, int32_t y, int32_t z);

/* The same, making an empty one if there is none. nullptr only with SMELTING_FURNACES in the world, counted in refused_count. */
smelting_furnace_t *smelting_open(smelting_store_t *store, int32_t x, int32_t y, int32_t z);

/* Ends the furnace in this cell, if any: its stacks are written to out and it is gone. Returns how many there were. Moves the last furnace
 * into the gap, so a pointer to one does not last across this. */
uint32_t smelting_remove(smelting_store_t *store, int32_t x, int32_t y, int32_t z, item_stack_t out[SMELTING_SLOTS]);

/* One tick of one furnace, whatever is in its slots. Returns whether it is burning at the end of it. Never waits. */
bool smelting_tick(smelting_store_t *store, smelting_furnace_t *furnace);

bool smelting_is_lit(const smelting_furnace_t *furnace);

/* What it will make of this input, or nullptr. */
const recipe_smelt_t *smelting_recipe_of(uint16_t item);

/* How long one of this burns: 0 if it does not. */
uint32_t smelting_burn_ticks(uint16_t item);

/* How far along a screen's two gauges are, from 0 to 1. */
float32_t smelting_cook_fraction(const smelting_furnace_t *furnace);
float32_t smelting_burn_fraction(const smelting_furnace_t *furnace);

/* A screen's clicks on a slot, with what the pointer carries. The fuel slot takes only what burns, and the input anything, as a furnace
 * that cannot smelt it just waits. The output is only taken from: all of it onto the pointer, or none. */
void smelting_click(smelting_furnace_t *furnace, uint32_t slot, item_stack_t *cursor, bool secondary);

/* A shift-click on an inventory's slot: what smelts to the input, or else what burns to the fuel slot. Returns how many moved. */
uint32_t smelting_quick_move_in(smelting_furnace_t *furnace, inventory_t *inventory, uint32_t slot);

/* A shift-click on a furnace's slot: as much as fits into the inventory. Returns how many moved. */
uint32_t smelting_quick_move_out(smelting_furnace_t *furnace, inventory_t *inventory, uint32_t slot);

#endif
