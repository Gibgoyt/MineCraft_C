# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 14: Loading and render distance speed

**Status:** stage 1 measured and stage 3 test in place (agent 14c); the far field's part of stage 2 built and tested (agent 14b); job priorities and the far-field cap with agent 14a   (updated 2026-11-10)

## Goal

A world is playable as soon as it opens, and fills in quickly. Changing render distance or full detail mid-game shows the change in under a second, without the game stuttering. Agents prove this in a running game, not only in benches.

**Ahmed's words, from `../prompt.md`:**

- "initial game load is very very laggy! changing chunk render distance and full resolution also makes things quite freiking laggy and we need to fix this please!!"
- "agents need to, during a live game, please change render distance and see a very very quick update in less than 1 second please!!"
- "the game feels absolutely attrocious to play! it takes about 5 minutes to render everything in nicely!!"
- "I think that these rendering issues picked up after we tried adding sound! please look into this!"
- "there are now lots and lots of rendering issues that I will dive into later"

## What is known

**Sound is not the cause of what Ahmed saw.** The binary in his paste is dated Nov 4, 17:48. Sound first landed in commit `c8429feb` on Nov 5 at 19:14, so that build had no sound code. **What it did have:** plan 003's taller world (phase 11) and parts of the new world generation (phase 8). When phase 11 was built, it measured terrain at up to 31% fewer columns a second, and opening a world up to 60% slower, before a later fix (plan 003 phase 11, Measured). Since then, plan 004's climate system has also landed. It keeps climate tiles 5224 blocks past the render distance, and its final check found the frame target missed at render distance 32. So the cause has to be measured on today's `main`, not guessed.

**From his log** (2115 s of play at 1920×1080, render distance 32, full detail 32):

| Number | Value | What it says |
|---|---|---|
| Frames | Mean 8.06 ms, p50 5.29, **p99 49.9, max 144.5** | Smooth on average, with heavy stutters |
| Worst passes | `shade` max 126.2 ms, `readback` max 79.1, `post` max 42.4 | Spikes on the card and in reading frames back |
| FPS on screen | 69.6, against 8.06 ms frames | The gap noted in plan 003 as "B" |
| Columns made | 4245 in the whole session | About the 4225 that render distance 32 needs, so the world kept up eventually |

**Opening a world, measured in plan 003 phase 11** (`bench_world`, before plan 004): 6.4 s on plains and up to 20.4 s in a cave at render distance 32, about 118 s at 128, and 127 s at 512. Ahmed's "5 minutes" is worse than any of these, so the bench may not be measuring what he sees.

## Identified, 2026-11-09: the far field starves everything else

