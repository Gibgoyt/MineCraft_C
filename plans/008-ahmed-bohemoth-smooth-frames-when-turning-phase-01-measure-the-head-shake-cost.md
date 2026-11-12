# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 1: Measure the head-shake cost

**Status:** tools built and tested, experiments run and recorded; waiting for Ahmed's own frame log (C6) and his answer to Q5   (updated 2026-11-12)

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

- [x] Plan 006 phase 01 is on `main`, or this branch is rebased onto it
- [x] Shade's five kernels have their own events and card-lane spans; `frame_timings_t.shade_ms[5]`; they add up to the shade pass
- [x] The fresh AO count comes home with every picture
- [x] `frame_gpu_clock_mhz`: the one-warp `clock64` kernel, once a second
- [x] The gpu thread's stage stamps, main's take stamps and tick, slot accounting, in a 4096-record ring in the World arena
- [x] X put-to-completion latency and main's block on `display_x11_wait_ready`, per frame
- [x] F3: card ms mean, p99 and max over 1 s; pictures a second, late, idle; turn speed and card MHz
- [x] `--frame-log <path>`, release builds included; the watch's minute line gains interval p99 and late count
- [x] `bench_world`: the `shake` path (yaw and pitch), `--at`, `--seed`, `--yaw`, live-capped rows, new history columns
- [x] `bench_world --rose` with CSV, polar PNG and the worst direction's shot
- [x] `bench/world/gpu_watch.sh`, killed at the end of each run, checked gone
- [x] Ahmed's game confirmed closed before each run (`pgrep` prints nothing)
- [ ] Experiments C1 to C9 and the ruled-out line run; the results table below filled in, each with its rows and commands (all but C6, which is Ahmed's own frame log, and C1's second run under the `performance` profile, which is his command: Q5)
- [ ] Ahmed's own `--frame-log` shake read and summarised below
- [x] The cost rose at his spot, render distance 32 and 64, looked at and described below
- [x] `bench/world/README.md`: the new path, flags and results
- [x] Every golden picture and every settled still row's hash unchanged (but F3's own two, which show its four new lines by design: `debug_640x480.png` and `corner_debug_640x480.png`, regenerated and looked at; `the_marks_change_no_picture` holds the marks to the same bytes)

## Results (filled in by this phase)

All on Ahmed's card (RTX 2080 Super in the laptop), his game closed, `build/profile/bin/bench_world_shake` at his spot (`--at -80 134 418 --seed 14148492376765118422`, the seed from `saves/test_7/world.meta`, whose state file is from 15:18 today), 1920x1080, every effect of play, the sidecar beside every run. Logs and CSVs were in `/tmp/mc_c_shake/` (not kept in the repo); the rows are in `bench/world/shake/history.tsv`.

**The card's state in every run (sidecar, 306 samples of the first run, and the same in the others):** enforced power limit 50 W; drawing 40 to 42 W, **under** the limit; clock event reasons `0x24` (SW power cap and **SW thermal slowdown**) on 305 of 306 samples; GPU 76 to 81 °C; CPU package 94 to 100 °C, fans at their maximum. `nvidia-smi` says 300 MHz (its floor) whenever the game draws and 1575 MHz while a world opens. **The `clock64` kernel says the SMs did 148 MHz's worth of cycles a second for most of every run**, with readings from 35 to 813 MHz. The two disagree by 2x at the floor: see C1. Because the speed moved within rows, the settled rows were run again with the clock read before every picture, and compare in **Mcycles** (ms x MHz / 1000).

