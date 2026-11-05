# tests/run/item/crafting/station

**Purpose:** ctest `run_item_crafting_station`. Tests where each recipe in `config/recipes/` is made, through `run/src/item/crafting/`. It has its own directory because `tests/run/item/crafting/index.c` is at the 1500 lines where a file splits.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, the generated `tables/`.

No card, no display and no world.

What it checks:

- Each recipe in the plan's bench row, written by hand in the test, makes nothing on a table and works on a bench.
- Each recipe of the table, a sample of each kind and all of what it takes to get a bench, makes nothing on a bench and works on a table.
- Every recipe in the tables is made at its own station and not at the other.
- Everything made only of planks, logs and sticks is `woodworking`, but for what it takes to get a bench. A new recipe of wood alone fails here until it says where it is made.
- A bench is 3 planks over 2 sticks, of any woods together, wherever it fits on a table, and a stick short or out of place is nothing.
- The way a player goes: logs, planks, a table, sticks and a bench, then a pickaxe that the same cells do not make on a table.
- The bench item is the west half, each half's file drops one bench, and it burns as a table does.

Placing and breaking a bench are in `tests/run/player/interaction/bench/`, a blast in `tests/run/mob/creeper/`, and its screen in `tests/run/ui/screens/`.
