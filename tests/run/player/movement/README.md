# tests/run/player/movement

**Purpose:** ctest `run_player_movement`. Tests `run/src/player/` with `movement`, and `run/src/game/`'s walking.
**Tier:** test.
**Owns:** the test arena, and what its world, game and card blocks take, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; and for the tests of the game, the real workers and the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `tests/run/input/fixtures/`, `run/src/player/`, `run/src/game/` and everything under it, and a card for the tests of the game.

Compute only. No real display, console or input device is opened: the keys come through the fake kernel, and the program always runs `--headless`.

What it checks:

- **The wind** (plan 004 phase 08, in `fixtures/wind.h`, apart from `index.c` to keep it under 1500 lines): a 60 blocks/s hurricane drifts a standing body west at 2.00 blocks/s, a 30 storm at 0.80, along a slant too; walking into it is 2.3; a jump in it goes under 2 blocks; 10 and under move it not at all, to the byte; a wall 2 blocks upwind makes it 0.50, and none downwind; water and a ladder are the same bytes as with no wind; and no wind is `player_movement_tick` to the byte over 2000 random ticks.
- **Each mode, measured:** walking, sprinting, sneaking, jumping, falling, swimming, lava and climbing against the numbers in `run/src/player/movement/README.md`, on worlds built by hand.
- **Steps, edges and low ceilings:** a slab and stairs walked up, a block not; sneaking at an edge; staying crouched with no room to stand.
- **Waiting:** a body stays put until its column is there, and cannot walk into one that is not.
- **Any input at all:** random intents through a random world leave the body clear of everything, and give the same bytes twice, with nothing from the heap.
- **The threads' side of it:** an intent's every field through the one word, a tap between two ticks, no one at the controls, and render interpolation along and between ticks.
- **The game:** real threads walk a real world at the speed the tick says, fake keys through `game_loop_source` do the same, and the built program walks a script to the same bytes each time.
