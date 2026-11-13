# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 7: The final check

**Status:** measured and checked, waiting for Ahmed: his two frame logs and his verdict (the gate list below). Every number the bench can give is in. At the same 177 to 181 MHz, plan 008 takes the 720°/s shake at his spot from 16 to 79 new pictures a second at render distance 64 (84 at 32), and the interval p99 from 67 to 25 ms. The 16.7 ms p99 target needs about 600 MHz, and the card spent the whole run at its thermal floor   (updated 2026-11-13)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: sections 4, 5 and 11. Then every earlier phase file's results and Issues found.

## Goal

Plan 008 is shown to do what Ahmed asked, on his card as it is, with nothing else made worse. When this phase is done:

- **Every target in master section 4 has its "after" number** beside phase 01's "before", on the same rows, on the card in a state the sidecar records. Each is met, or the miss is said plainly with why.
- **Ahmed has shaken his head in his own world** at his spot, at render distance 32 and 64, with F3 up and `--frame-log` on. The log is read here, and his verdict is recorded.
- **The full debug suite and the release build pass.** The full `bench_world` run shows no existing row slower by more than 3 %. Still pictures are byte for byte as before the plan.
- **Every open question (master section 11) has Ahmed's answer, or its default, recorded.**
- **The READMEs that changed say what is now true:** `cuda/src/frame/`, `cuda/src/shade/`, `cuda/src/post/`, `cuda/src/lod/` if phase 06 built, `run/src/game/pace/`, `run/src/game/watch/`, `run/src/display/x11/`, `run/src/render/frame/`, `bench/world/`.

## Where this fits

**The vision.** Master section 1 found the causes from the code; phase 01 measured them. Phases 02 to 06 each took one away. This phase measures the whole together, because the causes interact: pacing changes how far each picture turns, which changes fresh AO, which changes the rung.

**What this phase must NOT build:** no new feature. A fix found necessary here goes back to its phase's file as an Issue found, is built there, and this phase runs again.

## Ahmed's words

- "To add to ./plans/008* please look at ~/Pictures/4.png. when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### Like with like

The card's clock moves with heat and the cap: plans 004 and 005 saw it fall to a third within a minute. So a "before" and an "after" are compared only:

- on the same row;
- in the same process order;
- with the sidecar's MHz and enforced power limit within 10 % of each other.

Otherwise the row is run again, after the card has cooled for 5 minutes (`nvidia-smi` temperature under 60 °C). The "before" numbers come from phase 01's recorded runs, or from checking out the commit before phase 02 into a scratch worktree and running the same rows there. That is a separate build directory, through the wrapper, and is removed afterwards.

### What Ahmed runs

His usual command, with `--frame-log ~/shake_after.csv` added:

1. Stand still at `/tp -80 134 418` for 5 s.
2. Shake his head hard, left and right, for 10 s.
3. Stand still for 5 s.
4. Quit.

Then the same at render distance 32, from the Settings screen, into a second file. The agent reads the files, never his running game.

---

## Steps

1. **Check Ahmed's game is closed** before any bench run (`pgrep`). *Why: never touch his game; never share the card with it unless that is the experiment.*
2. **The full debug suite, the release build, standards.** *Why: nothing broken.*
3. **The shake rows, before and after, like with like.** *Why: master section 4's targets.*
4. **The full `bench_world` run** into `history.tsv`. *Why: no existing row worse by more than 3 %, and still rows' hashes unchanged.*
5. **Ahmed's own frame logs.** *Why: the real game, with the X server, on his desktop.*
6. **The targets table below, every row filled.** *Why: the plan's promise.*
7. **Open questions:** each answer or default recorded here and in the master's phase-00 Issues found. *Why: nothing left implied.*
8. **READMEs checked against the code;** the master's status updated in phase 00's Issues found (the master itself stays verbatim, `plans/README.md` rule 1). *Why: the record.*

## Todo

