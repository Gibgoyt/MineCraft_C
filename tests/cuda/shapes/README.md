# tests/cuda/shapes

**Purpose:** ctest `render_feature_shapes`. Tests non-cube blocks and holes in textures, on the real card, through the real encoder and upload queue: the shape table in `cuda/include/traverse/`, the rays going on through holes in `cuda/src/shade/`, the table `run/src/render/frame/` sends, and the settings.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** one ray against each shape, from each side that matters, with its `t` and face worked out on paper. A slab's top at a half, the step of the stairs, a plate one texel thick, a torch, a torch on a wall leaning each of its four ways, rays that pass a box by inside its voxel, a ray from inside a box, one told to start past a box, a section all of one shape with no masks to go by, a material with no row, and a table too short.
- **Under plain light:** everything here draws with shapes and holes alone, so the reference needs only each kind of face's one brightness. The sun, stored light and fog are `tests/cuda/sky/`'s.
- **Against an exact reference:** a floor and a block of each shape, from all sides and from near and far. Every pixel against a trace in `float64_t` over its own list of boxes, written apart from the card's table, from surface to surface through holes in the real textures. Pixels near enough an edge for rounding to choose are left out, as `tests/run/render/frame/` does, and at least 24 pictures' worth must be left in, with every block and face seen.
- **The settings:** the same comparison with shapes off, with alpha off, and with both off, each held to what it says.
- **The coarse pass:** rays with and without it find the same things, to the bit, while it does start tiles later than 0.
- **The layer limit:** a line of leaves with one hole lined up all the way. With 16 leaves the pixel stops on the last of them, with its mean colour. With 15 it reaches the stone behind.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_shapes$'

- **The cost:** each setting on real terrain and on a field of plants and leaves, at 1080p, on the card's own clock, printed as `cost, ...` lines. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md`. It also says that real terrain does have something not a cube in view, and that the setting changes the picture.
- **The built program:** `--no-shapes` and `--no-alpha` change the picture of a headless run, and mean nothing without `--test-world`.

`fixtures/` holds only those pictures.
