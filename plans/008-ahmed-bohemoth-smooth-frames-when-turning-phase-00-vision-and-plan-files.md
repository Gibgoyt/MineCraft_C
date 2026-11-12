# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 0: The vision, and the plan files

**Status:** done   (updated 2026-11-12)

**Read this file before any other file of plan 008,** then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`. Every phase file assumes you know what is here.

- Plan 004's vision file (`plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md`) describes the whole game.
- `cuda/src/frame/README.md`, `cuda/src/shade/README.md` ("AO") and `cuda/src/post/README.md` ("The temporal pass") describe the renderer this plan changes.
- This file only says where plan 008 sits, what it found, and the rules every phase follows.

## Goal

Two things are true when this phase is done:

1. **Every agent working on plan 008 can say in a few sentences:**
   - what is wrong: when Ahmed shakes his head, new pictures fall from 60 to 40 or 45 a second (`~/Pictures/4.png`: FPS 45.1, card 14.01 ms at render distance 64);
   - why (master section 1):
     - his laptop holds the card at 50 W and about 600 MHz, a third of its clock;
     - the gpu thread starts a picture only after main takes the last, so a picture of just over 16.7 ms waits until 25;
     - turning adds four AO rays for every pixel it brings into view, and 16-texel TAA reads;
     - some directions cost more than the slot;
     - main can block on the X server;
     - the readback and the gpu thread's own work are serial with the card;
   - what plan 008 changes: measure first, then pacing, overlap, fresh work, adaptive resolution, and a far-field turn cache if it pays;
   - what it must not change: a still camera's picture, byte for byte.
2. **The plan files exist:** the master, and phase files 01 to 07, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: Where plan 008 sits

### In the renderer

```
main (display loop, 120 Hz by the clock in a window)
 ├─ takes the newest picture from the exchange (3 slots, newest wins)
 ├─ HUD over it, copy into the display buffer
 └─ MIT-SHM put, then wait for the X server        ← phase 02: two segments, never block
gpu thread
 ├─ waits for main to take the last picture       ← phase 02: start just in time, at once when late
 ├─ upload, far-field planning, fill               ← phase 03: overlapped with the card
 └─ one stream: coarse, primary, entities, far field, shade (water, surfaces, sun, AO, fresh AO), post + TAA, outline, readback
                                                   ← phase 01: shade's kernels on their own clock
                                                   ← phase 03: readback on a second stream, one frame in flight
                                                   ← phase 04: fresh AO from a corner guess and a world-space cache; cheaper TAA read when turning
                                                   ← phase 05: a smaller picture while turning or late, upscaled on the card
                                                   ← phase 06: far-field answers cached by direction (if phase 01 says so)
sim thread                                         (unchanged)
```

### Beside the other plans

- **Plan 005 phase 8 and 14** built the far field, its "only where shade will look" rule, and its cheap columns. This plan does not change how the far field is built or what it draws. Phase 06 only caches its answers by direction.
- **Plans 006 and 007** change biomes and terrain. They touch nothing in the renderer, so they are independent of this plan, with one exception below.
- **Plan 006 phase 01 is being built now,** in another worktree. It edits `run/src/game/index.c` (the command box, `game_columns_request`) and the F3 lines (`run/src/game/corner/`). Plan 008 phase 01 also edits `run/src/game/index.c` (stamps in `game_gpu_thread` and `game_loop_frame`) and adds F3 lines. **So plan 008's first code commit waits until plan 006 phase 01 is on `main`, or rebases onto it.** Plan 006's and 007's files are not edited by this plan.

### What was measured for this plan, and what was not

At 14:41 on 2026-11-12, Ahmed's game was running (`pgrep -af 'build/release/bin/minecraft_c'` printed pid 463671). So no GPU measurement was run.

Only read-only queries were made: `nvidia-smi -q -d PERFORMANCE,TEMPERATURE,POWER,CLOCK`, `nvidia-smi --query-gpu=...` and `sensors`. They found:

- the card at 570 to 675 MHz of 2100;
- an enforced power limit of 50 W against 200 W;
- SW power cap and SW thermal slowdown active for 449,126 s;
- 81 °C;
- a laptop (Alienware) with fans at maximum and the CPU at 91 °C;
- another agent's CUDA tests on the card at the same moment.

The master's section 0 has the table. Every other number in the master is from the code, with arithmetic, or marked "suspected" with the phase 01 experiment that settles it.

---

## Part 2: Rules every phase follows

1. **Still pictures do not change.** `run_render_frame`'s golden pictures and every settled still row's hash in `bench_world` hold in every phase. A moving row's new hash is recorded only after looking at its shots.
2. **Measured before and after.** Phase 01's shake rows and frame log run before any fix, and after each phase, with the `nvidia-smi` sidecar beside them. Numbers are compared only like with like: same row, card state within 10 % (phase 07, "Like with like").
3. **Never run anything while Ahmed's game is open.** Check `pgrep -af 'build/release/bin/minecraft_c'` before every bench or GPU test; if it prints anything, ask him to close it and wait. Never touch his process. Never use `DISPLAY=:0`: X tests run under their own `Xvfb`, as `tests/run/display/x11/` does.
4. **Ahmed's own measurements are his to make.** The agent asks him to run his game with `--frame-log`, and reads the file afterwards.
5. **Every adaptive measure acts only while turning or late,** and returns to full detail within 250 ms of stopping.
6. **Every heavy command goes through `tools/test-or-build.sh`,** one command per call, with `timeout` inside the wrapper (`.claude/rules/test-or-build-mutex.md`). A sidecar runs inside the same wrapper call as its bench, is killed at the end, and is checked gone. After tests, `pgrep -af "$PWD/build"` prints nothing. Never start a server or the game and leave it running.
7. **Never push.**
8. **Commit as Ahmed,** with `git commit -F -`, one-sentence messages, and no AI trailers (no `Co-Authored-By`, no Anthropic or Claude lines). After each commit, `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing.
9. **The repo's own rules:**
   - `plans/README.md`: todos ticked as work lands, issues recorded when they arise, the master kept verbatim once approved;
   - `plans/CODING_STANDARD.md`: split at 1500 lines, hard limit 2000. `run/src/game/index.c` is at 1638, so new code goes in `run/src/game/pace/` and `run/src/game/watch/`;
   - `plans/REPO_STRUCTURE.md`;
   - main's 16384-byte frame limit: new state goes in arenas, not in `game_t` or `app_t`.