- [x] Ahmed's game confirmed closed before every bench run
- [x] Full debug suite passes (`ctest --preset debug`), standards and format pass, release builds (release builds after one fix to a test's frame size, below)
- [x] Shake rows before and after, like with like, at his spot and `jagged_peaks`, render distance 32 and 64; the sidecar's state beside each
- [ ] Full `bench_world` run recorded in `history.tsv`; no existing row slower by more than 3 %; still rows' hashes unchanged: **left to plan 009 phase 08**, which runs the full `bench_world` at render distance 32 and 64 in parallel; the orchestrator asked this phase not to run it twice. Still pictures are checked here by the goldens and the spot's settled hash (below)
- [ ] Ahmed's two frame logs (render distance 64 and 32) read and summarised below: **his run, still to come** (gate list, below)
- [x] The targets table below filled in, every row met or its miss explained
- [x] Every open question's answer or default recorded
- [x] READMEs match the code
- [ ] Ahmed's verdict recorded: **his, still to come**

## Targets, before and after (filled in by this phase)

**Like with like.** "Before" in the second column is phase 01's recorded row (`bench/world/shake/history.tsv`, "008 phase 01: before", 2026-11-12, at 148 to 215 MHz). The "After" column is plan 008 as the game ships it (`--dynamic-floor 50`, everything else on by default). The third column is **plan 008 turned off in this same build**, in the same run, alternated with "After" (on, off, on, off): `--pace-off --serial-frames --features-off 180000 --dynamic-floor 0` (phase 02's rule off, phase 03's overlap off, phase 04's first guess and phase 06's turn cache off, dynamic resolution off). What stays on is phase 04's bilinear TAA read past 2 pixels, which is compiled in, and phase 02's two SHM segments, which a bench has no use for. So "off" has plan 009's renderer and plan 007's land, as "after" does, and the two differ by plan 008 alone. **The card was at its thermal floor in every live row of the final run: 177 to 181 MHz mean by the clock64 kernel (readings 145 to 475), 40 to 41 W of a 50 W limit, 86 °C, reasons `0x24` (SW power cap and SW thermal slowdown)**. So on and off are within 2 % of each other in MHz, inside the 10 % rule. Rows at his spot (`bench_world_shake --at -80 134 418 --seed 14148492376765118422`), 1920x1080, every effect of play, 10 s each, on main at `5cba451b` (plan 007 phase 04's land). Recorded in `history.tsv` as "008 phase 07: plan 008 on (defaults), pass 1/2", "... off (...), pass 1/2", "... jagged peaks", "... still tail 2 s".