**Ahmed's report:** at render distance 64, full detail 32, a new world is drawn as flat untextured slabs around him (`~/Pictures/10.png`), and a log he broke stayed drawn (`~/Pictures/11.png`). His log: `far field: 2 levels, 408 columns built in 354.9 s of workers' time` in a 35 s session, which is about 10 workers busy the whole time.

**Reproduced** on a private Xvfb in real time, with his `settings.yaml`, `--no-sound`, the release build of `main` at `5e675623`. Screenshots were taken at 1, 3, 6, 10, 20, 40 and 60 s, then the ground underfoot was mined and looked at from inside the hole.

| | Render distance 64, full detail 32 (Ahmed's) | Render distance 32, full detail 32 |
|---|---|---|
| At 1 s | **Sky only, nothing drawn** | Everything near drawn, textured |
| At 3 s | **The ground around the player as untextured grey and coloured slabs**: the far field standing in for missing full detail, as in his 10.png | Same as at 1 s |
| At 6 to 60 s | Near blocks fill in, but **black and untextured patches stay in view at 60 s** | Complete |
| After mining 4 blocks down | **The whole screen black**, looking down and around | The hole's walls drawn and lit, the dropped dirt visible, the sky seen out of the top |
| Far field | **1325 columns in 1341.6 s of workers' time, in 87 s: all 16 workers busy all the time**, about 1 s of worker time per far-field column | None |
| Columns made in that time | 2521 | 3209 |
| Sections sent | 38,462 | 112,604 |
| Worst frame | 25.0 ms, with `readback` up to 21.8 ms | 6.9 ms |

**Why,** from reading the code:

1. **One shared queue, no priorities.** `run/include/job/index.h` has `job_submit` and `job_spawn`, and nothing else: no priority or class. Near chunks, re-encoding a section after a block edit, light, climate tiles and far-field builds all wait in the same first-come queues (`run/src/job/README.md`: own queue, then the injector, then stealing).
2. **Far-field columns are very expensive.** About 1 s of worker time each, from both logs. With render distance 64 there are thousands of them, so they fill every worker for minutes.
3. So **the work that matters most waits:** the chunks the player stands in, and the re-encode that makes a mined block disappear. That explains the slabs at 3 s, the black patches, and the log that stayed in his 11.png. It matches his "takes about 5 minutes to render everything in nicely".

**Not yet proven:**

- **The black screen in the hole.** Most likely the new faces' light, or their re-encode, waiting in the same queue. But the two runs had different seeds and different ground, a forest at 64 and a mountain at 32. Stage 1 repeats both on one seed.
- **Ahmed's exact 11.png.** A log staying drawn after it was mined wasn't reproduced in this run, which mined dirt and then couldn't see. Stage 1 checks a log on one seed at both distances.
- **Why a far-field column costs about 1 s.** It needs the profiler. It may sample the full 3D terrain where a height estimate would do, or wait on climate tiles.

**Sound and the climate system:** the 32 run had both and drew correctly, so neither is the main cause. Stage 1 still records the numbers with each off.

Pictures and the script are kept in `/tmp/claude-1000/-home-ahmed-Projects-Games-MineCraft-C/52c310b0-7c8d-4c06-a48e-20883c52be21/scratchpad/render/`, as `rd64_sheet.png`, `rd32_sheet.png` and `repro.sh`. That's `/tmp`, so a reboot clears them.

## Measured live, 2026-11-09: how long a settings change takes

**Ahmed, after the first identification:** "it takes so so so so long on new world generation to freiking render in everything! i mine a tree and after it has all settled 5 minutes later then the tree shows as disappeared", and "when changing full detail and render distance there are so so many rendering issues and it takes 5 entire minutes". He asked for render distance changes to be tested, to plan the fix.

**How:** on Ahmed's seed `11107447384583428913`, which spawned at his exact point, `-7.5, -7.5, 40`. A private Xvfb in real time, 1280×720, `--no-sound`, weather off so rain doesn't count as change, the release build of `main` at `adf33b79`. The player stood still, looking a little down, with the settings changed through the real pause menu and sliders. Each screenshot was compared with the last of its phase: the share of pixels still differing, at 6% tolerance.

| Step | What happened | Share still differing from the settled picture |
|---|---|---|
| **New world, render distance 32, full detail 32** | Complete at 1 s. | 0% from 1 s to 30 s |
| Mining at 32 | The hole was drawn as it was dug. | — |
| **Render distance 32 → 64, live** | **No change for at least 16 s.** New far terrain appeared somewhere between 16 and 150 s, and the picture **settled between 180 and 240 s.** That's the "5 minutes" Ahmed feels. | 2.0% from 0.5 s to 16 s, 0.8% at 150 s, 0.1% at 180 s, 0% at 240 s |
| **Full detail 32 → 16, live, at 64** | **No change for 45 s,** then all at once at 60 s, **and the whole scene went darker** and stayed darker. That's one of Ahmed's "so many rendering issues". | 6.4% to 6.5% from 0.5 s to 45 s, 0.3% at 60 s, 0% at 90 s |
| Mining at 64 | **Inconclusive.** The new blocks were dug at the bottom of a dark hole the earlier mining had made, so their change can't be seen. Ahmed's report of about 5 minutes stands until a test aims at a lit block | — |
| Far field over the 8.5 min | 3 levels, **2172 columns in 1997.2 s of workers' time**: every worker, the whole time | — |
| Frames | Fine on average: mean 3.5 ms, p99 4.1 ms. One spike to 32.5 ms, with `readback` at 29.6 ms | — |

**Why changes look frozen, then jump,** from `run/src/render/lod/README.md`: a far-field column being made again keeps "the old one shown until the new one has come". After a settings change, the old picture stays until the slow rebuild catches up, then flips. The rebuild is slow because of the starvation above.

**Why a mined tree can take minutes to vanish,** from the same README: the far field "draws no edits" of its own, only what's already saved. While the near chunks wait for workers, the far field stands in for them, as at 3 s in the first run, and still shows the tree. It only goes when the near chunk is finally built and drawn. Not reproduced exactly yet: stage 1 aims a test at a lit tree.

**The darkening after a full-detail change** is a separate defect with no cause found yet. Stage 1 starts by comparing stored light and the far field's shading before and after.

The pictures, timeline and script are kept in `/tmp/claude-1000/-home-ahmed-Projects-Games-MineCraft-C/52c310b0-7c8d-4c06-a48e-20883c52be21/scratchpad/render/measure/`, as `shots/`, `timeline.txt` and `../measure.sh`. That's `/tmp`, so a reboot clears them.

## Measured, 2026-11-10: the baseline before stage 2 (agent 14c)

**How.** `run_render_distance_live` (`tests/run/render/distance_live/`), the release build of this branch at `eb650bd3`, which is `main` at `7aea47e0` plus the fill-in report and the test, so the game itself is today's. The game's own `sim` and `gpu` threads against the clock, with main asking for a picture at 120 Hz into a plain buffer: no display, no X. Ahmed's seed `11107447384583428913`, 640 by 360, 16 workers, the day still, AO and TAA off so a picture changes only with the world. No menu, so no climate tiles and no save. Run with `MC_DISTANCE_LIVE_DEADLINES=0 MC_DISTANCE_LIVE_WAIT_S=300`, so each step waited for its fill to finish. Times are from the change (the end of `game_reconfigure`) or from `game_open`; **first** is the fill-in report's first column come or gone or far column built, **seen by 1 s** is how much of what the change does to the picture had happened at 1 s, and **all** is every ring out to full detail drawn and the far field done.

| Step | First | Seen by 1 s | Rings to 4 | All rings | Far field | All | Far columns | Target |
|---|---|---|---|---|---|---|---|---|
| Open, render distance 32, full detail 32 | 0.05 s | all | 0.16 s | 6.03 s | none | **6.03 s** | 0 | 2 s near, 15 s all: met |
| Mine a lit log at 32 (acacia, open sky in front) | | **gone at 0.02 s** | | | | | | 1 s: met |
| Render distance 32 → 64 | 0.58 s | **5%** | at once | at once | 90.3 s | **90.3 s** | 1618 | 1 s, 15 s: missed |
| 64 → 16 | 0.00 s | all | at once | at once | none | 0.00 s | 0 | met |
| 16 → 64 | 0.61 s | **3%** | at once | **76.1 s** | 98.6 s | **98.6 s** | 1618 | missed |
| Full detail 32 → 16, at 64 | 0.62 s | (see below) | at once | at once | 160.4 s | **160.4 s** | 2707 | missed |
| Full detail 16 → 32 at 64, a lit log mined 1 s in | 0.67 s | 0% | at once | 76.4 s | 98.6 s | **98.6 s** | 1618 | missed |
| The log mined in that change | | **gone at 5.8 s** (6.8 s into the change) | | | | | | 1 s: missed |

What the table says:

- **A change does start within a second by the game's books, but not in the picture.** The first far column is built at 0.6 s every time. But the picture had made 3 to 5% of its change by 1 s, and still 70% to 50% by 12 to 20 s: the far field builds coarsest first and nearest first, about 0.95 s of worker time a column (**1618 columns in 1541.5 s of workers' time**), so the picture creeps for 90 s.
- **Near chunks wait behind the far field.** After 64 → 16 → 64, the near rings 17 to 32 took **76 s** to come back, against 6 s for all 33 rings when the world opened with no far field. That is the starvation of "Identified, 2026-11-09", now with a number.
- **A full detail change makes the picture worse first.** 32 → 16 at 64: 0.12% differs between before and the end, but at 0.5 s to 12 s 0.84% differs: terrain past 16 chunks (here the far hills on the horizon) goes at once, and the far field that should stand in for it takes 160 s (2707 columns) to bring it back. That is Ahmed's "so many rendering issues" after a full detail change. Not the darkening: this run has no climate and no save, and its last picture was not darker (`fd_16_last.png` against `rd_64_again_last.png`), so the darkening needs the menu's world to show.
- **A mined block is held up the same way: 0.02 s with nothing going on, 5.8 s while the far field rebuilds.** Ahmed's 11.png, a log that stayed for minutes, is the same cause at his scale: the climate system's tiles and render distance 64 on a 1920x1080 frame give the workers more to do than this run.
- **Opening at 32 with no far field meets the targets:** the ground out to 4 chunks at 0.16 s, everything at 6.0 s.

**Sound, with numbers.** A world opened through the real menus under `--headless` on Ahmed's seed at render distance 32, full detail 32, once with sound mixed into a file (`--sound-file`, 1.7 MB of it), once `--no-sound`, once `--no-sound` with `effect_weather: false`. `--headless` settles the whole world before the first picture, so this is the workers' speed with nothing else to do. All 33 rings drawn at **8.57 s with sound, 8.72 s without, 8.62 s without sound or weather**; the rings to 4 at 0.45, 0.44 and 0.40 s. **Sound and weather cost nothing measurable.** Sound can only be heard on a terminal on Ahmed's own display (`main_audio_start`), so a run on a private Xvfb never plays it; the file is the same mixer with another output.

**Which stage takes the time** (`--profile`, profile build, the windowed game on Xvfb :96 at 1280x720, Ahmed's seed, `--no-sound`, weather off): two runs through the real menus by `xdotool`, as `measure.sh` does, the climate and save included. Opening at 32: all 33 rings at **6.81 s** (6.83 s in the second run), the rings to 4 at 0.35 to 0.40 s. Then render distance 32 → 64 through Settings: the far field had 1139 columns built 66 s later and was not done.

| Scope, workers' time | Opening at 32, 31 s | Opening, then 32 → 64 and 66 s more |
|---|---|---|
| `column`: terrain for a near column | **18,962 ms** (3209 columns, 5.9 ms each, at most 22.5) | 18,914 ms, the same 3209 |
| Other jobs (far-field columns, section encoding, light, climate tiles; only `column` has its own scope) | 4,071 ms | **1,114,923 ms**, of which the far field's own count is 1,070,400 ms for 1139 columns: **0.94 s a column**, the longest job 2.3 s |
| `upload` on the gpu thread | 507 ms, at most 2.6 | |
| `lod` on the gpu thread | | 1,148 ms, at most 0.2 |
| Frames | p99 3.48 ms, worst 5.68; `shade` at most 4.5, `readback` at most 0.79 | p99 3.85, worst 9.37; `readback` at most 6.2 |

So: **opening is terrain**, 82% of the workers' time, and it meets the targets at 32. **A change to 64 is the far field**, 96% of the workers' time, at about 1 s a column, which is what stage 2 has to cut; light, encoding, upload and the card are each under 1%. Far-field jobs have no profiler scope of their own yet, so they show as `job`'s self time: worth adding with the fix. The traces are kept in the scratchpad (`14c/prof_open/trace.json`, `14c/prof_change/trace.json`), which a reboot clears.

**Why `shade` and `readback` spike.** Not settled. What was measured: in the long run above, at 640x360 with every worker busy for minutes, `shade` never passed 1.18 ms but `readback` reached **19.9 ms** (mean 0.155 ms) and the worst frame 21.2 ms. Readback is one `cudaMemcpy` of the picture to pinned memory on the frame's stream (`cuda/src/frame/`), timed on the card's clock, so a spike there is the copy waiting on the card, not on a thread. Uploads go on the same stream but before the frame, so they are not inside it. That leaves the card shared with other contexts (Ahmed's X desktop, which draws the window, on the same card) and the card's clocks: plan 004 phase 08 found it slowing to a third under steady load, and with a capped frame rate it idles and drops its clocks between frames. Ahmed's `shade` of 126 ms at 1080p is the same kind of spike at 9 times the pixels. Proving which needs `nvidia-smi` clocks beside the frame log on his desktop; left open.

## Targets

**Proposed, for Ahmed to change.** Times are from opening a world, or from pressing Done in Settings.

| What | Target |
|---|---|
| A render distance or full detail change starts showing | **Under 1 s**, as asked |
| The ground near you, out to 4 chunks | Under 2 s from opening a world |
| Everything out to render distance 32, full detail | Under 15 s |
| Frames while the world fills in | p99 under 25 ms, and no frame over 50 ms |

## Todo

**Stage 1: measure what Ahmed sees.** Nothing gets fixed before this.

- [x] Add a fill-in report: log when each ring of chunks around the player is fully drawn after a world opens or the distance changes. One line per ring, and a summary line, in the end-of-session report too
- [x] **Repeat the 64 against 32 runs above on one seed,** in real time on a private Xvfb, and mine a log as well as dirt. Record whether a mined block disappears, and how long it takes, at each distance
- [x] Add a scripted headless test that **changes render distance during a live game**, as Ahmed asked agents to do: 32 to 64 and back to 16, then full detail 32 to 16. Take screenshots every 100 ms for 3 s, read them, and record when each change first shows
- [x] Time a world opening to everything drawn at render distance 32, from a cold start, on today's `main`. Run it once with sound and once with `--no-sound`, to settle the sound question with numbers. Also once with plan 004's weather off, if a setting allows
- [x] Use the built-in profiler (`--profile`, profile preset) during opening and during a distance change. Record which stage takes the time: terrain, light, climate tiles, encoding, upload, far-field build or the card
- [ ] Record why `shade` and `readback` spike past 100 ms during play

**Stage 2: fix the starvation.** Each fix gets a before and after number in this file.

- [x] **Priorities in the job system:** a block edit's re-encode and light first, then the columns nearest the player, then climate tiles, then the far field. Add a priority to `job_submit` in `run/{src,include}/job/`, with workers always taking higher first
- [ ] **A cap on far-field workers,** so it can never hold more than a few at once, and never while near chunks wait. Drop queued far-field work that's gone stale when the player moves or the distance changes

  Agent 14a, `5542ec35`, measured live with `run_render_distance_live` (`MC_DISTANCE_LIVE_DEADLINES=0 MC_DISTANCE_LIVE_WAIT_S=300`, release, 16 workers, Ahmed's seed), against the baseline above:

  | Step | Baseline | Priorities and cap (`5542ec35`) |
  |---|---|---|
  | Open at 32, all rings | 6.03 s | 6.82 s (rings to 4 at 0.17 s) |
  | Log mined at 32, idle | 0.02 s | 0.02 s |
  | 32 → 64, all drawn | 90.3 s, 5% seen by 1 s | 94.3 s, 5% seen by 1 s |
  | 16 → 64, near rings 17 to 32 | **76.1 s** | **11.85 s** |
  | 16 → 64, all drawn | 98.6 s | 96.4 s, 3% seen by 1 s |
  | Full detail 32 → 16 at 64, all drawn | 160.4 s | 160.3 s |
  | Full detail 16 → 32 at 64, near rings | 76.4 s | 11.95 s |
  | **The log mined during that change** | **5.8 s** | **0.05 s**, 1.05 s into the change: **met** |

  The mined log meets its 1 s. The near rings come back 6 times sooner but not yet as soon as the free workers allow (about 7 s, from the 5.2 s rings 17 to 32 take when a world opens on all 16), so the cap stays unticked. The far field's own time, and so "seen by 1 s" and "all drawn", are unchanged: that is the next todo's, a cheaper far column.
- [x] **Make a far-field column cheap.** Profile one, then build it from the 2D height estimate (`terrain_height_estimate`, plan 004 phase 3) and the top block, not the full 3D terrain, if that's what it does. Target: under 50 ms of worker time a column. *Done another way, as the profile showed it was not the 3D terrain: about 0.4 s to 14 ms and 24 ms at voxels of 2 and 4, and 8 ms at voxels of 8, which no longer grow trees: see Issues found.*
- [x] **Never show nothing.** At 1 s, render distance 64 drew only sky. Near columns come first, and a missing one shows the far field, until it's built
- [ ] Repeat the stage 1 table with each fix, and show the 64 run matching the 32 run near the player

- [x] **Show the change as it comes, not all at once.** Near rings switch to the new setting first, and a far column drawn at the old setting may stay only until its own replacement is ready, not until all of them are
- [x] **The far field draws unsaved edits too,** by reading the live edits as well as the save, so a mined tree is gone everywhere at once. Or it never stands in for a near column that has edits
- [x] **Find and fix the darkening** after a full-detail change, with before and after pictures. *Found: it is the sun setting, not the change. Nothing to fix in code: see Issues found.*

**Also likely, to be confirmed or ruled out by the numbers:**

- [ ] Build the nearest chunks first, and keep a far one from holding up a near one
- [x] Let a distance change reuse what's already built, and only build the difference. *For the far field: each level that stays the same is kept (`render_replan_lod`). Full detail already kept its columns.*
- [ ] Size the per-frame upload budget so filling in neither crawls nor stutters
- [ ] If climate tiles hold up chunk generation (plan 004's own risk table), work them out ahead of the player, nearest first
- [ ] Each fix gets a before and after number in this file

**Stage 3: hold it.**

- [x] Make the live render-distance test a ctest that fails if a change takes over 1 s to start showing
- [x] Turn `measure.sh`'s steps into that test: new world, render distance 32 → 64, full detail 32 → 16, and mining a lit log, each with its own deadline: under 1 s to start showing, and the targets above to settle
- [x] Add the fill-in times to `bench_world`, and its history

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 900 ctest --preset release -R '^(run_render_distance_live|run_render_frame|run_settings|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 900 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: open a new world and time how long until it looks right. Then, mid-game, change render distance and full detail in Settings, and check it shows within a second and doesn't stutter.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `d8a6dba2` Add the fill-in report: when each ring of chunks is drawn after a world opens or the distance changes. (agent 14c)
- `eb650bd3` Add run_render_distance_live: render distance and full detail changed in a live game, and a lit log mined. (agent 14c)
- `d81c6ea0` Add the fill-in times to bench_world and its history, with the first run before the starvation fixes. (agent 14c)
- The commit that records stage 1's baseline here, with the live test's deadlines held only in an optimized build. (agent 14c)
- `103aea7e` Make far-field columns cheap and keep them across settings changes (plan 005 phase 14, far field). (agent 14b)
- `433d44f2` Show the horizon first after a render distance change, and stop the far field rebuilding after every autosave (plan 005 phase 14, far field). (agent 14b)
- `5542ec35` Give jobs priorities, cap the far field while near work runs, and drop far-field work queued before a move. (agent 14a)

## Issues found

**Overlaps phase 7 and phase 8.** Phase 7 is lag after a long time in another window, and phase 8 is how far you can see. This one is load and change speed. They share stage 1's measurements, so whichever starts first records them, and the others read them.

### The far field, 2026-11-10 (agent 14b)

**Why a far-field column cost about 1 s.** Measured with a new bench, `bench/lod/` (one column on one thread, Ahmed's seed, six places), not the in-game profiler: the bench isolates one column and needs no card. It is **not the 3D terrain**. A column at voxels of 16 or more, where nothing grows, cost 7.7 ms for all 1536 layers of 256 lines, as the density already skips its 3D noise outside twice the softness. At voxels of 2, 4 and 8, where what grows is sampled, it cost 375 to 420 ms (worst 500 ms): `terrain_feature_fill_box`, given a box one line wide, loaded and walked the chunk of the line and its 8 neighbours again for every one of the column's 256 lines, each walk running hydrology, surface, caves and flood over the chunk. In the game, with all 16 workers fighting over memory, that came to the 0.8 to 1.0 s a column the logs show.

**The fix: walk each chunk once a column.** `terrain_feature_fill_box_cached` in `run/src/terrain/feature/` does the same steps in the same order with each chunk taken from a cache of 48 the caller keeps, loaded and walked the first time it is asked for. The far field's job gives it one per column in its arena, through a new scratch argument on `render_lod_sample_t` (`RENDER_LOD_SAMPLE_SCRATCH_BYTES`). The bytes are the same, so the far field looks exactly as before at the levels that grow anything.

| Level | Before, mean ms | Before, worst | After, mean ms | After, worst | Chunks loaded a column |
|---|---|---|---|---|---|
| voxels of 2 | 378.7 | 439.7 | **13.8** | 16.1 | 16 |
| voxels of 4 | 384.5 | 462.3 | **24.0** | 31.2 | 36 |
| voxels of 8 | 374.5 | 428.9 | **58.6** with trees, **8.6** without (now) | 71.2, 9.2 | 100, 0 |
| voxels of 16, 32, 64 | 7.7 to 7.9 | 8.3 | 7.7 to 7.8 | 8.3 | none |

In the game, on the release build, Ahmed's settings (render distance 64, full detail 32): a session that went to full detail 16 ended with `far field: 3 levels, 1462 columns built in 50.7 s of workers' time`, **35 ms a column** against 0.8 to 1.0 s before (`main`: `410 columns built in 332.0 s`, `1032 columns built in 939.2 s`). So the far field now takes a few seconds of the workers where it took all of them for minutes. The session line counts only the far field open at the end: in the session that went 32, 16, then back to 32, the far field after the last change built `180 columns in 9.2 s` on this build, as both its levels were kept, against `791 columns in 794.2 s` on `main`, which built them all again.

