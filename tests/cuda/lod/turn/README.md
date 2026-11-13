# tests/cuda/lod/turn

**Purpose:** ctest `render_feature_lod_turn`. Plan 008 phase 06's turn cache on the real card, apart from `tests/cuda/lod/` (`render_feature_lod`), which is at the size where a file is split.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test.
**Lifetime:** One run.
**Threads:** main and the real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/cuda/sky/fixtures/rig.h` for a real run with a world holding only what a test puts there, `run/src/render/` and everything under it, and a card. No card fails it, on purpose.

The world is rolling hills, the same in level 0 (4 chunks each way) and in the far field's samples (levels of 2, 4 and 8 blocks), drawn at 320 by 180 from 6 blocks over the ground, looking a little down, so that most of a picture is far field. The cache's tally (`render_lod_turn_tally`) counts far rays traced and read from a texel. Settling ends with `turn_revise_frames` more far-field frames, so that a revision put off for the last columns sent lands before a test and not in it.

What it checks:

- **`a_still_eye_draws_the_same_far_field`.** 60 frames at a fixed eye with TAA (so jittered), the cache on at the game's side and off, each from a newly opened frame: every picture the same bytes. No texel is read, as no frame is a turn, and every far ray was written to the cache.
- **`the_cache_takes_what_its_side_says_from_the_voxels`.** The block the first frame takes is `lod_turn_cache_bytes` of the tuning file's side, from the voxels' pool, printed beside what the pool held, and goes back when the far field closes.
- **`a_turn_back_traces_nothing_far`.** Side 256 (a texel about a pixel of 180 rows, as 1024 is of 1080). Turn 60 degrees right over 10 frames and back over 10: on the way back, far rays traced are under 5 % of the way out's, and each frame of the way back reads texels. Each picture of the way back is held to the same camera drawn again with the cache off: under 2 % of pixels off by more than 24 in a channel.
- **`moving_a_quarter_block_or_a_far_change_retraces`.** After a little turn has filled the cache, a turn from the same eye reads texels; one from an eye 0.3 blocks off reads none; nor does one after `render_lod_t.revision` moved on.