| Cause | Rows and command | Result | Verdict |
|---|---|---|---|
| C1 card cap | every row; sidecar beside each; `performance` profile not tried (Q5, Ahmed's command) | Effective SM clock 148 MHz (clock64) for most of every run, `nvidia-smi` 300 MHz, at 41 W of a 50 W limit, with SW thermal slowdown active and the CPU at 94 to 100 °C. Card time scales with it: post with TAA, a per-pixel pass, is 0.62 to 0.64 Mcycles at 148, 158 and 459 MHz; a still picture at the spot is 5.5 to 5.6 Mcycles: **3.2 ms at 1750 MHz, 9.3 ms at 600, 38 ms at 148** | **Confirmed, and worse than the master thought:** the card runs at 148 MHz, a twelfth of its peak, not 600; and what holds it is heat (thermal slowdown with the CPU at 100 °C), not the 50 W cap, which it is under |
| C2 slot rule | `spot/shake-0-live-capped`, `spot/shake-720-live-capped` against `spot/shake-720-live` | Still, capped, at 547 MHz: card 9.80 ms, yet pictures every 16.67 ms (p50; p99 16.71), **87 % late**, and the gpu thread waited **5.74 ms a picture** for main to take the last: the card idled a third of the time. 720°/s capped: intervals only in whole slots (2 to 10). Uncapped: intervals follow the card (58.4 against 59.0 ms) | **Confirmed.** Uncapped was no faster here (16.8 against 23.1 pictures/s): it ran hotter, at 158 against 215 MHz (C9) |
| C3 fresh AO | settled `shake-0/90/360/720/1440`, AO on and `--features-off 400` | Fresh pixels 503, 63.6 k, 224 k, 390 k, 506 k (0.02, 3.1, 10.8, 18.8, 24.4 % of the picture); the strip formula with 95 % of pixels having a surface says 51 k, 187 k, 338 k, 576 k: **within +24 to -12 %**. `ao_fresh` 0.22 to 1.66 Mcycles from 0 to 720°/s (7.6x; 0.47 to 4.57 ms at 459 and 363 MHz). The whole picture 5.62 to 7.34 Mcycles (+1.72); with AO off 3.03 to 3.81 (+0.78). Live at 720°/s, pictures 33 to 58 ms apart turn 24 to 42° each, so **fresh is 634 k (31 %) and `ao_fresh` is 15.5 of the card's 38.6 ms**: it feeds itself | **Confirmed: the largest cost of turning.** Fresh AO is all of turning's added card work at 720°/s settled, and 40 % of the whole picture live |
| C4 TAA | the same rows with `--features-off 800` | Post with TAA: 0.64 Mcycles still, 0.92 to 0.98 turning (**+0.3, 0.5 ms at 600 MHz**); without TAA 0.21 whatever the speed. TAA also makes a still camera's AO dearer (jitter: 503 fresh pixels still, against 0 without TAA). All of TAA: 1.2 to 1.5 Mcycles still or turning | **Confirmed, small**, as the master said: 0.5 ms at 600 MHz |
| C5 direction | `--rose`, r32 and r64 | In ms at 600 MHz (each direction's cycles): r32 median 11.6 / 10.2 / 8.8 ms at pitch -20 / 0 / +15, p90 22.3 / 13.3 / 10.5, worst 29.2 (looking down the slope, yaw 245); r64 median 10.9 / 10.8 / 12.0, p90 16.0 / 15.7 / 17.3, worst 33.1 (up at the big peak, yaw 255, +15°). The far field is 1.0 to 1.6 Mcycles of 6.5 to 7.2 at r64 (15 to 23 %); r64's south half 14.2 ms median, north 9.1 | **Confirmed:** at 600 MHz most still directions are over one slot and the worst tenth over two. Each direction is normalised by a reading just before it, and the clock moved from 53 to 411 MHz meanwhile, so single directions are noisy; the medians and p90s stand |
| C6 X server | Ahmed's own frame log in a window (below) | Not measured: needs his run. The columns are there (`x_put_ms`, `x_wait_ms`, `x_idle_ms`) and F3's `X` | **Open** |
| C7 serial work | every live row's rings | **Readback 1.27 to 1.29 ms on every row at every clock** (a PCIe copy, not the SMs): 15 % of a slot. **P, the gpu thread's own work** (upload, far plan, fill, launches, wake-up), **0.03 to 0.16 ms** a picture | **Readback confirmed; P much smaller** than the master's 0.5 to 2 ms |
| C8 other CUDA work | `c8`: `spot/shake-720-live-capped --seconds 30`, `run_render_frame` (debug) started on the card 20 s in, one lock call | Card 55 ms before; **113 to 163 ms** while it ran, effective clock 148 down to 67 to 93 MHz (they take turns); interval p99 83 to 200 ms; 180 of 278 intervals past 11 slots | **Confirmed** |
| C9 heat | `spot/shake-720-live-capped-r64 --seconds 60`; the first run's sidecar over 309 s | 60 s: clock64 187 MHz in the first sixth, **148 MHz for the other five**, card 55 to 57 ms, P 0.10 to 0.12 ms: flat, because already at the floor. Over the first 5-minute run the GPU went 76 to 81 °C and clock64 from 450 to 800 MHz in the first rows to 148 within minutes, where it stayed for every run after | **Confirmed in effect:** the card reaches its thermal floor within minutes of load and stays; no further trend is visible once there |
| Coherence and L2 | settled `shake-90` against `shake-1440` (the same directions), `--features-off c00` | 2.89 against 3.03 Mcycles (+4.8 %; +7.6 % by ms x the row's mean MHz). Coarse 0.84 to 1.08 ms, primary 6.07 to 7.20, surfaces 6.19 to 7.13 | **Not settled at 3 %:** at most 5 to 8 %, inside this card's clock noise (61 to 149 MHz within each row). Small against C3 |

| Row | Interval p50 / p99 / worst | Late | Card mean / p99 | Shade: water / surface / sun / AO / fresh | Fresh mean | $P$ | Wait for take | MHz |
|---|---|---|---|---|---|---|---|---|
| `spot/shake-0` | 11.82 / 14.60 / 18.09 (settled: the card) | | 12.11 / 14.60; 5.62 Mcycles | 0.33 / 1.83 / 0.86 / 3.48 / 0.47 | 503 | (1.41 host) | | 459 |
| `spot/shake-720` | 18.59 / 22.22 / 22.83 | | 18.69 / 22.22; 7.34 Mcycles | 0.37 / 2.50 / 1.27 / 3.38 / 4.57 | 389 966 | (1.49 host) | | 363 |
| `spot/shake-1440` | 51.96 / 69.72 / 70.47 | | 41.99 / 69.72; 7.58 Mcycles | 0.85 / 5.54 / 2.93 / 6.79 / 13.35 | 505 787 | (1.64 host) | | 182 (59 to 423) |
| `spot/shake-0-live-capped` | 16.67 / 16.71 / 18.11 | 86.9 % | 9.80 / 13.45 | 0.27 / 1.46 / 0.79 / 2.70 / 0.36 | 3 588 | 0.03 | 5.74 | 547 |
| `spot/shake-720-live-capped` | 33.30 / 83.33 / 83.36 | 100 % | 38.62 / 75.38 | 0.72 / 4.33 / 2.22 / 5.14 / 15.50 | 634 461 | 0.09 | 4.48 | 215 |
| `spot/shake-720-live` | 58.36 / 75.03 / 75.04 | 100 % | 58.96 / 76.19 | 0.97 / 6.95 / 3.33 / 7.73 / 24.60 | 736 400 | 0.16 | 0 | 158 |
| `spot/shake-720-live-capped-r64` | 58.33 / 83.33 / 83.33 | 100 % | 56.03 / 75.58 | 0.94 / 5.90 / 3.09 / 6.71 / 21.68 | 755 432 | 0.11 | 4.13 | 182 |

The settled rows' ms are from the first run (one clock reading a second); their Mcycles from the second (a reading before every picture). The master's section 4 "Today" column is filled from this table.

**Ahmed's own frame log (C6), still to come.** Once this branch is on `main` and built (`tools/test-or-build.sh cmake --build --preset release`), he plays as always with one flag more:

    ./build/release/bin/minecraft_c --frame-log ~/shake.csv

stands still 5 s at his spot, shakes his head hard for 10 s, stands still 5 s, and quits. The agent reading it then fills C6 from `x_put_ms` and `x_wait_ms` against `card_ms`, and the still and shaking seconds from `interval_ms`, `late`, `fresh`, `turn_deg_s` and `card_mhz`. How to read the file is in `run/src/game/watch/README.md`.

**What it comes to.** Ahmed feels three things at once. (1) The card is at 148 MHz, held there by heat, so a still picture at his spot is already 10 to 40 ms; walking without turning, the slot rule then shows it every second or third slot, steadily, which reads as "sits nicely". (2) Turning makes each picture dearer, almost all of it fresh AO for the new strip (7.6x the fresh rays at 720°/s), and the slower pictures turn further each, so the strip widens: live at 720°/s the card's time went from 9.8 to 38.6 ms. (3) The slot rule turns that into 2 to 10 slot waits, and the card idles 4 to 6 ms a picture waiting for main. Directions add up to 2x on top (C5), and any other CUDA program doubles it again (C8).

**The cost rose** (`rose-spot-r32.png`, `rose-spot-r64.png`, in ms at 600 MHz): roughly round at pitch 0 and +15, between one and two slots; looking down (-20°) spikier, out to 29 ms. At r64 the dearest direction is up at the tall snowy peak to the south-west (yaw 255, +15°): a frame filled with the mountain and the far field behind it (`rose-spot-r64-worst.png`).

## Tests

### `run_render_frame` (existing, extended)

- **`shade_spans_add_up_to_the_shade_pass`.** Over 30 frames of the real path, the five spans' sum is within 0.01 ms of the shade pass's own time on every frame.
- **`the_fresh_count_is_the_strip_on_a_turn`.** A settled world, two frames, the second turned 12° right. The fresh count is at most the formula's 17.2 % of pixels plus 1 %, and at least half of it: neighbours lower it, never raise it.
- **`a_still_camera_has_no_fresh_pixels_after_the_first_frame`.** The fresh count is 0 on frames 2 to 10 at a fixed camera, TAA jitter included. *Measured otherwise: it is `a_still_camera_has_almost_no_fresh_pixels_after_the_first_frame`, at most 1 % of the surfaces; see Issues found.*
- **`the_marks_change_no_picture`** (added). Ten frames turning with the marks and ten without, from the same opening: the same last picture, byte for byte.
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

- `fa24367b` Measure where a turning picture's time goes: shade's kernels and the fresh AO count on the card's clock, the card's MHz from a clock64 kernel, the gpu thread's and main's stamps in two rings, X put latency, a release --frame-log, four F3 lines with the real interval on the screen, and bench_world_shake.
- `0123fc0a` Read the card's clock before every settled picture in bench_world_shake, so rows compare in cycles, draw the cost rose in ms at 600 MHz, let --only name several rows, and keep the first runs' history.
- `96b387c1` Record plan 008 phase 01's measurements: the card held at 148 MHz by heat, fresh AO as nearly all of turning's added card work, the slot rule's idle card, the cost rose, and a test that the marks change no picture.
- this commit: F3's corner golden with the four new lines, and the verification.

**Verification, as run (2026-11-12):** `pgrep -af 'build/release/bin/minecraft_c'` printed nothing before every GPU run. `tools/test-or-build.sh cmake --build --preset debug`: clean. `run_render_frame`: passed (the five new cases among them). `render_feature_.*`, `cuda_traverse`, `run_display_x11`: passed. `standards_lint`, `clang_format`: passed. `cmake --build --preset profile` and `--preset release`: clean, release's 16 KiB frame limit held. The whole debug suite (`ctest --preset debug -j 4`, 164 tests): 161 passed; `run_ui_hud_corner` failed on F3's golden (regenerated, now passes), and `run_climate_warning` and `run_ui_atlas` failed under the parallel load and pass alone. The bench lines of the Verification ran as `bench_world_shake` (its own program, see Issues found): every row at the spot with the sidecar, recorded with `--label "008 phase 01: before"`, the settled rows again with the clock on every picture, the rose at r32 and r64, and the experiments in the results table; `pgrep -af gpu_watch` and `pgrep -af "$PWD/build"` printed nothing after each.

## Issues found

**The card's clock: `clock64` against `nvidia-smi`.** At the floor they disagree by 2x: `nvidia-smi` says 300 MHz, the `clock64` kernel 148 (and as low as 35). The plan's default was to trust the sidecar past a 5 % difference. Here the kernel is the one that agrees with the work: card ms times the kernel's MHz stays constant across rows (post with TAA 0.62 to 0.64 Mcycles at 148, 158 and 459 MHz), and with `nvidia-smi`'s it would not. `nvidia-smi`'s `clocks.gr` reports the P-state's clock; what the SMs do under SW thermal slowdown is lower. One caveat: the kernel is timed by events, so time the card gives another context (the desktop, another CUDA program) also reads as a lower clock. Either way it is the speed the game gets.

**The X stamps were not seen in a window here.** `--test-world` refuses a window and the menu needs clicks, so the release build's frame log was checked in `run_render_frame` (the real threads, the same code) and the release build itself only built. Its X columns are first filled by Ahmed's own run (C6).

Ahmed's words, 2026-11-12, as this phase began: "only when shaking my head does frame rate tank; when walking around without my mouse/view frame rate sits nicely; i think F3 frame rate tab is also innacurate as fuck something laggy is happening and we need to figure out where". So the priority was the release frame log and F3 lines that show the real interval on the screen.

**What changed from the plan, and why:**

- **Shade's spans live in a `frame_marks_t`, not in `frame_timings_t` and `frame_gpu_t`.** Growing either grows `render_frame_t`, `render_t` and so `app_t`, which is at main's 16 KiB frame limit (16192 of 16384). So `frame_gpu_t`'s `open` byte gave its place to a `frame_marks_t *borrowed_marks` (started now means its first event is there), and the marks (shade's four events, the clock's two, `shade_ms[5]`, `fresh_count`, `card_mhz`, `queued_ns`) are in the World arena, made by `game_watch_open` and set on each frame by `game_watch_marks_arm`. With no marks a frame runs as before. `shade_frame_marked` is the marked call; `shade_frame_entities` is it with none.
- **Shade's spans are not on the profiler's card lane.** `tests/run/profiler` holds the card lane to one span a kernel adding up to the frame; nesting five under `shade` would break that test's sum. They are in the frame log, F3's source, and the bench instead.
- **The fresh count is copied home after the frame's wait** (4 bytes, `cudaMemcpy`), not mid-stream into pinned memory: a copy to pageable memory mid-stream makes the thread wait there for shade with the card idle while it then queues post, and pinning needs an allocation the One-Owner rule gives to `run/src/heap/`.
- **The `clock64` kernel is timed by events only.** Reading `%globaltimer` needs inline PTX, which the lint bans (section 7). Events also hold the kernel's start, a few microseconds; `FRAME_CLOCK_CYCLES` is 200 000 (0.11 ms at 1750 MHz, 0.33 ms at 600) so that is about 1 %.
- **Two rings, one writer each, not one record joined by main.** A picture's card numbers are known only after its publish, when main may already have taken it. So the gpu thread writes a picture ring and main a take ring; what main needs at the take (the picture's number, when it began, main's frame count then) goes through the exchange slot before the draw; the sim joins them by number, 20 times a second, for the frame log and the minute line. See `run/src/game/watch/README.md`.
- **"Late"** is a take more than one of main's frames after the frame the picture began in: the first tick after a picture starts is the one it is aimed at. The plan's "the first tick at least 8.33 ms after its start" would call every picture that starts mid-slot late.
- **The X stamps reach the game through `game/session`**, one call in `game_session_window_look` (outside this phase's paths, the smallest wiring there is: only the session holds the display). Runs without the session (`--test-world`) log 0 for them.
- **`hud_compose_us` and `hud_present_us` moved from `game_t` to `game_watch_t`**, to make room in `game_t` (at its 1600 bytes) for `borrowed_frame_log`, the `--frame-log` path.
- **F3 has four new lines, not three:** `shown last p99 max` (the real interval on the screen, Ahmed's ask), `card mean p99 max`, `late idle X`, `turn deg/s MHz`. With a second of pictures, the five pass lines and `gpu total` are that second's means, not one picture's. `tests/run/ui/hud/fixtures/debug_640x480.png` was regenerated with `MC_GOLDEN_WRITE=1` and looked at: the four lines sit under `uploads`, inside the panel.
- **The bench is its own program, `bench_world_shake` (`bench/world/shake/`),** with its own `history.tsv`: `bench/world/index.c` is at 1494 lines, the standard's split. `gpu_watch.sh` is in `bench/world/`.

**Measured while building the tests (debug build, 320x180, render distance 6, every effect, at 1733 MHz):**

- Shade's five spans add up to the shade kernel's time to within 0.0000 ms on every one of 30 frames.
- **A still camera with TAA is not free of fresh AO pixels:** 72 to 114 a frame of 27 067 with a surface (0.3 to 0.4 %), from the jitter at edges. The phase's test said 0; it now says at most 1 % and is named `a_still_camera_has_almost_no_fresh_pixels_after_the_first_frame`.
- **A 12° turn makes 16.7 % of the surface pixels fresh, against the strip formula's 17.2 %.** The formula is right; sky has no surface to be fresh, so the share is of the pixels with one.
- The clock kernel read 1733 MHz on an idle-then-loaded card whose stated peak is 1755 MHz.
- The frame log over 2 s of real threads: 239 pictures taken, 240 lines (one header).
