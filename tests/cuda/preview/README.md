# tests/cuda/preview

**Purpose:** ctest `render_feature_preview`. Tests the preview of placing (plan 005 phase 10) on the card: `entity_rays_overlay`'s preview in `cuda/src/entity_rays/`, and its snapshot and setting in `run/src/render/entities/`, through the real encoder and upload queue. That the preview is where the click puts a block is `tests/run/player/interaction/preview/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/` and everything under it, and a card. No card fails this test, on purpose: a skip would read as a pass.

Compute only. It opens no display and no input device, and does not run the built program. What it checks, for a cube, a wall torch, a slab, the bench of two cells and stairs facing east:

- **The blend:** half the colour, lit by the face it is on, over the picture, on numbers worked out on paper. Red for blocked, blue for OK.
- **The picture:** every pixel the preview changes is the picture without it under the blue, on one of the six faces, and nothing else changes. Blocked, the same pixels are red. With the Settings box off it is the same bytes as with nothing to show, and on again it is back.
- **The shape:** the preview covers the pixels the block itself covers once it is placed, apart from at most one in twenty along the edges, so it is the card's own shape, turned by the facing.
- **Hidden:** behind a wall or under the floor it changes nothing. A frame with it takes nothing from the heap. More cells than `ENTITY_RAYS_PREVIEW_CELLS` is refused by the kernel, and cut to that by the snapshot.
- **Golden pictures:** `fixtures/cube.png`, `wall_torch.png`, `slab.png`, `bench.png`, and `blocked.png` (the cube red), with every effect a player has, at most 2 out of 255 apart on any channel. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_preview$'

- **The cost:** a bench's preview two blocks ahead on real terrain at 1080p, beside none, on the card's own clock, printed as `cost, ...` lines. The preview runs in the overlay, which the frame counts in post. Nothing here is a limit: the numbers are in the phase file.
