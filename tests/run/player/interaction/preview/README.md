# tests/run/player/interaction/preview

**Purpose:** ctest `run_player_interaction_preview`. Tests that the preview of placing (plan 005 phase 10) can never disagree with the click: `player_interaction_place_plan` in `run/src/player/interaction/` and `player_place_preview` in `run/src/player/`. It has its own directory because `tests/run/player/interaction/index.c` is past the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`.

No card, no display, and the program itself is not run. The picture of the preview is `tests/cuda/preview/`. What it checks:

- **Every face, shape and facing:** a cube, a slab, stairs, a torch, a ladder, a vine, a sapling, a lily pad, glass and the woodworking bench, on each of the six faces of a block, looking each of the four ways and turned by R none to three times. Where the plan says `OK`, the click puts down exactly its cells with exactly its voxels, both of a bench's, for one of the hand. Anything else places nothing and keeps the hand. Asking twice changes nothing and says the same. Each of `OK`, `BLOCKED`, `NONE` and a plan of two cells comes up.
- **What can be overwritten:** aimed at tall grass, the plan is the grass's own cell, and the click agrees.
- **Blocked:** a column not loaded, the body in the way, a torch with no ground and a bench's second cell taken are each `BLOCKED`, with the cells it would have taken, and place nothing.
- **Through the player:** `player_place_preview` after a tick is what the next tick's right button puts down, for each held block, looking 16 ways round at three tilts and straight down into the body, at a floor, pillars, a step and glass.
