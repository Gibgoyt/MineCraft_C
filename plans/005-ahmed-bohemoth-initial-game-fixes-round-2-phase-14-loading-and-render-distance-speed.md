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
- [ ] Add a scripted headless test that **changes render distance during a live game**, as Ahmed asked agents to do: 32 to 64 and back to 16, then full detail 32 to 16. Take screenshots every 100 ms for 3 s, read them, and record when each change first shows
- [ ] Time a world opening to everything drawn at render distance 32, from a cold start, on today's `main`. Run it once with sound and once with `--no-sound`, to settle the sound question with numbers. Also once with plan 004's weather off, if a setting allows
- [ ] Use the built-in profiler (`--profile`, profile preset) during opening and during a distance change. Record which stage takes the time: terrain, light, climate tiles, encoding, upload, far-field build or the card
- [ ] Record why `shade` and `readback` spike past 100 ms during play

**Stage 2: fix what stage 1 finds.** The likely candidates, to be confirmed or ruled out by the numbers:

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
