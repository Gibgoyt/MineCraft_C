# tests/cuda/resolution

**Purpose:** ctest `render_feature_resolution`. Tests plan 008 phase 05's pictures drawn smaller than they are shown: the ladder's sizes,
`post_upscale` (`cuda/src/post/`), the overlays drawn after it at the output's size (`cuda/src/entity_rays/`), the frame that draws at a rung
and sends the output's size home (`run/src/render/frame/`, `cuda/src/frame/`), and the history of TAA and AO across a change of size
(`cuda/src/post/`, `cuda/src/shade/`). Its own suite, as `tests/cuda/post/index.c` and `tests/run/render/frame/index.c` are near the
standard's split.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there,
`run/src/render/`, and everything under it, and a card. No card fails this test, on purpose.

Compute only. It opens no display and no input device.

What it checks:

- **On paper:** a rung's side is the render scale's old arithmetic (1920 at 85 % is 1632, 1080 at 70 % is 756); the pixel under an output
  pixel is itself at the same size and the nearest below; a rung's camera has the output's field of view and aspect.
- **The upscale of a flat colour is that colour,** to the bit on every output pixel; a ramp comes up as the ramp within a byte; a larger
  inner picture, no pixels and blocks too small are refused.
- **A sky drawn at 70 % comes up as at 100 %,** within 1 in 255 on every pixel.
- **History survives a size change:** a still room with every effect but AO, 30 frames at 100 %, 1 at 50 %, 30 at 100 %, against 61 at
  100 %: no channel of any pixel differs by more than 2 in 255.
- **AO's history survives a size change:** a room converged over 200 frames, then a frame at 50 % and one at 100 %: at least 97 % of the
  surface pixels keep their samples each time (indexed by this frame's width they would land on other surfaces and start again). The plan's
  2-in-255 test is not asked of AO: a pixel that takes a neighbour's history across a change takes that neighbour's sample noise.
- **The outline is one pixel at any rung:** a block's outline drawn at 50 % changes as many output pixels as at 100 %, within 20 %, every one of
  them the outline's own colour: drawn smaller and brought up it would be twice as wide and blended.
- **The render scale at 70 % is its old picture:** what is drawn is the very picture a frame of 224 by 126 drew before, to the byte, and what is
  shown, brought up, differs from the old nearest-pixel bring-up by a mean under 3 in 255.
