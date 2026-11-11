# tests/run/player/interaction/forward

**Purpose:** ctest `run_player_interaction_forward`. Tests placing ahead, for bridging (plan 005 phase 13): the cell beside the block underfoot that `player_interaction_place_plan` in `run/src/player/interaction/` offers when the aim finds nothing in reach, and the look `player_place_preview` and the click in `run/src/player/` give it. It has its own directory because `tests/run/player/interaction/index.c` is past the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`.

No card, no display, and the program itself is not run. The pictures of a bridge going out over water are `tests/cuda/bridge/`. What it checks:

- **A bridge:** a body on the end of a one-block platform in a lake, holding 20 planks, walks east looking a little down and clicks every 1 or 3 ticks; then sneaking, every 2 or 7. Each time, all 20 go down in a straight line at the platform's height with no gap and nothing anywhere else, the body never drops below the bridge's top, and before every click the preview was `OK` on exactly the cell the click filled.
- **Looking up:** at the platform's edge, from straight down to straight up in tenths of a radian: above level the aim finds nothing and nothing is shown or placed; from level down, wherever the aim finds nothing, the cell ahead is shown and placed.
- **The aim wins:** with a block aimed at, on any face and looking any way, the plan is the same bytes whether looking ahead is allowed or not, and the click places where aimed. An aim into a column not loaded is not a miss.
- **Never in the body:** standing and crouched, on a cube and on a slab, from the middle of the block out to where the feet are all but off its edge, looking each way. An `OK` plan never overlaps the body, and the click places it; on the slab, near the edge, the cell ahead reaches up into the body and is `BLOCKED`, red, with nothing placed.
- **Never with nothing underfoot:** in a jump over a block, in water, over air, and on a wide floor where the ground ahead is not replaceable: nothing to show and nothing placed.
- **Facing:** what goes ahead is the same plan, to the byte, as aiming at that side of the block underfoot with the same look: stairs face the way the player looks, turned by R; a torch hangs from the block underfoot; a ladder lies on its side.
