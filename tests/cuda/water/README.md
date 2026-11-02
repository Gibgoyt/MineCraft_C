# tests/cuda/water

**Purpose:** ctest `render_feature_water`. Tests rays going on through glass, ice and water, the one ray off the surface of water, and the colour water takes with depth, on the real card, through the real encoder and upload queue: `cuda/src/shade/`, the settings in `run/src/render/frame/`, and the flags in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** what water reflects at each angle, what a length of it lets past and gives back, fog behind a surface coming to what its own distance's fog is, and which surfaces are only the far side of what a ray just left, on numbers worked out on paper.
- **Against an exact reference:** every pixel against a trace in `float64_t` from cell to cell, written apart from the kernels, near to far through what is see-through, with each surface's sun ray and each surface of water's one reflection. Pixels near enough an edge for rounding to choose are left out, shadow edges and the edges of what a reflection shows among them. Done for: a stone basin with a shelf and a pit, a block of bricks on its bed, a tower and leaves over it, a glass wall two thick that stands in it, ice on it and water hanging in the air, at five times of day, from above and low down; from under the water, up at the surface and along the bed; with fog ending inside the scene; with each setting off. It holds that water, glass, ice, the sun through water and a reflection that met a block were all really counted.
- **Deeper is darker and bluer:** by hand, 1, 2, 4, 8 and 11 blocks deep over one pale floor, looked at straight down. With tint off, the bed looks the same at each.
- **The sun in the water:** looking along the mirror way to the sun shows its disc, and the same look at the same place with reflection off does not. What a reflection adds grows the lower one looks.
- **A tower upside down:** on a flat sea, the mirror lands on a tower's face where the arithmetic says, and past its top it is the sky.
- **Glass:** seen through a wall 4 thick, only the outer faces draw. With nothing part-covered and no glass in view, this stage's setting changes no pixel at all.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_water$'

- **The cost:** each setting on real terrain and over a sea with nothing else in view, the worst case on purpose, at 1080p, on the card's own clock, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-3-water-glass.md`.
- **The built program:** the three flags are taken with a world, and refused without one.
