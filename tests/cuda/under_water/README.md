# tests/cuda/under_water

**Purpose:** ctest `render_feature_under_water`. Tests what eyes under water see: `post_under_water` in `run/include/gpu_bridge/post/`, its colours in `run/src/render/frame/`, the way a ray from an eye in the water counts in `cuda/src/shade/`, and the flag and the setting in `run/src/main/`, `run/src/player/` and `run/src/game/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** how much is water at each distance, and what comes of a colour, on numbers worked out on paper.
- **The settings:** the colour follows the day and is still there at midnight, the flag survives a change of time and of effects, and with the setting off nothing shows of it.
- **A sea of our own:** flag off, or setting off, is the same picture to the byte. On, far water is exactly the water's colour, which only holds if a ray from an eye in the water counts by how far off the water stops. A bed further off is nearer the water's colour, the sky through the surface still shows, and an eye in a kelp stalk, which has no water in its own cell, still sees far water and not black.
- **The real thing:** `fixtures/dive.script`, in the built program, with a real body from the start of the default seed out to sea and down: nothing changes on land, what was black is not, and the pictures match. Golden pictures at most 2 out of 255 apart on any channel. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh timeout 600 ctest --preset debug -R '^render_feature_under_water$'

- **The cost:** at 1080p, on the card's own clock, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/003-ahmed-bohemoth-initial-game-fixes-phase-09-breath-underwater.md`.
- **The flag:** taken with a world, and refused without one.
