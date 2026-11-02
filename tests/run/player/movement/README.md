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

- **Each mode, measured:** walking, sprinting, sneaking, jumping, falling, swimming, lava and climbing against the numbers in `run/src/player/movement/README.md`, on worlds built by hand.
- **Steps, edges and low ceilings:** a slab and stairs walked up, a block not; sneaking at an edge; staying crouched with no room to stand.
- **Waiting:** a body stays put until its column is there, and cannot walk into one that is not.
- **Any input at all:** random intents through a random world leave the body clear of everything, and give the same bytes twice, with nothing from the heap.
- **The threads' side of it:** an intent's every field through the one word, a tap between two ticks, no one at the controls, and render interpolation along and between ticks.
- **The game:** real threads walk a real world at the speed the tick says, fake keys through `game_loop_source` do the same, and the built program walks a script to the same bytes each time.