**Voxels of 8 no longer grow trees.** With the cache that level still cost 59 ms (worst 71 ms): its 100 chunks are each loaded and walked once, at about 0.6 ms each, and that walk is the terrain's own. It is the level that reaches the horizon, and `run_render_distance_live` showed it had to be whole within a second of render distance 32 to 64 (below). So `RENDER_LOD_FEATURES_SHIFT_MAX` went from 3 to 2: that level costs 7.7 ms, and forests past about 900 blocks are drawn as their ground, a few pixels lower. Ahmed can put it back at the cost of a slower horizon. `render_feature_lod`'s golden pictures still pass as they were, within their 2 in 255: their far ground is past what trees at that level would change. None was made again.

**A profiler scope.** Each far-field column is now a `far_column` scope on its worker's lane, so `--profile` shows the far field's share of the workers.

**The 2D height estimate was tried and taken out.** Working out only the layers the estimate allows (the profile's height and softness at the four lattice columns round a line, a river's carve, the sea and the tallest tree) made no difference: 26.0 ms against 26.4 ms at voxels of 4, and about 1 ms slower where nothing grows, as the estimate's own columns cost what the skipped layers did. So a sample stays the whole height, exact by construction.

**Never show nothing.** At 1 s render distance 64 drew only sky. The far field asks for its coarsest level first, nearest column first, and that level stands round the player too; with columns of 1 s that level alone took 529 s of workers, so nothing showed. With columns of 8 ms it is whole in well under a second, coarse, and the finer level sharpens it. A first version asked for the nearest column in blocks across all levels; it drew the ground round a new world as fast, but after render distance 32 to 64 the new horizon came last, and the live test failed its 1 s deadline (7% of the change shown at 1 s), so the coarsest-first order came back once the coarsest level was cheap. A far column that full detail is already drawn over all of (by the near world's table, `render_lod_fine_set`) is now put off until every other has been asked for, and counts towards its level's shown square while covered, as a ray there meets full detail first. Live, on Ahmed's seed, render distance 64 and full detail 32, release build, a private Xvfb, 1280 by 720:

| A new world | Before (`main` at `7aea47e0`) | After |
|---|---|---|
| 0.5 s | sky only | **the ground round the player, textured** |
| 1 s | sky only | the same, complete |
| 2 s | untextured slabs: the far field standing in, as in Ahmed's 10.png | complete |
| 4 s | the slabs, partly textured | complete |

Part of that is the starvation going: near chunks no longer wait behind far columns. Job priorities (14a) still matter for the rest.

**Show the change as it comes.** A change of render distance or full detail closed the far field and opened a new one, so every far column went at once and came back only when the new levels were built. Now `game_view_change` calls `render_replan_lod` (`run/src/render/`): each level whose shift and radius stay the same is kept, with its blocks on the card, its table, and every column it has built (`world_buffers_adopt` in `cuda/src/world_buffers/`, `render_lod_level_adopt` in `run/src/render/lod/`), and only new levels are built, nearest first, the kept level showing through until they are. Full detail 32 to 16 at render distance 64 keeps both levels it had (voxels of 4 and 8) and adds one at voxels of 2; 16 to 32 keeps those two again. `render_feature_lod` holds it to that on the card: a dropped level leaves the other shown on the very next frame with nothing built and every voxel as it was, and back again the kept level is still whole while the new one fills. A change also waits for every far job on a worker, as it did before: that was up to 1 s, and is now under 0.1 s.

**`run_render_distance_live` passes, deadlines held** (release, after rebasing on 14c's test; it failed on `main` by design). On its world, render distance 32 to 64: the far field (1618 columns) done in **3.86 s** where 14c's baseline had it at about 160 s, and **67%** of the picture's change shown by 1 s (the deadline is half); back to 64 a second time, 4.75 s and 61%. Full detail 32 to 16 at 64: both far levels kept, the new one built, done in **2.22 s**; back to 32, **0 columns built**, as both levels were kept. Before this section's last two changes (the order and voxels of 8) it failed: 6.7 s to settle, and 7% and 4% by 1 s.

**Mined blocks in the far field.** The far field reads only what is captured for the save, and that was at the next autosave (30 s) or when a column left. With a far field open, `game_far_capture_tick` now captures what is dirty once a second of ticks, at the end of `game_sim_tick`; the autosave writes what is captured as before, and a column of the far field is cheap to make again. So a mined log is gone from the far field within about a second wherever the far field stands in for a near column. `run_save_state_game` holds it: a tower built and then taken away reaches the far field within 20 ticks each way, with no autosave. Ahmed's tree that stayed for minutes was most likely the near column's own re-encode waiting behind far-field work (job priorities, 14a), which the cheap columns also relieve. Not measured live: the script's mining aims at the ground ahead, not a lit log, so 14c's live test should aim at one.

**The darkening is the sun setting, not the full-detail change.** A day is `GAME_DAY_SECONDS` = 1200 s, and a new world starts at `GAME_DAY_START` = 0.35 of a day, so the sun sets about 7 minutes in. Ahmed's measured run changed full detail 375 s in, and its pictures darkened at 420 to 500 s. A control run on the same seed and build with **no settings change at all** and the day cycle on: the mean brightness of the picture was 42.9 at 60 s, 54.6 at 360 s, then **41.2 at 420 s, 28.8 at 480 s, 9.5 at 540 s**, the same curve as Ahmed's 53, 42, 30. With the day cycle off (`effect_day_cycle: false`) and full detail changed 32 to 16 and back, the brightness of the picture is 38.88 before, 38.88 after 16 and 38.88 after 32, before and after this phase alike. Stored light is not touched by a full-detail change: the map keeps its columns (`world_map_set_view_radius` only changes what is drawn), and the far field's light is its fixed open sky. So there is nothing to fix in code. If the night coming so early reads as a defect, that is a question for Ahmed: a day of 20 minutes starting at 08:24.

**The far field after every autosave (phase 7's lag).** At render distance 64 a draw stalled 360 to 680 ms every 30 s, as each autosave told the far field of every column it captured, and when that list overflowed, of all of them (`render_lod_all_changed`). Now a capture tells the far field of a column only if one of its sections' blocks differ from the last capture of that section: `save_world_far_t.seen` holds a hash of each section's states as last captured, 65536 slots, and a capture that matches is counted in `unchanged` and not told (fluid levels moving and blocks put back as they were tell nothing either). The ring of columns went from 4096 to 16384, and when it overflows the game marks only the far columns over the loaded map (`render_lod_area_changed`), as nothing else is ever captured. `run_save_state_game` holds it: an autosave with nothing edited builds no far column, and a block taken away and put back before a capture builds none. Phase 7's headless rerun of the stall is for after this lands on `main`, as its steps are not there yet: not measured here.

**How it was measured.** `/tmp/claude-1000/-home-ahmed-Projects-Games-MineCraft-C/52c310b0-7c8d-4c06-a48e-20883c52be21/scratchpad/14b/live.sh` drives the real game on Xvfb :95 with xdotool: a new world on Ahmed's seed, full detail changed through the Settings screen, which applies a change with the screen still up but does not draw the world behind it, so times are from Resume. Pictures in `before_full/`, `after_full/` and `before_dusk/` beside it, in `/tmp`, so a reboot clears them. The looking direction faces a hillside, so the far field shows little in the settled pictures: the far-field claims above rest on the ctests and the session log lines, not on these pictures.

**Disk.** `/home` was full while this was built (22 MiB free), from many worktrees' builds. The release test binaries of this worktree were deleted to finish, and the release build here was of `minecraft_c`, `bench_lod` and `bench_world` only.

**Ahmed's rendering issues aren't listed yet.** He said he'll look into them later. When he does, each gets its own note, and this phase's measurements should help.

**`run_render_distance_live` fails on today's code, on purpose (agent 14c, 2026-11-10).** It is registered with no label and no `WILL_FAIL`, so it fails the suite until stage 2 lands. On the baseline above it misses 8 deadlines: rd_64, rd_64_again and fd_32_busy each by "seen by 1 s" (3 to 5% and 0%, against half) and by "all" (90 to 99 s against 15 s); fd_16 by "all" (160 s); the log mined while busy (5.8 s against 1 s). Its first-change check passes today (0.6 s): the game's books say a far column came, but the picture barely moved, so the picture check is the one that holds Ahmed's "under 1 second". `MC_DISTANCE_LIVE_DEADLINES=0` makes it report only; `MC_DISTANCE_LIVE_WAIT_S=300` lets each step finish. With the default 20 s wait it takes about 2 minutes on today's code, and about 30 s once the fill meets its targets.

**How the test differs from the plan's wording.** The plan asked for a scripted headless test with screenshots every 100 ms. `--headless` cannot show this: it settles the whole world before every picture (`run/src/game/settle/`), so a change always shows complete in the next frame and a mined block always goes at once. So the test runs the game's real threads against the clock in-process, as `run_render_frame`'s real-threads test does, with no display, and reads the picture main is handed every frame: no Xvfb needed. Settings changes go through `game_reconfigure`, which is what the Settings screen's Done calls; the log is mined through `world_edit_block_set`, the call the hands make, with the threads stopped for that moment. Pictures are kept at 0, 0.5, 1, 2, 3, 5, 8, 12, 16, 20 s and every 10 s after, not every 100 ms; the log's square is compared every frame.

**What the test does not have that Ahmed's game does:** no menu, so no climate tiles and no save. So the far field there draws no saved edits, and the darkening after a full detail change (which may need either) did not show. The darkening is still open for stage 2.

**The 15 s settle deadline for render distance 64 is a proposal.** The targets table gives 15 s for "everything out to render distance 32"; the test holds every change to it, as the stage 3 todo says "the targets above to settle". At about 1 s of worker time per far column and 1618 columns, 15 s needs the far column under about 0.15 s of worker time with 16 workers: stage 2's "under 50 ms" would meet it. Ahmed may want a separate, longer number for 64 and over.

**The fill-in report** is `run/src/game/fill/`. A ring is drawn when all its columns are in the card's table and the upload queue is between views, which is when all their sections have gone; the table is looked at no more than every 10 ms while a fill goes on. It lives in the game's World arena beside the column jobs, as `game_t` has no room. `run/src/game/index.c` went from 1500 to 1513 lines for it: past the standard's "split at 1500" (the hard limit is 2000), which whoever next touches that file should split.

**`bench_world` and the fill-in times.** Five new columns at the end of `history.tsv`'s first line: `fill_first_s fill_near_s fill_rings_s fill_far_s fill_all_s`, of the opening, from `game_open`. A settled row settles on one thread, so these are the workers' speed with nothing else in the way, after the eye has moved to the row's place. The first run (`d81c6ea0`) is in `bench/world/README.md`: at render distance 32 the near rings take 7.4 to 11.0 s; at 128 and 512 they take 96 to 108 s, waiting for the far field.

**Stage 1's "Repeat the 64 against 32 runs on a private Xvfb, and mine a log as well as dirt"** was done in the test, not on Xvfb, and with a log only: the dirt of the first run was mined, and is what left the hole too dark to see. Both distances on one seed: at 32 the log went in 0.02 s; at 64, during a change, in 5.8 s.

**`shade` and `readback` spikes: left unticked.** The measurements and what they rule out are in the baseline section; which of the card's clocks or the desktop sharing it causes Ahmed's 126 ms needs his desktop.

**The live test's deadlines hold only in an optimized build by default.** In the debug build (`-O0`) the opening alone took over 20 s and every change's first far column over 2 s, so there it reports and does not fail; `MC_DISTANCE_LIVE_DEADLINES=1` holds it anyway. The phase's Verification runs it on the release preset, where it fails today as above.

**`/home` filled up during this work (2026-11-10, about 18:30)**, from every agent's build trees together (2 to 3.4 GiB each). A `clang-format -i` truncated a source file then; it was put back from the commit. Agent 14c removed its own `build/debug` and `build/profile` to make room.

**Priorities and the far-field cap, live (agent 14a, 2026-11-10).** On `5542ec35` a log mined during the busiest change goes in 0.05 s, against 5.8 s, which meets its target. After 16 → 64 the near rings 17 to 32 come back in 11.85 s, against 76.1 s, but about 5 s slower than 12 free workers should manage. Likely causes, not yet measured: far-field jobs already running cannot be stopped and take up to 2.3 s each; spawned jobs and stealing take no account of priority. The far field still takes 94 to 160 s to finish, so the live test still misses rd_64, rd_64_again and fd_16, and fd_32_busy's "all drawn".
