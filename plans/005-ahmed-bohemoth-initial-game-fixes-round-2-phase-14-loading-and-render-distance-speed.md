# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 14: Loading and render distance speed

**Status:** not started   (updated 2026-11-09)

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

- [ ] Add a fill-in report: log when each ring of chunks around the player is fully drawn after a world opens or the distance changes. One line per ring, and a summary line, in the end-of-session report too
- [ ] **Repeat the 64 against 32 runs above on one seed,** in real time on a private Xvfb, and mine a log as well as dirt. Record whether a mined block disappears, and how long it takes, at each distance
- [ ] Add a scripted headless test that **changes render distance during a live game**, as Ahmed asked agents to do: 32 to 64 and back to 16, then full detail 32 to 16. Take screenshots every 100 ms for 3 s, read them, and record when each change first shows
- [ ] Time a world opening to everything drawn at render distance 32, from a cold start, on today's `main`. Run it once with sound and once with `--no-sound`, to settle the sound question with numbers. Also once with plan 004's weather off, if a setting allows
- [ ] Use the built-in profiler (`--profile`, profile preset) during opening and during a distance change. Record which stage takes the time: terrain, light, climate tiles, encoding, upload, far-field build or the card
- [ ] Record why `shade` and `readback` spike past 100 ms during play

**Stage 2: fix the starvation.** Each fix gets a before and after number in this file.

- [ ] **Priorities in the job system:** a block edit's re-encode and light first, then the columns nearest the player, then climate tiles, then the far field. Add a priority to `job_submit` in `run/{src,include}/job/`, with workers always taking higher first
- [ ] **A cap on far-field workers,** so it can never hold more than a few at once, and never while near chunks wait. Drop queued far-field work that's gone stale when the player moves or the distance changes
- [ ] **Make a far-field column cheap.** Profile one, then build it from the 2D height estimate (`terrain_height_estimate`, plan 004 phase 3) and the top block, not the full 3D terrain, if that's what it does. Target: under 50 ms of worker time a column
- [ ] **Never show nothing.** At 1 s, render distance 64 drew only sky. Near columns come first, and a missing one shows the far field, until it's built
- [ ] Repeat the stage 1 table with each fix, and show the 64 run matching the 32 run near the player

**Also likely, to be confirmed or ruled out by the numbers:**

- [ ] Build the nearest chunks first, and keep a far one from holding up a near one
- [ ] Let a distance change reuse what's already built, and only build the difference
- [ ] Size the per-frame upload budget so filling in neither crawls nor stutters
- [ ] If climate tiles hold up chunk generation (plan 004's own risk table), work them out ahead of the player, nearest first
- [ ] Each fix gets a before and after number in this file

**Stage 3: hold it.**

- [ ] Make the live render-distance test a ctest that fails if a change takes over 1 s to start showing
- [ ] Add the fill-in times to `bench_world`, and its history

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 900 ctest --preset release -R '^(run_render_distance_live|run_render_frame|run_settings|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 900 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: open a new world and time how long until it looks right. Then, mid-game, change render distance and full detail in Settings, and check it shows within a second and doesn't stutter.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**Overlaps phase 7 and phase 8.** Phase 7 is lag after a long time in another window, and phase 8 is how far you can see. This one is load and change speed. They share stage 1's measurements, so whichever starts first records them, and the others read them.

**Ahmed's rendering issues aren't listed yet.** He said he'll look into them later. When he does, each gets its own note, and this phase's measurements should help.
