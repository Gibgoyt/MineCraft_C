# tests/cuda/sky

**Purpose:** ctest `render_feature_sky`. Tests the sky, the day, fog, the one sun ray and stored light, on the real card, through the real encoder and upload queue: `cuda/src/shade/`, `cuda/src/post/`, `render_day_fill` and the settings in `run/src/render/frame/`, the clock in `run/src/game/`, and the flags in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers, and in the clock's test the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** where the sun is at sunrise, noon, sunset and midnight, and the moon's being what lights the night. A day in 4000 steps with no jump in any colour, nor in what falls on a surface when the light swaps from sun to moon. Noon's top of a block coming out as its own colour. Each light level, each face against a direction, and what adds up on a surface, on numbers worked out on paper.
- **Against an exact reference:** every pixel against a trace in `float64_t` over a list of boxes, written apart from the kernels, through holes in the real textures to a surface, then through holes again to the sun. Pixels near enough an edge for rounding to choose are left out, shadow edges among them. What adds up the light is the same `shade_surface_light` as the card's, which the hand test holds. Done for: nothing but sky, all round and through a day; fog, on and off, at noon, dusk and night; a floor, tower, arch, slab, stairs, plant, tree and glass at five times of day; a floor with all 256 stored light bytes.
- **A shadow by hand:** a tower 10 blocks tall at noon: the ground where its shadow must fall, up to where the tilt says its tip is, and the ground just past it and to each side, where the sun ray must change nothing at all.
- **Discs and fog:** straight at the sun, straight at the moon, and a wall past the fog in front of the sun, which must show the sky with no disc.
- **Stored light on the card:** read back from the hits, voxel by voxel, and from a section all of one block and one light, which is a form of its own.
- **The settings:** each off, against the same reference. All off is what was drawn before this stage: `tests/cuda/shapes/`' golden pictures are unchanged, and it draws with these off. A shadow only ever takes light away.
- **The clock:** through the real threads, with what `game_loop_frame` is told has passed: it wraps past midnight, and the setting stops it.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_sky$'

- **The cost:** each setting on real terrain and under a forest's crowns, the worst case on purpose, at 1080p, on the card's own clock, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-2-sky-shadows-light.md`. It also says what each setting changes on real terrain, and holds that each does change something, but for stored light, which has nothing to change there until Stage 11.1 stores some.
- **The built program:** the flags change the picture of a headless run, one script gives one set of pictures, the sun moves in a script and with `--no-day-cycle` does not, and wrong uses are refused.

`fixtures/` holds those pictures, and `rig.h`, what opens a real run and a world with nothing in it, kept apart to keep `index.c` under the line where the standard asks for a split.