---

## Part 3: The phases, and what each leaves behind

| Phase | File | What's true when it's done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the master and phase files 01 to 07 exist |
| 01 | `...-phase-01-measure-the-head-shake-cost.md` | A head-shake bench; shade's kernels timed apart; slot accounting; the card's real clock; F3's new lines; `--frame-log`; the cost rose; each cause C1 to C9 measured; Ahmed's own shake read |
| 02 | `...-phase-02-pacing-and-presenting-without-waiting.md` | Pictures start just in time, or at once when late; two SHM segments; intervals follow the card's time, not 8.33 ms steps |
| 03 | `...-phase-03-overlapping-the-cards-work.md` | Readback on a second stream; one frame in flight; async copies; the same pictures |
| 04 | `...-phase-04-turning-without-fresh-work.md` | Fresh AO from a corner guess and a world-space cache; TAA's cheaper read when turning fast |
| 05 | `...-phase-05-adaptive-resolution-while-turning-or-late.md` | A smaller picture while turning or late, upscaled on the card; two settings; still pictures exact |
| 06 | `...-phase-06-the-far-fields-turn-cache.md` | Built only if phase 01 shows it pays: far-field answers cached by direction for turning in place |
| 07 | `...-phase-07-the-final-check.md` | Every target before and after; Ahmed's own shake; full suite; his verdict |

**Open questions** (master section 11) each have a default, and each is repeated in the phase it affects:

| Question | Phases |
|---|---|
| Q1 dynamic resolution on, floor | 05, 07 |
| Q2 fresh AO: corner and cache, one ray, or four | 04 |
| Q3 pacing: just in time or at the take | 02, 07 |
| Q4 agents' card work while he plays | 01 |
| Q5 the `performance` platform profile once | 01, 07 |
| Q6 the panel's real refresh rate | 01, 02 |
| Q7 phase 06 conditional | 06 |
| Q8 targets | 01, 07 |
| Q9 temporal upsampling | 05 |

---

## Ahmed's words, 2026-11-12

- "To add to ./plans/008* please look at ~/Pictures/4.png. when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Todo

- [x] Read `plans/README.md`, plan 004's master and a rich phase file, plan 007's master and phase files 00 and 01 for structure
- [x] Looked at `~/Pictures/4.png`: FPS 45.1 at 1920×1080, card 14.01 ms (coarse 0.81, primary 3.65, shade 6.90, post 1.27, readback 1.38), sim tick 1.01, HUD 0.87, present 0.34, at x -80 y 134 z 418, uploads 0, far jobs 0
- [x] Read the frame's path:
  - `run/src/render/frame/index.c`, `cuda/src/frame/index.cu`;
  - the gpu and sim threads and `game_loop_frame` in `run/src/game/index.c`;
  - the window loop in `run/src/display/index.c`, and `run/src/display/x11/index.c`;
  - `cuda/src/traverse/` (coarse and primary), `cuda/src/shade/` (AO history and fresh list), `cuda/src/post/` (TAA), `cuda/src/lod/` README, `render_upload_frame`, `render_lod_frame`;
  - the watch, the profiler README, `bench/world/` (paths, live rows), `run/src/game/view/` (render scale);
  - plan 005 phases 7, 8 and 14 and plan 001 stage 17.3 on frame cost
