# tests/cuda/lod/shadows

**Purpose:** ctest `render_feature_lod_shadows`. Plan 009 phase 06's promises about rays a surface sends after the primary ray, on the real card, apart from `tests/cuda/lod/` (`render_feature_lod`), which is at the size where a file is split.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/` and everything under it, and a card. No card fails it, on purpose.

Each test puts its own ground in level 0 by hand and gives the far field a sampler of the same ground (`render_open_lod` with no block radius, so every column of every level is sampled), plus whatever stands only in the far field. "In shadow" is a pixel at all darker in luma (by over half a step; lit, the two are the same bits, and a low sun on a top is faint) than the same picture with `SHADE_FEATURE_SHADOW` off. The reference is the same picture with `SHADE_FEATURE_LOD` off: shadow rays through level 0 to its edge, as before this phase. With the far field on, its pixel error is set tiny where only shadows are being compared, so that primary rays stay in level 0 to its edge too.

What it checks:

- **`a_near_pillars_shadow_is_unchanged`:** a pillar one block across and 20 high, sun 30 degrees up. Every shadow ray that meets it does so within 40 blocks, in level 0, so every pixel level 0 shows is the same bits with the far field on and off; more than 200 pixels are in its shadow.
- **`a_far_towers_shadow_follows_the_cone`:** a tower one block across and 40 high, in level 0 and in the far field's sampler, with a sun so low its shadow runs 330 blocks. Patches along it seen from straight above. Within 108 blocks along the ray (level 0 by the cone) every pixel agrees with the reference. From 108 to 216 (level 1) the tower is not in level 1, whose 2 x 2 columns are sampled at a corner it is not on, and its shadow is gone. From 216 (level 2) the 4 x 4 column it stands in is sampled at the tower, and its shadow is at least the reference's and no wider than that voxel. Held so that a change to either is seen.
- **`near_ground_gets_a_far_hills_shadow`:** a ridge 100 high from 560 to 700 blocks east, in the far field alone. With the sun at 1 in 10 behind it, every pixel of the field under the eye is in shadow with the far field and none with level 0 alone; at 3 in 10, none either way.
- **`far_and_near_sun_rays_agree_at_the_band_edge`:** a plain at E 4 (d_1 32 blocks), sun 20 degrees up: every pixel, level 0's and the far field's, is the same bits as with shadows off, and no far hit has the sun hidden. So the hand-over at L_1 and a coarse voxel's rounding shadow nothing.
- **`the_dim_light_cut_moves_no_pixel_by_more_than_2`:** finds where the cut starts by halves of the sun's height, as `render_sky_fill` lights it (under half a degree over the horizon), and holds that it never fires on midnight's moon nor at 7 degrees, and that full cloud (`render_weather_apply`) moves it higher. Then a pillar drawn with the sun either side of that edge, one picture with shadow rays and one with none: no channel of any pixel moves by more than 2.
- **`far_leaves_stop_after_four_holes`:** `render_feature_shapes`' line of leaves with a hole on the line and a stone behind. From 20 blocks four leaves let the stone show; from 100, past `alpha_far_from_blocks`, the fourth leaf stops the ray on the leaves' mean colour; with three leaves the stone, the fourth surface, shows from 100 as from 20.
