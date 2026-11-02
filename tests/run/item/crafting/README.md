# tests/run/item/crafting

**Purpose:** ctest `run_item_crafting`. Tests `run/src/item/crafting/`, `smelting/` and `container/`, the join to the world in `run/src/item/`, `config/recipes/`, and what `run/src/game/` does with all of it.
**Tier:** test.
**Owns:** the test arena, and each `item_t`'s and world's regions, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the real game test runs the game's own, and the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/test_world.h`, `run/src/item/`, the generated `tables/`, the built program, and a card for that program and for the real game.

Compute only. No real display, console or input device is opened: the program always runs `--headless`.

What it checks:

- **The tables:** every item has a row to lie on the ground by; every row of every table is possible; the basic set is there, with the published burn times.
- **Matching:** every recipe, with everything each cell can be, in every place it fits, both ways round, against the whole table. A grid of two makes what fits and no more. What is almost a recipe is nothing. 40000 grids at random, half of them spoiled, against a second way of working it out: a recipe laid in every place and compared whole, and each order of a shapeless one tried by Heap's algorithm.
- **A grid:** from a log to a pickaxe, as a player clicks it. The output whole or not at all; a shift-click up to what the cells and the inventory allow; closing a screen.
- **A furnace:** one coal is 8 items at 200 ticks each, to the tick; every smelt and every fuel in the tables; fuel only goes on for what can cook; cooling, and starting over; a lava bucket; the slot rules a screen must follow; a store's capacity; and the same clicks giving the same bytes.
- **A chest:** where things go; wear kept; both shift-clicks with the books kept; a store's capacity; and 200000 steps at random with no item lost or made.
- **The join to the world:** a chest coming, going and spilling all it held; old slots kept for a cell not coming back; a furnace lighting its block, going out, and spilling when broken lit; waiting for a column not loaded; the log going round past the cursor; and no heap in play.
- **A real game:** with the threads running, a chest broken before they start comes out on the ground as items, including an item no block drops, and a furnace cooks and is lit by the sim's tick alone.
- **The program:** `--walk` runs with all of this, and its report says so.

Not checked here, because nothing reaches it yet: a player opening a chest or furnace in the running game, which is Stage 14.3's screens.
