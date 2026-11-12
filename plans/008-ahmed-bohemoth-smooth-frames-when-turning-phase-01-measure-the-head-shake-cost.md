# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 1: Measure the head-shake cost

**Status:** not started   (updated 2026-11-12)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: sections 0, 1 (the causes C1 to C9), 3.1 and 4. Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

Every cause in master section 1 has a number, measured on Ahmed's card as it is. When this phase is done:

- **`bench_world` has a `shake` path.** Yaw sweeps back and forth at fixed speeds, at a chosen place and seed, settled, live-capped and live-uncapped, at render distance 32 and 64. Each row prints:
  - picture intervals: p50, p99, worst, and a histogram in 8.33 ms bins;
  - late pictures;
  - card time per kernel, shade's own kernels included;
  - the fresh AO count;
  - the gpu thread's loop split;
  - the card's MHz.
- **The card's time is split finer.** Shade's water, surfaces, sun, AO and fresh AO each have their own clock events on the card lane, and the fresh list's length comes home with every picture.
- **The gpu thread says where its time goes, picture by picture:**
  - waiting for main to take;
  - upload;
  - far-field planning;
  - fill;
  - draw's CPU part;
  - waiting for the card.

  Each picture also has its slot accounting: when it was ready, when it was shown, ticks waited, late or not.
- **The card's clock is known without NVML,** from a one-warp `clock64` kernel timed by events. An `nvidia-smi` sidecar script logs clocks, power limit, temperatures and throttle reasons beside a run.
- **F3 shows three more lines,** and `--frame-log <path>` writes a CSV line per picture, in release builds too. The watch's minute line gains the interval p99 and the late count.
- **The cost rose is measured:** card ms by yaw (72 directions) and pitch (3), settled, at Ahmed's spot, at render distance 32 and 64.
- **Each cause C1 to C9, and the "ruled out" line, has its experiment run** and its answer written in this file's results table.
- **Ahmed has recorded one real shake with `--frame-log`** in his own game, and it is read here.

**No picture changes.** Every golden picture and settled hash holds.

## Where this fits

**The vision.** Master section 1 argues the causes from the code, and one measurement (`nvidia-smi` at 14:41). Only C2 and C7 are proven by the code alone. The sizes of C3 to C6 and C9 are guesses until measured. Every later phase is judged with this phase's tools, before and after.

**What reads it:**

| Reader | What |
|---|---|
| Phase 02 | Live-capped against live-uncapped intervals; the card's idle time waiting for main; X put latency; the frame logs that tune $\hat{T}$ and the margin |
| Phase 03 | Readback ms; $P$ per picture by part |
| Phase 04 | Fresh AO count and its card ms by turn speed; TAA's cost by speed |
| Phase 05 | Frame logs as the controller's test input; card ms by direction |
| Phase 06 | The cost rose: whether the far field decides the worst directions |
| Phase 07 | The "before" column of every table |
| Ahmed | F3's new lines, his own frame log, and the table of causes |

**What this phase must NOT build:**

- No fix. No change to pacing, presenting, streams, AO, TAA, resolution or the far field. Phases 02 to 06 do those.
- No change to any picture. Golden pictures and settled hashes hold.
- No NVML or other new library. The clock comes from `clock64`; `nvidia-smi` is only called by a script beside the bench.
- **Never run anything while Ahmed's game is open.** Check with `pgrep -af 'build/release/bin/minecraft_c'`, and if it prints anything, ask him to close it and wait.

## Ahmed's words

- "To add to ./plans/008* please look at ~/Pictures/4.png. when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### A picture's slot

Main asks for a picture every 8.33 ms (120 Hz, `config/machine.yaml:21`, paced by the clock in `run/src/display/index.c:421-427`). A **slot** is one of those ticks. A picture is **shown** at the first tick after it is ready. It is **late** if it was ready after the tick it was aimed at, which is the first tick at least 8.33 ms after its start. Its **ticks waited** are how many ticks passed from its start to being shown.

Today a picture starts only when main has taken the last (`run/src/game/index.c:1464-1468`). So:

- ticks waited is $\lceil (P + G) / 8.33 \rceil$;
- the card's idle time is from ready to shown.

Master C2 gives the arithmetic that turns $G = 14.01$ into 45.1 a second.

### Turn speed

Turn speed is the angle between the unjittered forward vectors of two pictures, over the time between their camera reads, in degrees a second. Yaw and pitch both count. F3 shows the mean over the last 0.25 s.

### The strip and fresh AO

