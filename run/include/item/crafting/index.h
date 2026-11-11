/* See run/src/item/crafting/README.md. */
#ifndef MC_RUN_ITEM_CRAFTING_INDEX_H
#define MC_RUN_ITEM_CRAFTING_INDEX_H

#include "item/inventory/index.h"
#include "item/stack/index.h"
#include "tables/recipes/index.h"

#include <stdint.h>

enum : uint32_t {
    CRAFTING_SIDE_HANDS = 2u,
    CRAFTING_SIDE_TABLE = 3u,
    /* What crafting_match says for a grid that makes nothing. */
    CRAFTING_NO_RECIPE = UINT32_MAX,
};
static_assert((uint32_t)RECIPE_CELLS == (uint32_t)CRAFTING_SIDE_TABLE * CRAFTING_SIDE_TABLE);

enum : uint32_t {
    /* The smith table's alloy slots: two inputs, then what they would make (plan 005 phase 11 part B). */
    CRAFTING_ALLOY_INPUTS = 2u,
    CRAFTING_ALLOY_OUTPUT = 2u,
    CRAFTING_ALLOY_SLOTS = 3u,
    /* How far from the smith table, in blocks along each axis, a lit block gives an alloy that needs it its heat. */
    CRAFTING_ALLOY_HEAT_REACH = 4u,
};
static_assert((uint32_t)RECIPE_ALLOY_INPUTS == (uint32_t)CRAFTING_ALLOY_INPUTS);

/* A grid and what it would make. Cells go by row, side to a row, and a grid of two has only its first four. Plain numbers. The sim's. */
typedef struct crafting_grid_t {
    item_stack_t cells[RECIPE_CELLS]; /* 0 */
    item_stack_t output;              /* 72: what taking it now gives; empty if it makes nothing */
    uint64_t crafted_count;           /* 80: items that have come out */
    uint32_t recipe;                  /* 88: the row of recipe_defs, or CRAFTING_NO_RECIPE */
    uint8_t side;                     /* 92 */
    uint8_t station;                  /* 93: a recipe_station_t: only that station's recipes are made here */
    uint8_t _pad0[2];                 /* 94 */
} crafting_grid_t;
static_assert(sizeof(crafting_grid_t) == 96);
static_assert(alignof(crafting_grid_t) == 8);

/* Which of this station's recipes these cells make: side to a row, side * side of them. The grid's place in the cells does not matter, nor
 * does a shaped recipe being the other way round, left to right. Only item matters: how worn a cell is, and how many it holds, do not. */
uint32_t crafting_match(const item_stack_t *cells, uint32_t side, uint32_t station);

/* An empty grid of this side, CRAFTING_SIDE_HANDS or CRAFTING_SIDE_TABLE, that makes this recipe_station_t's recipes. */
void crafting_grid_start(crafting_grid_t *grid, uint32_t side, uint32_t station);

/* How many cells it has. */
uint32_t crafting_grid_cells(const crafting_grid_t *grid);

/* A cell, or nullptr past the end. Whoever writes to one directly calls crafting_grid_changed after. */
item_stack_t *crafting_grid_cell(crafting_grid_t *grid, uint32_t cell);

/* What it makes, from what its cells now hold. */
void crafting_grid_changed(crafting_grid_t *grid);

/* The two clicks of a screen on a cell. What can go in a cell is anything, and the output follows. */
void crafting_grid_click_primary(crafting_grid_t *grid, uint32_t cell, item_stack_t *cursor);
void crafting_grid_click_secondary(crafting_grid_t *grid, uint32_t cell, item_stack_t *cursor);

/* A click on the output: one making of it onto what the pointer carries, with one of each cell gone. Nothing, if it makes nothing or the
 * pointer has no room for all of that one making. Returns how many came. */
uint32_t crafting_grid_take(crafting_grid_t *grid, item_stack_t *cursor);

/* A shift-click on the output: as many makings as the cells have for and the inventory has room for. Returns how many items came. */
uint32_t crafting_grid_take_all(crafting_grid_t *grid, inventory_t *inventory);

/* A screen closes: what is in the cells goes back to the inventory. What did not fit stays in its cell, and the caller sees to it.
 * Returns how many items stayed. */
uint32_t crafting_grid_give_back(crafting_grid_t *grid, inventory_t *inventory);

/* The alloy slots beside a station's grid: counted inputs, each a slot of its own, and what they make. Plain numbers. The sim's. */
typedef struct crafting_alloy_t {
    item_stack_t
        slots[CRAFTING_ALLOY_SLOTS]; /* 0: the inputs, then the output: what taking it now gives, empty if nothing */
    uint64_t alloyed_count;          /* 24: items that have come out */
    uint32_t recipe;                 /* 32: the row of recipe_alloys, or CRAFTING_NO_RECIPE */
    uint8_t station;                 /* 36: a recipe_station_t */
    uint8_t heat;                    /* 37: a bit for each recipe_process_t burning near the station */
    uint8_t _pad0[2];                /* 38 */
} crafting_alloy_t;
static_assert(sizeof(crafting_alloy_t) == 40);
static_assert(alignof(crafting_alloy_t) == 8);

/* Which of this station's alloys these two inputs make, with heat a bit for each recipe_process_t burning nearby: every input of the
 * recipe in a slot of its own with at least its count, and nothing else in the slots, in either order. More than the count is fine: the
 * rest stays. */
uint32_t crafting_alloy_match(const item_stack_t inputs[CRAFTING_ALLOY_INPUTS], uint32_t station, uint32_t heat);

/* Empty slots of this station, with no heat. */
void crafting_alloy_start(crafting_alloy_t *alloy, uint32_t station);

/* What it makes, from what its inputs now hold. Whoever writes to an input directly calls this after. */
void crafting_alloy_changed(crafting_alloy_t *alloy);

/* What burns near it now; what it makes follows. */
void crafting_alloy_heat_set(crafting_alloy_t *alloy, uint32_t heat);

/* A click on a slot: an input takes anything, as a grid's cell does, and the output is crafting_alloy_take. */
void crafting_alloy_click(crafting_alloy_t *alloy, uint32_t slot, item_stack_t *cursor, bool secondary);

/* One making onto what the pointer carries, with each input's count gone. Nothing if it makes nothing or the pointer has no room for all
 * of it. Returns how many came. */
uint32_t crafting_alloy_take(crafting_alloy_t *alloy, item_stack_t *cursor);

/* A shift-click on the output: as many makings as the inputs have for and the inventory has room for. Returns how many items came. */
uint32_t crafting_alloy_take_all(crafting_alloy_t *alloy, inventory_t *inventory);

/* A screen closes: what is in the inputs goes back to the inventory. What did not fit stays, and the caller sees to it. Returns how many
 * items stayed. */
uint32_t crafting_alloy_give_back(crafting_alloy_t *alloy, inventory_t *inventory);

#endif
