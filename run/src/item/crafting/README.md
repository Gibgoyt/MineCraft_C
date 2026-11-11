# run/src/item/crafting

**Purpose:** 2x2 and 3x3 crafting: which recipe a grid makes, and what taking it does to the grid. And the smith table's alloy slots: counted inputs and what they make (plan 005 phase 11 part B).
**Tier:** Arena, allocating nothing. A grid is 96 bytes held by `run/src/item/`'s `item_t`, and the alloy slots 40, from its arena.
**Owns:** nothing.
**Lifetime:** A grid's is its holder's: World, for those of `item_t`.
**Threads:** the sim's alone, as with the inventory.
**Public functions** (`run/include/item/crafting/index.h`):

- `crafting_match`: which row of `recipe_defs` some cells make, among one station's recipes. A grid knows its station: `crafting_table` is the table's and `crafting_hands` the hands', and `borrowed_crafting_bench` is whichever station's screen last opened it: woodworking, stoneworking, smithing or leatherworking (plan 005 phase 6). See `config/recipes/README.md`.
- `crafting_grid_start`, `crafting_grid_cells`, `crafting_grid_cell`, `crafting_grid_changed`: a grid, its cells, and bringing its output up to date.
- `crafting_grid_click_primary`, `crafting_grid_click_secondary`: the two clicks of a screen on a cell.
- `crafting_grid_take`, `crafting_grid_take_all`: a click and a shift-click on the output.
- `crafting_grid_give_back`: what a screen does as it closes.
- `crafting_alloy_match`, `crafting_alloy_start`, `crafting_alloy_changed`, `crafting_alloy_heat_set`: the alloy slots, `item_t.borrowed_crafting_alloy`, and which row of `recipe_alloys` they make, with what burns nearby.
- `crafting_alloy_click`, `crafting_alloy_take`, `crafting_alloy_take_all`, `crafting_alloy_give_back`: what the smith table's screen does with them.

**May be called by:** `run/src/item/`, and the screens of Stage 14.3.
**Depends on:** `item/stack`, `item/inventory`, the generated `tables/recipes/`.

## How a grid is matched

The rules are Minecraft's, and the recipes are `config/recipes/`.

1. **Only what the grid holds counts, and not where.** A box is drawn around what is in the cells, and a shaped recipe must be just that size. So a stick's recipe works in any column of a table, and one wider than 2 can never come from a grid of two.
2. **A shaped recipe also matches the other way round, left to right.** So an axe has two forms, and neither needs a file of its own.
3. **A shapeless recipe takes its things in any order, anywhere.** A cell of one may be a group that overlaps another's, so which item goes in which cell is a real question: this walks sets of cells instead of trying orders, which is at most 512 steps and no recursion.
4. **How many a cell holds does not change what it makes.** One of each goes per making.
5. **A worn tool counts as that item.** Nothing here repairs one, so nothing in a recipe takes one on purpose.

`compile/src/game_tables/recipes/` refuses two recipes one grid could match, so what comes out never depends on which is first. `run_item_crafting` holds every recipe to matching, with each thing each cell can be, mirrored, and in every place it fits, against the whole table.

**It costs about a walk over `recipe_defs`,** and only on a change to a cell: nothing here runs in a tick.

## Taking the output

A click gives one making, and only if all of it fits on what the pointer carries: part of a making would have to go somewhere. A shift-click repeats that into the inventory until a cell runs out, the recipe stops matching, or the inventory has no room for a whole making. A recipe here leaves nothing behind in its cells.

## The alloy slots

A grid's cell holds one item for a making, and 9 iron and 1 chromium is 10, so an alloy (`kind: alloy` in `config/recipes/`) has **slots of its own**: two inputs and an output, beside the smith table's grid. A recipe matches when each of its inputs is in a slot of its own with at least its count and nothing else is in the slots, in either order; taking it takes each input's count and leaves the rest. An alloy whose file says `heat` matches only with a block of that process burning within `CRAFTING_ALLOY_HEAT_REACH` along every axis of the smith table, which the screen asks `smelting_lit_near` for at every click and every tick it is up. Stainless steel needs none. There is one set of slots, as there is one bench grid: the smith table's screen gives the inputs back when it closes, and a save keeps them.