| Measure (master section 4) | Before (phase 01, its MHz) | After: plan 008 on (pass 1 / pass 2, at 177 to 181 MHz) | Plan 008 off, same build and run | Target | Met? |
|---|---|---|---|---|---|
| Pictures a second shaking at 720°/s, his spot, render distance 64, capped; interval p99 | 45.1 a second on his screen (`~/Pictures/4.png`, about 600 MHz); bench 16.5 a second, p99 83.3 ms (182 MHz) | **79.4 / 79.1 a second, p99 25.00 / 25.00 ms**, p50 8.36 / 8.33 | 16.3 / 16.3 a second, p99 67.6 / 66.7 ms | ≥ 59 / ≤ 16.7 ms | **Pictures a second: met (79). p99: missed at this clock**, 25.0 ms: one picture in a hundred waits three slots. At 148 to 181 MHz even 50 % is near two slots of card (13.8 ms card mean at 50 %, 2.5 Mcycles: 4.1 ms at 600 MHz). Phase 05's replay at 600 MHz gives p99 16.0 |
| The same at render distance 32 | p99 83.3 ms, 23.1 a second (215 MHz) | **p99 25.00 / 25.03 ms, 83.6 / 83.9 a second** | p99 66.7 / 66.7 ms, 16.4 / 16.4 a second | p99 ≤ 16.7 ms | **Missed at this clock**, as above |
| Worst interval in a 10 s shake at 720°/s | 83.4 ms (r32), 83.3 (r64) | r32 33.3 / 33.4; **r64 25.04 / 75.0** (one picture: 2 of 3 rows' worst is the first after the world opens, or a picture in flight when the rung stepped) | 75.0 / 66.9 (r32), 75.0 / 66.7 (r64) | ≤ 25 ms | **Missed at this clock:** 25.04 in pass 1 (by 0.04 ms), 75.0 in pass 2 (one picture) |
| At 1440°/s, render distance 64, p99 | no such row in phase 01 (added in phase 05; its off row: 50.0 ms at 175 MHz) | **25.03 / 25.00 ms**, 79.4 / 79.4 a second, worst 58.4 / 25.0 | 91.7 / 91.7 ms, 12.2 / 12.2 a second | ≤ 25 ms | **Met** (25.0 is a picture every three slots; the p99 sits on it) |
| Still camera's picture | `BA28CEC20AD658FA` (phase 01 to 05; plan 009 changed the picture since) | settled `spot/shake-0` **`277603BACCC7A973`** on, in both passes | `277603BACCC7A973` off, in both passes | Byte for byte | **Met**: on and off draw the same still picture; every golden in the debug suite passes |
| Still camera's card time | 5.62 Mcycles (459 MHz, before plan 009) | 3.86 Mcycles, 25.37 ms at 155 MHz (pass 2; pass 1 ran at 667 MHz: 7.99 ms, 5.19 Mcycles, not like for like) | 3.85 Mcycles, 25.93 ms at 148 MHz (pass 2) | Within 2 % | **Met:** +0.3 % in cycles at 155 against 148 MHz (-2.2 % in ms) |
| Card idle while late, waiting for main | 4.1 to 6.7 ms a picture (wait for take, live rows) | **0.02 to 0.07 ms** wait for take on every live row; the card's idle before a picture started at once after a late one: 0.00 ms on every row | 4.1 to 5.7 ms | < 0.5 ms a picture | **Met** |
| Main blocked on the X server, p99 | not measured (needs his window; phase 01's C6 open) | **not measured: needs Ahmed's own `--frame-log`** (`x_wait_ms`, `x_put_ms`) | | < 1 ms | **Open**: his run (gate list) |
| Stopping a turn to full resolution | no adaptive resolution | **151 / 154 ms** (r32 / r64, `--still-tail 2`, the first picture back at 100 % after the camera stops) | | ≤ 250 ms | **Met** |
| Camera-to-screen latency p50 | not measured in phase 01 (phase 03 added it: 35 to 58 ms serial at 150 to 190 MHz) | shaking **18.7 to 20.4 ms**; still r32 39.2 / 40.1, still r64 42.5 / 41.9 | shaking 61.3 to 83.3 ms; still r32 41.5 / 41.5, still r64 41.5 / 41.6 | No worse | **Met shaking (a third of off); still: r32 1 to 2 ms better, r64 0.4 to 1.0 ms worse** (the overlapped copy home waits for the next frame's queue, which the far field's work at the spot makes 5 to 6 ms: see Issues found) |
| Card power while still | 41 to 42 W (phase 01's sidecar, a run of every row) | **43.2 / 43.2 W** (30 s still, live-capped r32, each with its own sidecar; 34.3 / 34.2 a second, at 158 / 159 MHz) | 41.2 / 41.4 W (26.7 / 26.7 a second, at 159 / 158 MHz) | Within 5 % | **Met, just: +4.6 %.** The card is busy 86 % of the time against 71 %: the start rule takes away the wait for main, so the same card at the same clock draws 28 % more pictures for 4.6 % more power. No extra pictures beyond what main can show |
| Existing `bench_world` rows | `bench/world/history.tsv` | left to plan 009 phase 08's full run | | None slower by more than 3 % | **Not run here** (orchestrator: no duplicate of plan 009 phase 08's benches) |
| Stretch: render distance 32 at 720°/s if uncapped, p99 | p99 75.0 ms, 16.8 a second (158 MHz) | p99 25.00 / 25.03 ms, 84.5 / 84.2 a second (180 MHz) | p99 66.7 / 66.7 ms, 18.2 / 18.2 a second | ≤ 8.33 ms | (recorded only; the card is not uncapped) |

Card state for each column: on and off alternated within one lock call, each pass of 7 rows about 4 minutes; `gpu_watch.sh` beside the whole run. Live rows: clock64 177 to 181 MHz mean in every row of both (readings 145 to 475; the first sixth of each row ~300 MHz, the rest 147 to 149); `nvidia-smi` 300 MHz, 40 to 41 W of 50 W, 86 °C, reasons `0x24`. The settled still rows' clock moved more (155 to 667 MHz), which is why their card time is compared in cycles and only within 10 % of MHz.

**Where the far field is seen** (`jagged_peaks`, the bench's own place on plan 007 phase 04's land, 720°/s capped, at 162 to 180 MHz), on / off: r32 **65.1 / 23.2 a second, p99 24.85 / 58.4 ms**; r64 **60.1 / 19.1 a second, p99 25.03 / 66.7 ms**, the far field's rays 1.14 / 9.43 ms; the turn cache read 93.2 % (r32) and 88.0 % (r64) of far rays; camera to screen p50 20.8 / 41.6 and 21.4 / 50.0 ms.

**Where each picture's card time went, at his spot, r64, 720°/s** (pass 2), off then on: shade 37.9 → 4.4 ms (fresh AO 24.2 → 0.32: phase 04's first guess; the rest from 99 % of pictures at 50 %, phase 05), post 3.97 → 3.78, primary 4.29 → 1.22, fresh pixels 829 k → 86 k a picture.

## Tests

No new tests. Every test the earlier phases added runs here in the full suite:

- `run_game_pace`;
- `run_display_x11`;
- `run_render_frame`, with its golden pictures;
- `run_render_upload`;
- `render_feature_post`, `render_feature_lod`, `render_feature_bridge`;
- `run_settings`.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset tsan
    tools/test-or-build.sh timeout 1800 ctest --preset tsan -R '^(run_render_frame|run_game_pace|run_game)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_final.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 07: after"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_final_r64.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --render-distance 64; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 1200 build/profile/bin/bench_world --record bench/world/history.tsv --label "008 phase 07: full run"
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

## Gate

Ahmed plays his own world at render distance 64, as he did for `~/Pictures/4.png`, and shakes his head as hard as he did then. He reads F3 and says:

1. Is the drop gone? Does it feel smooth?
2. Does anything look worse: the edges while turning, softness, far hills?
3. Anything he wants changed: dynamic resolution's floor (Q1), the pacing margin (Q3)?

His words are recorded below, and plan 008 is done when he says so.

### Ahmed's gate list: what to play and what to check (2026-11-13)

Build once, after this branch is on `main`, and close every agent's GPU work first (the card is shared):

    tools/test-or-build.sh cmake --build --preset release

**Run 1, render distance 64** (Settings: render distance 64, and leave "Dyn 50%" as it is; that is dynamic resolution, on by default):

    ./build/release/bin/minecraft_c --frame-log ~/shake_after_r64.csv

1. Open your world and `/tp -80 134 418`. Press F3.
2. Stand still for 5 s. Read F3's `shown` line (the time between new pictures on the screen: last, p99, max) and the `turn ... MHz` line (the card's real clock).
3. Shake your head as hard as you did for `~/Pictures/4.png`, left and right, for 10 s. Watch `shown` and FPS. While you shake, the `turn` line says `res 50% dyn`.
4. Stand still for 5 s. The `res` field goes away within about a quarter of a second, and the picture is sharp again.
5. Quit to the menu, so the frame log is written.

