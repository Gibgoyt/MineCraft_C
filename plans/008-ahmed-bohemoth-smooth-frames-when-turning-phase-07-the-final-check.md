# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 7: The final check

**Status:** not started   (updated 2026-11-12)

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

- [ ] Ahmed's game confirmed closed before every bench run
- [ ] Full debug suite passes (`ctest --preset debug`), standards and format pass, release builds
- [ ] Shake rows before and after, like with like, at his spot and `jagged_peaks`, render distance 32 and 64; the sidecar's state beside each
- [ ] Full `bench_world` run recorded in `history.tsv`; no existing row slower by more than 3 %; still rows' hashes unchanged
- [ ] Ahmed's two frame logs (render distance 64 and 32) read and summarised below
- [ ] The targets table below filled in, every row met or its miss explained
- [ ] Every open question's answer or default recorded
- [ ] READMEs match the code
- [ ] Ahmed's verdict recorded

## Targets, before and after (filled in by this phase)

| Measure (master section 4) | Before (phase 01) | After | Target | Met? |
|---|---|---|---|---|
| Pictures a second shaking at 720°/s, his spot, render distance 64, capped; interval p99 | 45.1 / | | ≥ 59 / ≤ 16.7 ms | |
| The same at render distance 32 | | | p99 ≤ 16.7 ms | |
| Worst interval in a 10 s shake at 720°/s | | | ≤ 25 ms | |
| At 1440°/s, render distance 64, p99 | | | ≤ 25 ms | |
| Still camera's picture | | | Byte for byte | |
| Still camera's card time | | | Within 2 % | |
| Card idle while late, waiting for main | | | < 0.5 ms a picture | |
| Main blocked on the X server, p99 | | | < 1 ms | |
| Stopping a turn to full resolution | | | ≤ 250 ms | |
| Camera-to-screen latency p50 | | | No worse | |
| Card power while still | | | Within 5 % | |
| Existing `bench_world` rows | | | None slower by more than 3 % | |
| Stretch: render distance 32 at 720°/s if uncapped, p99 | | | ≤ 8.33 ms | (recorded only) |

Card state for each column: MHz, enforced power limit, temperature.

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

## Risks and open questions

| Risk or question | Default |
|---|---|
| The card's state differs between before and after runs | Like with like (above); rerun after cooling; the sidecar's numbers beside every row |
| A target is missed on the capped card | Said plainly with the numbers and which cause is left; Ahmed decides whether to accept, or to open a follow-up phase |
| The full suite takes longer than its timeout | Split by `-R` into groups, each under its own wrapper call, as earlier plans did; recorded |
| Ahmed's laptop state changed since phase 01 (profile, cap) | Recorded; if he moved to `performance` (Q5), both states are measured if he is willing |

## Commits

Appended as they land:

## Issues found

None.
