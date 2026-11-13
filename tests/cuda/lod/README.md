# tests/cuda/lod

**Purpose:** ctest `render_feature_lod`. Tests the coarse grids in `run/src/render/lod/` and the far rays in `cuda/src/lod/`, with their share in `cuda/src/shade/`, on the real card, through the real encoder and table, with the setting in `run/src/render/frame/` and the flags in `run/src/main/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, under 3 GiB of voxels.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/game/`, and everything under it, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display and no input device, and runs the built program only with `--headless`.

What it checks:

- **By hand:** which levels a distance has, and that every distance is reached. What a sampled column makes as coarse voxels. What a far surface looks like.
- **The grids:** every level's columns as a real kernel reads them, at places all over, against the samples, on ground a test works out and on real terrain.
- **The coarsest first, and a change of levels:** the first columns asked for are the coarsest level's, nearest first; a plan that drops a level keeps the other, shown at once, on the card as it was, with nothing built; and back again, the new fine level fills while the kept one shows.
- **Following the eye:** a level moved one column shows no less on the very frame it moved, builds exactly one new row, holds no more after going there and back, and after a long jump shows nothing old. A pool with no room says so, and shows only what is whole.
- **Rays:** random rays on the card through every level, against a march in `float64_t` voxel by voxel, from the same samples.
- **The sun:** a wall's shadow on the ground where it must fall, and no ray for a surface that faces away.
- **The picture:** every pixel's far ray against the march, every far pixel's colour against the host's, and no far ground drawn nearer than where level 0 let its ray go (plan 009 phase 04: its `t_end`, its square's edge, or a column it does not hold); `ld_plan` stands in for the planner the far field had before, so that the levels can be counted on paper. The band tests, near guarantee and seam, are `tests/cuda/lod/bands/`. A wall hides it, and glass lets it through. The setting off is the same bytes as no far field at all. Fog takes it as it takes anything.
- **The machine:** no heap in a frame, moving, with columns on their way, and a kernel asked for what is too small says so and starts nothing.
- **Golden pictures:** `fixtures/*.png`, at most 2 out of 255 apart on any channel. Ahmed's card and driver made them. To make them again, after looking at them:

      MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^render_feature_lod$'

  `real_near.png` and `real_far.png` were last made for plan 003 phase 8 stage 7: the new land. `real_far.png` was made again at that phase's gate, as frozen seas now show as ice at every level, 398 pixels past the tolerance. Their eyes moved with it. Over the start the eye looks further down, as it was nearly all sky, and the far one is at z = 1240, as z = 420 at the world's middle is inside a range now. The `hills*` pictures stand on made-up ground and came out the same bytes. Plan 009 phase 04 made `real_near.png` and `real_far.png` again on plan 006 phase 03's land with its bands: coarser levels sooner, and past level 0's two chunks `real_near`'s far field is made from the loaded blocks, so the snow the census laid on the hill shows white. Plan 007 phase 03 made both again on its plateaus: the start is on a slope falling away from the eye, so `real_near`'s eye looks down at -0.75 (at -0.45 it was nearly all sky), over jungle crowns and the hazed ground below; `real_far` shows a coast under terraced plateaus and ranges.

- **The cost:** on real terrain, at 1080p, on the card's own clock, printed as `cost, ...` lines: at R = 512, on a release build only, as a debug one's terrain takes too long. Nothing here is a limit: the numbers are recorded in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-6-lod-clipmaps.md`.
- **The real threads:** the gpu thread builds it beside the sim, and goes on with the eye flying.
- **The built program:** a script looks at the far field of real terrain: `--no-lod` changes it, two runs give the same bytes, and the flags are refused without a world.
