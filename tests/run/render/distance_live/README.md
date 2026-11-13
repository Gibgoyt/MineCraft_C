# tests/run/render/distance_live

**Purpose:** ctest `run_render_distance_live`, plan 005 phase 14 stage 3, and plan 009 phase 03's edit reaching the far field's levels. During a live game, on the game's own threads against the clock, render distance is changed as the Settings screen changes them, and a lit log is mined. Each change must start to show in under a second and be all drawn within the targets, and the log must leave the picture in under a second.
**Tier:** test.
**Owns:** nothing outside the test arena, and the card's blocks for a world at render distance 64 (level 0 as the game works it out, and the far field) for the length of the test.
**Lifetime:** One run.
**Threads:** main, the real workers, and the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest. It times itself, so it runs alone (`RUN_SERIAL`).
**Depends on:** `tests/harness/`, `run/src/game/` and its part `fill`, and everything under it, and a card. It opens no display and no input device: the picture comes back into a plain buffer, as in `tests/run/render/frame/`'s real-threads test.

## What it does

On Ahmed's seed, `11107447384583428913`, at 640 by 360, with the day standing still and AO and TAA off so a picture changes only with the world:

| Step | What | Held to |
|---|---|---|
| `open_32` | A world opening, from cold, at render distance 32, level 0 as the game works it out | the 5 nearest rings (out to 4 chunks) under 2 s, all of it under 15 s |
| `log_32_before`, `log_32_mined` | The eye moves to stand 5 blocks from the nearest log with open sky in front of it and no log right behind it (a jungle tree's 2 x 2 trunk would show the same bark once mined) and looks at it square on for 2 s; the log is mined through `world/edit`, with the threads stopped for that moment, as the hands would mine it on the sim; the eye goes back | the square round the crosshair, 24 pixels, at least 30% changed from before, in under 1 s |
| `rd_64` | Render distance 32 to 64 | starts showing under 1 s, all drawn under 15 s |
| `rd_16` | 64 to 16 | the same |
| `rd_64_again` | 16 to 64 | the same |
| `log_64_busy_before`, `_change`, `_mined` | Another lit log, the same way, but first render distance 64 to 48, a far field planned again, and the log mined a second into it: Ahmed's tree that took minutes to go | the log as at 32, and the change as above (reported as `rd_48_busy`) |

A second test, `an_edit_reaches_level_1_and_level_2_within_100_ms` (plan 009 phase 03), opens a world at Ahmed's 1920 x 1080, render distance 64 and level 0 as the game works it out, 27 chunks (level 2's band starts at 771 blocks, so it needs the 64), settles it with `game_settle`, far field and all, puts a 4 x 4 x 4 cube of stone, one level 2 voxel, in the open air 320 blocks east of the eye (well inside level 0, so that the level 2 column over it stands on loaded blocks) through `world/edit` with the threads stopped, and starts them. The octant store's watch (`render_octant_watch_arm`) says when the level 1 and level 2 columns holding the cube were sent with stone there: each within 100 ms of the edit, held in an optimized build as the deadlines above are.

**Starts showing** and **all drawn** are the fill-in report's (`run/src/game/fill/`): the first column come or gone, or far column built, and every ring out to level 0's radius drawn with the far field done. The 15 s for a change is the targets' "everything out to render distance 32", taken for every change as the plan says "the targets above to settle". The pictures are only reported: each step keeps the picture at 0, 0.5, 1, 2, 3, 5, 8, 12, 16 and 20 s, as `measure.sh` did, and says how much of each still differs from the step's last.

Each step waits for its fill to be done, and then half a second, with at least 2 s; or 20 s, past which it says what it got and goes on. So on a slow far field the test takes about 3 minutes.

## Settings

| Variable | Does |
|---|---|
| `MC_DISTANCE_LIVE_DEADLINES=0` | Only reports: the deadlines print `MISSED` but do not fail. For measuring code that is known to miss. `=1` holds a debug build to them too, which by default only reports, as its terrain alone misses them (`open_32` took over 20 s at `-O0`) |
| `MC_DISTANCE_LIVE_WAIT_S=<s>` | Each step waits this long for its fill, not 20 s: 300 measures how long today's code takes to settle |
| `MC_DISTANCE_LIVE_SHOTS=<dir>` | The pictures at the marks, and each step's last, as PNGs, to look at |
| `MC_DISTANCE_LIVE_OPEN=<chunks>` | The world opens at this render distance, not 32: what a new world at a raised default comes to (plan 005 phase 8). The step is `open_<chunks>`. The changes after it are the same, so `rd_64` from 64 changes nothing: read only the opening with it |

    MC_DISTANCE_LIVE_DEADLINES=0 MC_DISTANCE_LIVE_SHOTS=/tmp/dl tools/test-or-build.sh timeout 900 ctest --preset release -R '^run_render_distance_live$' --output-on-failure

The game's own lines (`fill: ...`, and the end-of-session report's) are in the output too, as the test calls `game_report`.
