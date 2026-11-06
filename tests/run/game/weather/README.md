# tests/run/game/weather

**Purpose:** ctest `run_game_weather`. Tests plan 004 phase 08's sim side of the weather, `run/src/game/weather/`: lightning by hash, the forced kinds, and in a real game on the card the slots it sends, the rain top map following edits, and a world with no climate sending nothing.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers. The test is the reader of the weather's exchange, as the thread that draws would be.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h`, `run/src/game/` and everything under it, `run/src/climate/systems/`, and a card.

What it checks:

- **Lightning on replay:** eight thunderstorm cells over 400 seconds strike at the same seconds and places twice, as often as 0.05 a second times their strength says (within four standard deviations), each within its cell; another seed strikes otherwise. A bolt's joints are the same twice, from 180 above the ground (the world's top, near it) to the ground exactly, none more than 8 blocks out.
- **The forced kinds:** each name read back, and the numbers each stands for.
- **No climate:** the slot is sent with nothing in it, the wind is still, and the picture drawn with it is the clear day's to the byte.
- **The rain top follows edits:** the map is round the eye's column, and a block put over a column beside it, then taken away, is in the next slot each time, with a new revision; a tick with no change keeps the revision.
- **Forced thunder:** a strike every 2 seconds 300 blocks east, seen for 4 ticks, with the flash of its distance; a forced hurricane's wind is on the body at once.