Master C3 gives the strip's share at each speed. This phase checks the formula against the fresh count. At a settled 720°/s with pictures every 16.7 ms (12° a picture), the count should be about 17 % of the pixels, 357 k. That is the formula's own prediction, within the 3×3 neighbour fallback's effect, which only lowers it.

### The card's clock without NVML

A kernel of one warp reads `clock64()` (the SM's cycle counter) at its start and end and spins for about 20 µs. Events time it on the card's clock, in ms. Then MHz = cycles / (ms × 1000).

Run once a second, it costs about 20 µs. It needs nothing beyond the CUDA runtime the game already links, unlike NVML (`libnvidia-ml`), which the game does not link. The `nvidia-smi` sidecar is the check on it: the two should agree within 5 %.

### The cost rose

A **cost rose** is a chart of what a still picture costs in each direction. At one place, settled, the camera looks in 72 yaws (every 5°) at 3 pitches (-20°, 0°, +15°). Each direction is drawn after 8 warm frames at that direction, so TAA and AO history are full and turning adds nothing.

Card ms per kernel is recorded, and the rose is drawn as a PNG: a polar plot, one ring per pitch, with the slot lines at 8.33 and 16.67 ms. If its worst direction is over the slot, turning will find it whatever turning itself costs (master C5).

---

## Design

### The `shake` path (`bench/world/index.c`)

A new path beside `orbit`, `flight` and `walk-mobs`:

- **The motion:** yaw is a triangle wave about the place's heading, ±60°, at a constant speed $\omega$ of 0, 90, 360, 720 or 1440°/s. Pitch is fixed at -10°. The `-pitch` variant sweeps pitch ±20° at the same speed with yaw fixed.
- **Settled rows:** frame $k$ is drawn at time $k / 60$ s, so every speed passes over the same directions at the same picture rate. 30 warm frames, then 240 measured.
- **Live rows:** the game's own threads for 10 s, main asking at 120 Hz, as the live rows do now.
  - **Live-capped:** `uncapped = 0`, exactly as a player with vsync on. This is new: today's live rows are all uncapped.
  - **Live-uncapped:** as today.
- **Places:**
  - `--at X Y Z` and `--seed S` make a row at any place; heading from `--yaw`.
  - Ahmed's spot is `--at -80 134 418` with his world's seed, read from his save's state file.
  - The standard worlds' places work too: `jagged_peaks` is the mountain one.
- **Rows** (the defaults; `--list` prints them):
  - `jagged_peaks/shake-<ω>` for $\omega$ in 0, 90, 360, 720, 1440, settled;
  - `jagged_peaks/shake-720-live-capped` and `-live`;
  - `jagged_peaks/shake-720-pitch`;
  - the same at render distance 64 with `-r64`.

  With `--at`, the same set is named `spot/...`.
- **Self-check:** at $\omega = 0$ the settled picture hash equals a still row's at the same place and heading. A row whose last picture has fewer than 8 colours is unsound, as today.

New columns go at the end of `history.tsv`'s first line:

- `interval_p50_ms`, `interval_p99_ms`, `interval_worst_ms`, `late_share`;
- `fresh_mean`, `fresh_max`;
- `ao_ms`, `ao_fresh_ms`, `sun_ms`, `water_ms`, `surface_ms`;
- `gpu_wait_take_ms`, `p_ms`;
- `card_mhz`.

### Shade's kernels on their own clock (`cuda/src/shade/`, `cuda/src/frame/`)

`frame_gpu_t` gets events between shade's kernels:

1. water;
2. surfaces;
3. sun;
4. the AO history pass;
5. fresh AO.

They are spans on the profiler's card lane, named as in `cuda/src/shade/README.md`'s list. `frame_timings_t` gains `shade_ms[5]` beside `kernel_ms`. The pass totals do not change, so F3's `shade` line still adds up as before.

**The fresh list's length.** `shade_ao_fresh_kernel` already reads a count the card keeps (`fresh_count`). After the frame, that count is copied home in the same stream, 4 bytes into pinned memory, read after the existing wait.

### The gpu thread's loop split and slot accounting (`run/src/game/watch/`)

`game_gpu_thread` stamps `time_now_ns()` at each of its stages:

- the end of the wait for main;
- after upload;
- after far-field planning;
- after fill;
- after the draw's CPU part (before `frame_gpu_run` waits);
- after the wait.

Main stamps when it takes a picture, and which tick that was. A picture's record is these stamps plus its card timings, the fresh count, its camera's turn speed and the card MHz. It goes into a ring of 4096 records in the World arena beside `game_watch_t`, not in `game_t`, which is at its size.

**The X put's latency.** `display_x11_t` stamps when each `ShmPutImage` is sent and when its `ShmCompletion` arrives (`put_sent_ns` exists at `run/src/display/x11/index.c:477`; the completion is taken at `:92-93`). Main's record for the frame holds both stamps, and how long `display_x11_wait_ready` blocked.

### F3's new lines (`run/src/ui/hud/`, `run/src/game/report/` or wherever `game_hud_debug_read` lives then)

Under `present`:

    card 14.0 ms  p99 17.8  max 21.2  (last 1 s)
    pictures 45/s  late 22  idle 4.1 ms
    turn 640 deg/s  card 615 MHz

All three are read from the ring by `game_hud_debug_read`. The F3 block grows by 3 lines. Plan 006 phase 01 also adds F3 lines (phase 00), so this phase builds on whatever is on `main` when it starts.

### `--frame-log <path>` (`run/src/main/`, `run/src/game/watch/`)

When given, the watch appends one CSV line per picture main takes, from the ring, written at the end of each minute of play and at exit (so play never waits on a write). The columns are the record above, one per field, with a header line. It is allowed in release builds: it costs a null check a picture when off.

The minute line in the log gains `pictures a second, interval p99 X ms, late N`.

### The `clock64` kernel (`cuda/src/frame/`)

`frame_gpu_clock_mhz` is a new call. Once a second the gpu thread queues the one-warp kernel at the start of a frame, between two events, and reads the result after the frame's wait. It goes in the record and F3.

### The `nvidia-smi` sidecar (`bench/world/gpu_watch.sh`)

A shell script that runs `nvidia-smi --query-gpu=timestamp,clocks.gr,clocks.mem,power.draw,enforced.power.limit,temperature.gpu,utilization.gpu,clocks_throttle_reasons.active --format=csv -l 1` and `sensors` once a second into a file, until killed. It is run inside the same wrapper call as the bench:

    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake ...; s=$?; kill $w; wait $w; exit $s'

The script exits when killed, and the verification checks nothing is left.

### The cost rose (`bench/world/`)

`bench_world --rose` (with `--at`, `--seed`, `--render-distance`):

- 72 yaws × 3 pitches, settled, 8 warm frames each;
- per-kernel ms to a CSV;
- a polar PNG through `display_png_encode`, as the bench already writes shots;
- a shot of the worst direction.

---

## The experiments, one per cause

Each is run on the card as it is, with the sidecar on. Each result goes in the table under "Results". "Expected" is the master's prediction.

| Cause | Experiment | Expected | What settles it |
|---|---|---|---|
| C1 card cap | Every row: `card_mhz` from `clock64` and the sidecar. If Ahmed agrees (Q5), the `spot/shake-720` rows again after `powerprofilesctl set performance` (his command, not ours) | About 600 MHz, 50 W enforced; card ms scales with clock | Card ms × MHz roughly constant between the two runs |
| C2 slot rule | `spot/shake-720-live-capped` against `spot/shake-720-live`; slot accounting | Capped intervals in 8.33 ms steps, idle waiting for main up to 8.33 ms; uncapped intervals ≈ $P + G$ | Histogram shape, and `gpu_wait_take_ms` |
| C3 fresh AO | Settled shake at 0, 90, 360, 720, 1440°/s, AO on; `fresh_mean`, `ao_fresh_ms` | Fresh ≈ the strip formula; `ao_fresh_ms` grows with $\omega$ | Fresh count within 15 % of the formula; ms by speed |
| C4 TAA | The same speeds with `--features-off` TAA, against on | Post grows by 0.2 to 0.5 ms from 0 to any speed | Post ms by speed, on against off |
| C5 direction | The cost rose at the spot, render distance 32 and 64 | Worst direction over 16.7 ms at 64 on the capped card | The rose's maximum, and which kernel decides it |
| C6 X server | Ahmed's own frame log in a window (below): put latency and main's block on `wait_ready` | Latency rises with card utilisation; blocks of 8 to 25 ms | Correlation of latency with card busy share |
| C7 serial work | Profile build: `upload`, `lod_frame`, `frame_draw` lanes; readback on the card lane | $P$ 0.5 to 2 ms; readback 1.38 ms | Means and p99 per picture |
| C8 other CUDA work | `spot/shake-720-live-capped` with `run_render_frame` started on the card at 5 s, in the same wrapper call | Intervals jump while it runs | Interval p99 with and without |
| C9 heat | A 60 s live-capped shake; sidecar temperatures and power limit over time; `p_ms` over time | Card MHz falls, $P$ rises as the CPU heats | Trend over the 60 s |
| Ruled out: coherence and L2 | Settled shake at 0 and 1440°/s with AO and TAA off (`--features-off`) | Same card ms within 3 % | Per-kernel ms equal within run noise |

**Ahmed's own frame log.** Once the build has `--frame-log`, Ahmed runs his game as he always does, adding `--frame-log ~/shake.csv`. He stands still for 5 s at his spot, shakes his head hard for 10 s, stands still 5 s, then quits. The agent reads the CSV, never his running process, and fills in the table. This is the only measurement made on his desktop with the X server, which is what C6 needs.

---

## Owned paths

This phase may edit these, plus this file:

- `bench/world/` (the `shake` path, `--at`, `--seed`, `--yaw`, `--rose`, live-capped, new columns, `gpu_watch.sh`, README)
- `cuda/src/shade/`, `cuda/src/frame/`, `run/include/gpu_bridge/frame/`, `run/include/gpu_bridge/shade/` (events inside shade, the fresh count home, the `clock64` kernel)
- `run/src/game/watch/`, `run/include/game/watch/` (the ring, the frame log, the minute line)
- `run/src/game/index.c` (stamps in `game_gpu_thread` and `game_loop_frame` only)
- `run/src/display/x11/`, `run/include/display/x11/` (put and completion stamps only)
- `run/src/ui/hud/`, `run/include/ui/hud/`, and the F3 composer (three lines)
- `run/src/main/` (`--frame-log`)
- `tests/run/render/frame/`, `tests/cuda/` (tests below)

**Wiring outside them, as small as possible:** the CMake line for any new test; `plans/REPO_STRUCTURE.md` rows if a directory is added.

## Steps

1. **Wait for plan 006 phase 01 on `main`,** or rebase onto it. *Why: both edit `run/src/game/index.c` and F3.*
2. **Shade's events and the fresh count,** with a test that the five spans add up to the old shade number within 0.01 ms. *Why: C3 cannot be measured without them.*
3. **The `clock64` kernel,** with a test that it reads within 20 % of `cudaDevAttrClockRate` on an idle card under the wrapper, and below it when capped. *Why: C1 in every row.*
4. **The gpu thread's stamps, main's take stamps, the ring.** *Why: C2 and C7.*
5. **X put and completion stamps.** *Why: C6.*
6. **`--frame-log` and the minute line; F3's three lines.** *Why: Ahmed's own measurement, and what he sees while playing.*
7. **The `shake` path, `--at`, `--seed`, `--yaw`, live-capped, the new columns; the `--rose` mode; `gpu_watch.sh`.** *Why: the reproducible bench.*
8. **Check Ahmed's game is closed** (`pgrep -af 'build/release/bin/minecraft_c'` prints nothing; if not, ask him and wait). **Run the experiments** in the table, each through the wrapper. *Why: the numbers.*
9. **Ask Ahmed for his own frame log;** read it; fill the results. *Why: C6 and the real picture.*
10. **README of `bench/world/` with the results; tick todos as they land.**

## Todo

- [ ] Plan 006 phase 01 is on `main`, or this branch is rebased onto it
- [ ] Shade's five kernels have their own events and card-lane spans; `frame_timings_t.shade_ms[5]`; they add up to the shade pass
- [ ] The fresh AO count comes home with every picture
- [ ] `frame_gpu_clock_mhz`: the one-warp `clock64` kernel, once a second
- [ ] The gpu thread's stage stamps, main's take stamps and tick, slot accounting, in a 4096-record ring in the World arena
- [ ] X put-to-completion latency and main's block on `display_x11_wait_ready`, per frame
- [ ] F3: card ms mean, p99 and max over 1 s; pictures a second, late, idle; turn speed and card MHz
- [ ] `--frame-log <path>`, release builds included; the watch's minute line gains interval p99 and late count
- [ ] `bench_world`: the `shake` path (yaw and pitch), `--at`, `--seed`, `--yaw`, live-capped rows, new history columns
- [ ] `bench_world --rose` with CSV, polar PNG and the worst direction's shot
- [ ] `bench/world/gpu_watch.sh`, killed at the end of each run, checked gone
- [ ] Ahmed's game confirmed closed before each run (`pgrep` prints nothing)
- [ ] Experiments C1 to C9 and the ruled-out line run; the results table below filled in, each with its rows and commands
- [ ] Ahmed's own `--frame-log` shake read and summarised below
- [ ] The cost rose at his spot, render distance 32 and 64, looked at and described below
- [ ] `bench/world/README.md`: the new path, flags and results
- [ ] Every golden picture and every settled still row's hash unchanged

## Results (filled in by this phase)

Card state for each run (from the sidecar): MHz, enforced power limit, temperature, throttle reasons.

| Cause | Rows and command | Result | Verdict: confirmed, smaller, or refuted |
|---|---|---|---|
| C1 | | | |
| C2 | | | |
| C3 | | | |
| C4 | | | |
| C5 | | | |
| C6 | | | |
| C7 | | | |
| C8 | | | |
| C9 | | | |
| Coherence and L2 | | | |

| Row | Interval p50 / p99 / worst | Late | Card mean / p99 | Shade: water / surface / sun / AO / fresh | Fresh mean | $P$ | Wait for take | MHz |
|---|---|---|---|---|---|---|---|---|
| `spot/shake-0` | | | | | | | | |
| `spot/shake-720` | | | | | | | | |
| `spot/shake-1440` | | | | | | | | |
| `spot/shake-720-live-capped` | | | | | | | | |
| `spot/shake-720-live` | | | | | | | | |
| `spot/shake-720-live-capped-r64` | | | | | | | | |

The master's section 4 "Today" column is filled from this table.

## Tests

### `run_render_frame` (existing, extended)

- **`shade_spans_add_up_to_the_shade_pass`.** Over 30 frames of the real path, the five spans' sum is within 0.01 ms of the shade pass's own time on every frame.
- **`the_fresh_count_is_the_strip_on_a_turn`.** A settled world, two frames, the second turned 12° right. The fresh count is at most the formula's 17.2 % of pixels plus 1 %, and at least half of it: neighbours lower it, never raise it.
- **`a_still_camera_has_no_fresh_pixels_after_the_first_frame`.** The fresh count is 0 on frames 2 to 10 at a fixed camera, TAA jitter included.
- **`the_frame_log_has_one_line_per_picture`.** The real threads for 2 s with `--frame-log`. Lines equal pictures taken; columns parse; stamps increase.
- **Golden pictures:** unchanged.

### `render_feature_bridge` (`tests/cuda/bridge/`, a new case)

- **`the_clock_kernel_reads_a_plausible_clock`.** Between 300 and `cudaDevAttrClockRate` × 1.1 MHz.

### `bench_world`'s own self-check

- **`shake-0` hashes as a still row** at the same place and heading.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before any run below
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(render_feature_.*|cuda_traverse|run_display_x11)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 01: before"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_r64.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --render-distance 64; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --rose --at -80 134 418 --seed <his seed> --render-distance 64 --shots /tmp/mc_c_shake
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

`tests/cuda/*` are the `render_feature_*` tests and `cuda_traverse`; `tests/run/display/x11/` is `run_display_x11`.

## Gate

Ahmed reads:

- the results table, cause by cause;
- his own frame log's summary: pictures a second while still and while shaking, late share, put latency;
- the cost rose at render distance 64, and the shot of the worst direction;
- the card's clock and power limit during the runs.

He says:

1. Does the table explain what he feels when shaking his head?
2. Q5: will he try the `performance` platform profile once for a second measurement?
3. Q8: are master section 4's targets right?
4. Q6: what is his panel's refresh rate?

His answers are recorded below and in the phase files they affect. If C1 explains nearly everything, phases 04 and 06 may shrink: recorded here and in the master's open questions.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Ahmed's game is open when the agent wants to measure | Ask and wait; never touch it; never measure with another context on the card unless that is the experiment (C8) |
| Events between shade's kernels change its timing | An event costs microseconds (`cuda/src/frame/README.md`); the spans-add-up test, and the shade pass's mean before and after within 1 % |
| The `clock64` reading disagrees with `nvidia-smi` | Both recorded; a difference over 5 % is an Issue found, and the sidecar is trusted |
| Settled shake at 60 Hz steps is not what a capped live run sees | That is why both are run; settled isolates per-speed cost, live shows pacing |
| `--frame-log` costs something in release | A null check when off; when on, writes once a minute off a ring |
| The card's cap moves during a run, as plans 004 and 005 saw | The sidecar's lines are beside every row; rows compared only at similar MHz, and that is said |
| F3 conflicts with plan 006 phase 01's lines | Built after it is on `main` (step 1) |

## Commits

Appended as they land:

## Issues found

None.