**Run 2, render distance 32:** the same, with `--frame-log ~/shake_after_r32.csv` and render distance 32 in Settings.

**What to check, and tell the agent:**

1. **Is the drop gone? Does it feel smooth?** At the same clock the bench gives 79 to 84 new pictures a second while shaking, against 16 with plan 008 turned off. F3's FPS should now *rise* when you shake, not fall, because the picture is drawn at half size while you turn.
2. **Does anything look worse while turning?** Check the edges of blocks and trees (phase 04's first-guess AO on newly seen faces). Check softness while turning (phase 05's 50 % size, brought up by plan 009 phase 07's temporal upscale). Check the far hills when you turn in place (phase 06's turn cache): they should look the same as when you stand still.
3. **Does a still picture look exactly as before?** It should, to the bit.
4. **Anything you want changed:** the dynamic resolution floor (Q1: the "Dyn" button steps 50, 70, 85, 100, off), and the pacing (Q3: just in time, 1 ms margin).
5. **Your laptop's state:** what F3's MHz says while you play (the bench saw 148 to 181 MHz). If you ever try the `performance` platform profile (Q5), a second run under it shows how much is heat.
6. **Your panel's refresh rate** (Q6), if you know it: the game assumes 120.

The agent then reads the two CSVs (`interval_ms`, `late`, `rung`, `x_wait_ms`, `x_put_ms`, `card_mhz`) and fills in the rows still open: main blocked on the X server, and your own pictures a second and p99, still and shaking.

