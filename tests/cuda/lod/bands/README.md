# tests/cuda/lod/bands

**Purpose:** ctest `render_feature_lod_bands`. Plan 009 phase 04's promises about the bands, on the real card, apart from `tests/cuda/lod/` (`render_feature_lod`), which is at the size where a file is split.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/` and everything under it, and a card. No card fails it, on purpose.

What it checks:

- **The near guarantee** (`the_near_guarantee_and_the_error_bound`): real terrain of the default seed, level 0 held 16 chunks each way and the far field one chunk past it, so that the picture with the far field off is the reference twice over: level 0 everywhere it reaches, and level 0 as wide as the banded picture's. Six eyes (four ways round the start, straight down, and in the rock), each at E 2 and 8 and fields of view 70 and 30. For every pixel whose surface the reference has under 16 blocks, the hit (voxel, face, value, `t` to the bit) is the same bits, and so is the shaded colour against a third picture: the far field on with a pixel error so small that every pixel's own ray stays in level 0 to its edge. Since plan 009 phase 06 a near pixel's shadow ray steps the far field's levels past the light's cone, so it may take a far hill's shadow that level 0 alone did not have; only the primary path is held to level 0. The count of such pixels is printed per eye and must be over 0. E 8 at 180 rows puts d_1 on the near guarantee itself; a render scale of 50 does the same to the bands as twice the E (the bands see only E, the field of view and the rows, through `band_base`), so it is not drawn again at another size.
- **The error bound**, same pairs at E 2 and 70 degrees: past 16 blocks and within what the reference holds (240 blocks), a pixel is wrong when its surface moved by more than E pixels across the screen (`|dt| x |the ray's part along the surface's normal| / (t theta)`), or is sky in one picture and ground in the other. At most 1 % per eye. Another block at the surface (the coarse level's grass-top rule) is printed, not counted.
- **The seam** (`the_seam_on_a_plain_and_a_slope`): hand-made ground, the same in level 0 and in the far field's samples, a plain and a 45 degree slope crossing d_1 and d_2 at E 4 (32 and 64 blocks at 180 rows). No pixel by a band edge is further from level 0's own surface than half a coarser voxel across it; and after 32 still frames with TAA, the ground's row means step no more at a band edge than twice the largest step elsewhere and 3 more. With `LOD_BANDS_PNG_DIR` set the settled pictures are written there, for Ahmed at the gate.
