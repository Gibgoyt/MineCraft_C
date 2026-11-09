# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 8: How far you can see

**Status:** not started   (updated 2026-11-05)

## Goal

Settings says plainly how far you'll see, in blocks as well as chunks, and the default lets you see well into the distance at a good frame rate.

**Ahmed's words:** "Not sure what a chunk size is, how many blocks that is, but I am not able to see really really far away, not sure if 32 chunks is not actually 32, or a chunk is a lot smaller than I think, or if there is a rendering issue??"

## What it is today

**It's working as set. There's no evidence of a rendering bug.**

- **A chunk is 16 blocks wide,** the same as Minecraft. Render distance 32 means **512 blocks** in each direction.
- Ahmed's `settings.yaml` has `render_distance: 32` and `full_detail: 32`. The far field (level of detail) only draws beyond the full-detail distance, so with both at 32, **nothing is drawn past 512 blocks.**
- The render distance slider goes to **512 chunks, which is 8192 blocks**. Past the full-detail distance, terrain is drawn coarser. Measured in plan 001: about 150 FPS at render distance 128, and about 107 at 512 (`bench_world`).
- The slider shows only "Render distance: 32 chunks", which doesn't say how far that is.

## Todo

- [ ] Show blocks on both sliders in `run/src/ui/screen_settings/index.c`: "Render distance: 32 chunks (512 blocks)"
- [ ] Explain the two settings in one line each on the Settings screen: full detail, and how far to draw at all
- [ ] **Only after phase 14 is fixed,** raise the default render distance, keeping full detail at 32, to the highest that still holds the frame target on `bench_world`. Measure, and record the numbers here
- [ ] Check the far field looks right from a mountain at the new default, with screenshots checked by eye. Taller terrain from plan 003 phase 8 may need it tuned
- [ ] Add a test that the label shows blocks, and that a `settings.yaml` without the new default still loads as before

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^(run_ui_screens|run_settings|render_feature_lod|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 900 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: in Settings, set render distance to 128 or more, keep full detail at 32, and look out from a mountain. Then try the new default.

## Commits

Appended as they land:

## Issues found

**Ahmed can see further today,** with no code change, by raising the render distance in Settings.

**Raising render distance is what broke the game, 2026-11-09.** Ahmed raised it to 64, as this file suggested. That turned on the far field, whose builds then took every worker, so near chunks and mined blocks waited for minutes. See phase 14, "Identified". Until phase 14 is fixed, **render distance 32 with full detail 32 is the setting that plays properly**, and this phase's raised default waits for it.