### Ahmed's verdict

Still to come.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The card's state differs between before and after runs | Like with like (above); rerun after cooling; the sidecar's numbers beside every row |
| A target is missed on the capped card | Said plainly with the numbers and which cause is left; Ahmed decides whether to accept, or to open a follow-up phase |
| The full suite takes longer than its timeout | Split by `-R` into groups, each under its own wrapper call, as earlier plans did; recorded |
| Ahmed's laptop state changed since phase 01 (profile, cap) | Recorded; if he moved to `performance` (Q5), both states are measured if he is willing |

## Commits

Appended as they land:

- `d0eb3162` Keep run_terrain_feature_own's tops test within the release build's 16 KiB frame limit, found by plan 008 phase 07's release build: its chunk read and column walk (23 KiB together) come from the test's heap in place of the stack.
- `98a7dc09` `run_ui_hud_corner`'s `place` set before `rig_find_hurricane_season`, so that the whole tsan build links again (`-Werror=maybe-uninitialized`; phase 03 found the whole build stopping there).
- this commit: the final measurements in `bench/world/shake/history.tsv`, the targets table, the open questions, Ahmed's gate list and the verification.

**Verification, as run (2026-11-13, on `5cba451b` with this branch's commits):**

- `pgrep` for his game printed nothing before every GPU run.
- `tools/test-or-build.sh cmake --build --preset debug`: clean.
- `tools/test-or-build.sh timeout 3600 ctest --preset debug -j 3 --output-on-failure`: 175 of 177 passed, `standards_lint` and `clang_format` among them, every golden picture included. `run_climate_warning` (`it_takes_under_20_ms_on_any_preset`: `slowest < 20000000`) and `run_save_world` (`killed_at_any_moment_it_loses_at_most_the_save_in_progress`: `confirmed > SW_KILL_ROUNDS`) failed under the parallel load, and both passed run alone. Before the rebase, on `905dcf7f`, the same suite gave 175 of 177, with `run_climate_warning` and `run_ui_atlas` (`the_sim_never_waits_long_on_the_lock`) failing and passing alone.
- `cmake --build --preset tsan`: the whole build is clean after the corner fix. `ctest --preset tsan -R '^(run_render_frame|run_game_pace|run_game|run_ui_hud_corner)$'`: `run_game_pace`, `run_game` and `run_ui_hud_corner` passed. `run_render_frame` passed all 31 of its cases (907 783 checks), but ctest marks it failed for one ThreadSanitizer report. That report is the race phase 03 recorded: `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap`'s atomic read of `game->stream_quiet` (`tests/run/render/frame/index.c:1360`) against the sim's plain write in `game_columns_request` (`run/src/game/index.c:1150`). It is not plan 008's code, and the fix (an atomic flag in `game_t`, read by `game/settle` too) is not a final check's to make.
- `cmake --build --preset release`: fails on `905dcf7f` at `run_terrain_feature_own`'s frame size; clean with `d0eb3162`, release's 16 KiB frame limit held.
- `cmake --build --preset profile`: clean.
- The bench lines ran as `bench_world_shake`, with `gpu_watch.sh` in the same lock call (above), recorded in `bench/world/shake/history.tsv`. The full `bench_world` line was not run: plan 009 phase 08 runs it.
- Afterwards `pgrep -af "$PWD/build"`, `pgrep -af gpu_watch` and `pgrep -a Xvfb` printed nothing. The release, profile and tsan builds were deleted after use, for the disk (13 GiB free).

## Issues found

### Open questions (master section 11): answers or defaults, as plan 008 ends

Ahmed has answered none of them yet, so each default stands, and each is in the code as described:

| # | Question | As it stands |
|---|---|---|
| Q1 | Dynamic resolution while turning or late: on by default? floor? | **Default: on, floor 50 % per axis**, back to 100 % within 15 still pictures (151 to 154 ms measured here). Settings `dynamic_resolution` and `dynamic_resolution_floor_percent`; the "Dyn 50%" button on the render scale's row. Since plan 009 phase 07, with TAA on, a picture drawn smaller is brought up by the temporal upscale; phase 05's bilinear `post_upscale` is used only with TAA off |
| Q2 | Fresh AO: A corner guess and cache, B one ray, C four rays | **Default A** (phase 04); B is `--fresh-ao-rays 1`, for comparison |
| Q3 | Pacing when on time: A just in time, B at the take | **Default A, margin 1 ms** (`GAME_PACE_MARGIN_NS`); B exists as `game_pace_start(..., false)`, but nothing sets it |
| Q4 | Agents' card work while he plays | **Default A, warn**: before any GPU test or bench, every agent checks `pgrep` for his game and waits. Nothing in the code |
| Q5 | The `performance` platform profile once | **Default: measured as it is only.** Not tried. Every run of this plan saw the card at its thermal floor (148 MHz by clock64, reasons `0x24`: SW power cap and SW thermal slowdown; the CPU at 94 to 100 °C in phase 01) |
| Q6 | The panel's real refresh rate | **Default 120** (`config/machine.yaml`), not confirmed |
| Q7 | Phase 06 conditional | **Built** (phase 06): when phase 01 measured it, the far field cost ≥ 1.5 ms at r64. On by default; `turn_cache_side: 0` in `compile/config/render/lod/index.yaml` takes it and its 96 MiB out |
| Q8 | Targets | **As written**: the master's section 4 targets assume the capped card at about 600 MHz. This card ran at 148 to 181 MHz throughout plan 008, so the p99 ≤ 16.7 ms targets are missed here, and met only in phase 05's replay at 600 MHz (p99 16.0). Ahmed decides whether to accept |
| Q9 | Temporal upsampling | **Done by plan 009 phase 07**, not here: plan 008 phase 05's ladder now feeds it |

The master's status lines are left as they are. This phase file says the master stays verbatim (`plans/README.md` rule 1), and the orchestrator asked this phase to edit only its own file. The orchestrator can copy this file's Status into the master's phase 00 Issues found.

### Findings for Ahmed (too big to fix in a final check)

- **The bench's "spot" is no longer his view.** `bench_world_shake --at -80 134 418 --seed 14148492376765118422` now draws a dark grass clearing with a block face filling three quarters of the picture, not the snowy slope and valley of `~/Pictures/4.png`. This holds since plan 009 phase 04 at the latest; the shots of `shake-0` and `shake-720` were looked at. The settled still hash was `277603BACCC7A973` both before and after plan 007 phase 04's land landed, so the land there did not change with it. Plan 008's spot rows since phase 05 measured that scene. His own frame log at his own place is the real test (gate list). Whoever next uses the bench at his spot should check its shot first, and check the seed (`saves/test_7/world.meta`) and the coordinates against F3 in his world.
- **At his spot the far field keeps the gpu thread busy for 2 to 13 ms a picture.** The gpu thread's own part (start to all queued) was 0.03 to 0.16 ms in phases 01 to 03. In this run at the spot it is **4.9 to 6.3 ms a picture still, 2.1 to 2.9 ms shaking, 6.7 to 9.6 ms with plan 008 off, and 11.8 to 13.2 ms in the settled rows**. At `jagged_peaks` it is 0.05 to 0.19 ms. The bench's own report says why. In a 10 s row the octant store made 4 141 to 4 663 level-1 columns from octants (7.3 to 8.3 s of the row) and sent 1.1 to 1.6 GiB, with "a far field level's pool is full" and 609 to 653 far columns "with no room". Over a 90 s still row on the final land, P fell only from 4.9 to 1.7 ms while 11 564 level-1 columns (3.7 GiB) were sent, 479 with no room in their pool. The pyramid keeps remaking columns at the spot long after it has filled. This is also **why the far field's revision moves every frame at the spot** (phase 06's open note): while this goes on, the columns sent and the levels' squares shown change every frame.
  - It is plan 009's pyramid (`run/src/render/lod/`, `render_lod_octants_request`, the size of the level-1 pool at render distance 32 and 64), not plan 008's code.
  - Plan 008's overlap (phase 03) hides most of it from the card, because the next picture is queued while the card works. That is why "on" gets 79 pictures a second while "off" pays the cost serially.
  - What it still costs: the overlapped copy home waits behind the next frame's queue (readback 3.4 to 7.5 ms a picture at the spot, against 1.4 at the jagged peaks); still camera-to-screen at r64 is 0.4 to 1 ms over serial; and it uses CPU while he plays.
  - **For plan 009 phase 08 or a follow-up:** why the level-1 pool is full at the spot, and why the octant store keeps remaking columns.
