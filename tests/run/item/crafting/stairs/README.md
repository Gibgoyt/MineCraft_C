# tests/run/item/crafting/stairs

**Purpose:** ctest `run_item_crafting_stairs`. Tests the stairs of every wood in `config/blocks/`, `config/items/` and `config/recipes/`, through `run/src/item/crafting/` and `smelting/`, and what the real renderer makes of them. It has its own directory because `tests/run/item/crafting/index.c` is at the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena, and what the picture's world, game and card blocks take, given back by the end of that test.
**Lifetime:** One run.
**Threads:** main, and the real workers for the picture.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/item/`, `run/src/physics/`, `run/src/entity/`, `run/src/player/`, `run/src/game/` and everything under it, the generated `tables/`, and a card for the picture.

Compute only. No display, console or input device is opened, and the program itself is not run.

What it checks:

- Each wood's planks, laid with the right button in the pattern written by hand in the test, make 4 of that wood's stairs on a woodworking bench, and taking them uses every plank. The pattern mirrored does the same.
- A grid of two cannot make them: the pattern is three by three.
- One plank of any other wood, in any cell, makes nothing, for every pair of woods. So does a pattern with six different woods.
- One plank short is not stairs.
- Each wood's block and item: its own drop, its own item, the shape and the hardness of `oak_stairs`, and every face the texture of that wood's planks.
- Each burns as oak stairs burn, stands up to a blast as they do, and has the same two collision boxes.
- Every planks item has its stairs: a new wood without them fails here.
- `cobblestone_stairs` still takes cobblestone.
- **On the card:** each wood's stairs, put down by the hands and seen from above over the top step, are nearest in colour to that wood's own planks, of all eight, in the same cell and light. To see all eight together:

      mkdir -p /tmp/stairs_shots
      MC_STAIRS_SHOTS=/tmp/stairs_shots tools/test-or-build.sh timeout 280 ctest --preset debug -R '^run_item_crafting_stairs$' --output-on-failure
