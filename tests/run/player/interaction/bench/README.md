# tests/run/player/interaction/bench

**Purpose:** ctest `run_player_interaction_bench`. Tests a bench two voxels long: `run/src/player/interaction/` and `run/src/world/bench/`. It has its own directory because `tests/run/player/interaction/index.c` is past the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`.

No card, no display, and the program itself is not run. What it checks:

- **Placing:** the west half goes where aimed and the east half at `x + 1`, for one item.
- **Refusing:** a block in the east cell, the player in either cell, and a column not loaded for the east half each place nothing and keep the item. What can be overwritten, as tall grass or water, is overwritten.
- **Breaking:** mining either half takes both and drops exactly one bench, with what stood on or hung from either half going too. A bench beside it stays. It is the same across the edge of a column.
- **A half alone** breaks by itself with one bench. If its other half's column is not loaded, it stays until it is.
- **Facings** (plan 005 phase 12): each half finds the other by its facing, a bench lies across the view each of the four ways and mining either half takes both, R turns it before it is placed and the turn stays with what is held, and stairs rise away from where the player looks while stone placed the same way has no state.
- **The plan:** `player_interaction_place_plan` says what and where, both cells and their voxels, with nothing in the hand, with room, and with the second cell taken, and changes nothing; placing then does what it said.
- **The other stations** (plan 005 phase 6): the smith table takes its four cells 2 by 2 each of the four ways, the back two ahead of the placer, mined by any part with a pickaxe it all goes and one smith table drops, and a block in any of its cells refuses it. The stoneworking bench, the tanning rack and the leather workbench go across the view as the woodworking bench does. Eight blocks are half of a station two long, and twelve are a part of one.

A blast is in `tests/run/mob/creeper/`, and the whole of it played through the built program in `tests/run/playthrough/`.