- **The p99 ≤ 16.7 ms targets are out of reach on this card at 148 to 181 MHz.** Every live row at the spot ran at the 50 % floor 99 % of the time, at 13.2 to 13.8 ms of card per picture (2.4 to 2.8 Mcycles; 4 to 4.6 ms at 600 MHz). Main shows a picture only on its 8.33 ms tick, so 13.8 ms pictures come every second slot, and now and then every third. With two frames in flight overlapping, that gives p50 8.36 ms and p99 25.0 ms. The causes left at this clock are the card's speed itself (C1) and, at the spot, the far field's gpu-thread work above. A floor of 35 or 25 % is not in the ladder; Ahmed decides (Q1, Q8).

### Small things fixed here

- **The release build did not link** `run_terrain_feature_own` (plan 007's test, on `main`): `a_chunks_tops_read_from_its_box_are_what_the_walk_down_finds` had a 23 168-byte frame, past release's `-Wframe-larger-than=16384`. Its `terrain_feature_chunk_t` and its column array now come from `test_alloc`. `d0eb3162`.

### What changed from the plan, and why

- **"Before" is two things:** phase 01's recorded rows (the table's second column), and plan 008 turned off in the final build by its own switches (third column), alternated with "on" in one run. The plan offered phase 01's rows, or a scratch worktree at the commit before phase 02. A scratch build there would also have left out plan 009 and plan 007, and phase 01's spot is not today's scene (above). The switches cover phases 02, 03, 04 (first guess), 05 and 06. Phase 04's bilinear TAA read past 2 pixels and phase 02's two SHM segments stay on in "off".
- **The bench lines ran as `bench_world_shake`** (the shake rows have been their own program since phase 01), with `gpu_watch.sh` in the same lock call. They ran from scripts, so that on and off alternate within one lock: two passes of 7 rows at the spot each way, the jagged peaks each way, a still-tail run for the way back to full size, and still-camera power runs of 30 s each way, each with its own sidecar.
- **The first full measurement ran before plan 007 phase 04 landed** (main at `905dcf7f`). The orchestrator asked for a rerun on the new land, so every number above is from the second run, on `5cba451b`. The first run's numbers agreed (r64 720°/s on: 78.7 a second, p99 25.03 ms at 182 MHz; off: 19.4 a second, p99 66.7 ms at 179 MHz). They are not recorded in `history.tsv`.
