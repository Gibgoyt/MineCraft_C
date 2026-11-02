# tests/run/render/frame

**Purpose:** ctest `run_render_frame`. Tests a whole frame on the real card, from a world in memory, through the real encoder and upload queue, the rays, shading and post, back to a picture: `run/src/game/`, `run/src/render/` with `frame`, `cuda/src/shade/`, `cuda/src/post/` and `cuda/src/frame/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main, the real workers, and in the test that needs them the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** a face's texture coordinates, a texel's place, the mip level, the sRGB curves and the tone map, sky and fog, on numbers worked out on paper. The camera: which way each key and the mouse move it.
- **Against an exact reference:** a world of a few blocks, grass and a furnace among them, seen from all sides. Every pixel against a trace in `float64_t` over a list of boxes, with its own table of faces, looking up each texel through `run/src/texture/`. This is what says that no face is mirrored or upside down.
- **The real path on real terrain:** what the rays found against the map, voxel for voxel, against a separate trace through the map. Every pixel against what its hit says it should be. The window scrolling. A column that wrapped out of its slot not being seen is held to in `tests/cuda/traverse/`, since a settled world never shows it here.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^run_render_frame$'

- **The built program:** the same script gives the same PNG files byte for byte, flying changes them, and bad flags are refused. KMS is not run here: only Ahmed can, on a text console.
- **The real threads:** `sim`, `gpu` and a `main` loop on a plain buffer draw the same picture as a run with no threads, take nothing from the heap once running, and stop clean.
- **What a frame refuses:** targets too small, and a material with no texture, which draws magenta.

`fixtures/` holds only those pictures. Any change to the terrain, the test world's surface, or the textures changes them, and they are made again and looked at.

The real loop through `display_frames_run`, with the guardian forked, is `tests/run/game/`.
