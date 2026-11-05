# tests/run/item/crafting/copper

**Purpose:** ctest `run_item_crafting_copper`. Tests copper armour's recipes in `config/recipes/`, through `run/src/item/crafting/` and `smelting/`. It has its own directory because `tests/run/item/crafting/index.c` is at the 1500 lines where a file splits.
**Tier:** test.
**Owns:** nothing outside each test's stack.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, and the generated `tables/`.

No card, no window and no built program.

What it checks:

- Copper ore, deepslate copper ore and raw copper smelt to the ingot.
- Each of the four patterns, written by hand in the test and laid with the right button at every height it fits on a table, makes its one piece, unworn, and taking it uses every ingot.
- None of them is made in a grid of two: each is three wide.
- Each pattern with any one ingot missing is not its piece, and with any one ingot swapped for iron makes nothing. The one shape that does come out of a missing ingot, boots from a helmet without the middle of its top row, is checked.
- The same four shapes in iron still make iron.
- 24 ingots make a whole set, which goes on, for 10 points.

What worn copper does is in `tests/run/item/equipment/copper/`.
