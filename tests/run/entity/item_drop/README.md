# tests/run/entity/item_drop

**Purpose:** ctest `run_entity_drop`, named in `test.cmake`. Tests `run/src/entity/` with `item_drop`, with what `run/src/player/` and `run/src/game/` do with it.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/entity/`, `run/src/player/`, the built program, and a card for that program.

Compute only. No real display, console or input device is opened: the program always runs `--headless`.

What it checks:

- **Physics:** a fall by hand, for three ticks and for a hundred; coming out of a block; landing on a block and on a slab and staying; sliding on stone and on ice, by the sum of what each tick keeps; a wall; what it lay on going, and a block coming down on it, even buried with only the top open; water; lava and a cactus; a column not loaded and the edge of what is loaded; old age to the tick.
- **Merging:** the reach to each side of its limit, on each axis, in negative cells and across cells; different items; the limit of 64 and what stays; the age and wait of a merge; a pile of 1000 coming down to 16; and the grid against every pair among 1500 stacks at random.
- **Pickup:** the wait to the tick; an overlap against a touch; a taker taking none or some; a real hand taking blocks, keeping what it has, and leaving coal; every item in the tables.
- **The store and the Owner:** a full store; the books balancing; the same bytes from the same seed and others from another; no heap in a tick; the region given back; a world opened again; and the time a full store of moving stacks takes.
- **A body:** a real `player_t` mines dirt, in the order `game_sim_tick` has it, and the item lies out of its reach until it walks over. Then it has it, and places it.
- **The built program:** one script, stopped at each point of the story, and what the program reports there: nothing out while mining; dirt on the ground with the hand empty; dirt in the hand and none on the ground; and dirt placed with the hand empty, and `0 bytes live`. Run twice, it gives the same pictures, and the hole shows and then does not.