- [x] Checked that turning causes no uploads or far-field work (position-only residency, no frustum anywhere)
- [x] Checked whether a measurement was allowed: Ahmed's game was running, so none was run; only read-only `nvidia-smi` and `sensors` queries
- [x] Check `008` is free: `ls plans/ | grep '^008'` printed nothing before this plan
- [x] The master written, status "draft for approval (2026-11-12)", committed alone
- [x] Phase files 01 to 07 written with plan 004's richer parts, every box unticked, status `not started`, each with a "Read first" line linking back to this file and the master, one commit each
- [x] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found: `grep -L "## Issues found" plans/008-*phase-*.md` prints nothing
- [x] Plans 006 and 007's files left untouched

## Verification

No build, test or code changes in this phase.

    ls plans/008-*
    grep -L "## Issues found" plans/008-*phase-*.md    # prints nothing
    git status --short                                 # nothing uncommitted

## Commits

Appended as they land:

- `bc2777ff` Add the plan 008 master for smooth frames while turning: what the card and the slot rule do when Ahmed shakes his head, the causes ranked with evidence, and pacing, overlap, fresh-work, resolution and far-field fixes to measure first and then build.
- `25a15f1d` Add the plan 008 phase 01 file for a head-shake bench, shade's kernels on their own clock, slot accounting, the card's real clock, a per-picture frame log and one experiment for each suspected cause.
- `b2084bb3` Add the plan 008 phase 02 file for starting each picture when it is needed so a late one never waits a whole slot, and for two shared-memory buffers so main never blocks on the X server.
- `65235344` Add the plan 008 phase 03 file for taking the picture home on a second stream and keeping one frame in flight with asynchronous copies, so the card never waits on the readback or the gpu thread's own work.
- `920d919b` Add the plan 008 phase 04 file for a ray-free first guess and a world-space cache for the AO of pixels a turn brings into view, and a cheaper TAA read while turning fast, with still pictures unchanged.
- `1c5fdd02` Add the plan 008 phase 05 file for drawing fewer pixels only while turning or late, with a tested controller, history across size changes and an upscale on the card, so a still picture stays exactly as it is.
- `5c69b147` Add the plan 008 phase 06 file for a direction-keyed cache of far-field answers that lets turning in place trace each far direction once, built only if the phase 01 measurements show it pays.
- `3e8d408b` Add the plan 008 phase 07 file for the final check: every target measured before and after like with like on Ahmed's card, his own head-shake frame logs, the full suite and his verdict.

## Issues found

- **2026-11-12, plan setup, reading the code to write the files.** Places where a first reading and the code differed, settled before the files were committed:
  - **The display rate is 120, not 60.** `config/machine.yaml:21` sets `refresh_hz: 120`, and main keeps to it by the clock in a window (`run/src/display/index.c:421-427`). So the slot is 8.33 ms, and the steps are 120, 60, 40 and 30 pictures a second, not 60 and 30. FPS 45.1 is one picture in three at 16.67 ms and two in three at 25. The master uses 120 throughout; whether Ahmed's panel really runs at 120 is Q6.
  - **The card is not "an RTX 2080 Super" at desktop power.** It is the laptop part, held at an enforced 50 W. This was found by `nvidia-smi` and `sensors` while writing the plan, and it became master cause C1 and section 2.2.
  - **The AO cache needs a per-edit key.** The first draft of master 3.4 said `run/src/render/encode/` already bumps a per-section revision. It does not: the column table's `generation` (`run/include/gpu_bridge/world_buffers/index.h:100`) is per load. Corrected in the master before its commit. Phase 04 step 1 decides between the section's slab offset and a new counter.
  - **TAA's history and AO's history are indexed by this frame's width** (`cuda/src/post/index.cu:96,113`; `cuda/src/shade/index.cu:232-240`). So dynamic resolution needs them indexed by the previous camera's. Phase 05 step 1.
  - **Every host-to-card copy is a synchronous `cudaMemcpy` on the legacy stream.** So removing `cudaDeviceSynchronize` alone would not overlap anything. Phase 03 moves the per-frame copies to `cudaMemcpyAsync` from pinned staging.
  - **Another agent's CUDA tests were on the card while Ahmed played.** The build mutex does not cover the game. That is master C8 and Q4.
