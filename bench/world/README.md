# bench/world

**Purpose:** defines the standard worlds and camera paths, runs the whole game over them on the real card with no display, and keeps a history of what they came to, against the plan's targets of 120 pictures a second at 1080p, render distance 32 and all effects, and a tick of at most 5 ms at 100 mobs.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region per row, for an `app_t`, its samples, a column's working memory, the picture and its PNG, with what those reserve, released before that row's process exits.
**Lifetime:** Process. Each row has a process of its own.
**Threads:** the parent, which has no threads and no card. In a row's process: main and the job workers, and the sim and gpu threads on a live row.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 600 build/profile/bin/bench_world`, so no build shares the CPU. Exit status 0 if every row measured what it was meant to, 1 if one did not, or 2 if it could not start. A target missed is printed as `MISSED` and does not change the status, as the plan calls them goals to measure and not promises. The phase gate wants the misses said plainly, and a failing command would hide the rest of the table.
**May be called by:** people.
**Depends on:** `main/index.h` for `app_t`, `game`, `render`, `gpu_bridge`, `world`, `terrain`, `job`, `thread`, `mob`, `entity`, `item`, `player`, `ui`, `texture`, `physics`, `math`, `simd`, `profiler`, `display/headless/png`, `file`, `heap`, `time`, `log`, and the generated `tables/`. No display and no input: it never opens `/dev/dri` or `/dev/input`.

It takes at most 3584 MiB of the card: `BENCH_VOXELS_MIB` and the budget's textures and frames. The most `nvidia-smi` saw it use, sampled every 2 s over a whole run, was 2498 MiB.

Run it on the `profile` preset, which is release code with frame pointers and scopes that cost 2 ns each with no lanes. `debug` numbers mean nothing.

## The worlds

Every world is the game's default seed, `TERRAIN_SEED_DEFAULT`, and 1920 by 1080. The day does not go on, so that the sun is where it was. A place is looked for, and the same seed finds the same one.

| World | Where | Eye | Time | What it asks |
|---|---|---|---|---|
| `plains` | The nearest plains from the world's middle, plains too for 64 blocks each way | 24 over the ground | Morning | Open ground and long sight |
| `jungle` | The same, of jungle | 16 | Morning | Leaves in the way of every ray, and of every shadow ray |
| `jagged_peaks` | The same, of jagged peaks | 12 | Morning | Steep rock near and far |
| `deep_ocean` | The same, of deep ocean | 10 | Morning | Water, with its reflection and tint, as far as there is |
| `cave` | The nearest gap at least 12 under the ground and 5 tall, with air 5 away each way at eye height | In it | Morning | Everything close, and stored light only |
| `night` | The `plains` place | A player's | Midnight | Mobs, and what they cost a tick and a frame |

## The paths

| Path | What it does | Frames |
|---|---|---|
| `orbit` | Stands still, clear of the ground for 36 blocks around, and turns right round, looking a little down. No streaming: all of it is the picture | 30 to warm up, then 240 |
| `flight` | Straight on at one height, clear of all the ground on the way. Settled, 0.5 blocks a frame. Live, 100 blocks a second, the fly camera's fast speed | 30 and 240, or 10 s |
| `walk-mobs` | A player walks a wide circle, among 100 mobs, a third of each kind, spawned within 32 blocks and topped up between ticks, with their real minds. It takes no harm, so that it stays something to chase | 30 and 400, a tick to a frame |

## The two ways of running

- **Settled:** one thread, and the world is whole before every picture. Every run draws the same pictures, which `picture` says: it is a hash of the last one, and if it has moved, the row is not measuring what it was. The `fps` is what the card's clock allows, 1000 over its mean. A tick and a draw are timed apart, so `tick` is `scene.tick`, and `past the card` is what a draw took past the card's clock: settling, the HUD and the copy out.
- **Live:** the game's own threads against the clock, uncapped, with main asking at 120 Hz as a display would. The `fps` is what came out. What a picture shows depends on how streaming kept up, so these rows have no hash, and move more from run to run.

## Rows

`bench_world --list` prints them. Those at render distance 32 are held to the 120 fps, and `night/walk-mobs` to the tick as well. `plains/orbit-godrays` has the one effect the game leaves off by default, and is the only row with all of them: `--god-rays` asks for that on the rest. `plains/orbit-rain`, `-snow`, `-thunder` and `-fog` are `plains/orbit` in a fixed weather (plan 004 phase 08's `--weather`), as there is no climate here: what rain, snow, lightning and weather fog cost.

A row is not sound, and the exit status says so, if the last picture has fewer than 8 colours on a grid of 64 by 36 (an eye in a wall is very fast), if anything called the heap while it was timed, or, on the walk, if the minds did no searches, the store fell under 100 or the player died.

## Flags

- `--only <text>`: the rows whose name holds this, or `=<name>` for exactly that row.
- `--features-off <hex>`: take these `SHADE_FEATURE_*` bits away, which is how to see what each effect costs.
- `--god-rays`: every row with god rays too, as only that is all effects. Not with `--record`, as a line would carry another row's name.
- `--live`: every row but the walk on the game's own threads against the clock, an orbit included. Not with `--record`, for the same reason.
- `--weather <kind>`: every row in this weather, as the game's `--weather` takes it: `clear`, `rain`, `snow`, `sleet`, `storm`, `thunder`, `fog`, `dust` or `hurricane`. Not with `--record`, for the same reason.
- `--render-distance <chunks>`: the rows held to the frame target at this render distance, not 32, with full detail still the game's 32, so the rest is the far field. What a raised default costs (plan 005 phase 8). The history's `render_distance` column says which it was.
- `--record <file>`: add a line per row to a history. **`bench/world/history.tsv` is the repository's.** New columns go on the end of its first line, and old lines stay.
- `--label <text>`: says what was measured, in the history, such as the commit.
- `--shots <dir>`: the last picture of each row as a PNG, to see what a row is looking at.
- `--trace <file>`: with an `--only` that picks one row, its Chrome trace, and the profiler's table on stdout. Not in a release build.
- `--list`: print the rows, and do nothing.

To add to the history after a change that may move these numbers:

    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --record bench/world/history.tsv --label "<commit>: <what changed>"

## Reading these numbers

Compare a row's card numbers, with its `picture` the same. On this shared machine, with the same pictures, the card's numbers moved up to about 10% between runs. **What runs on the CPU moved much more:** the `tick` and `past the card` of `night/walk-mobs` come out in two groups, as in the table. Stage 17.3 found out why: **it is the CPU's heat.** The package reaches 93 to 100 C under a run, and when it does, everything on the CPU takes 2 to 3 times as long while the card's numbers do not move: see that stage file's issue 1. So take the tick from several runs, say which group it is, and note that a full run is nearly all in the hot one.

| Run of `night/walk-mobs`, same picture | Tick mean | Tick p99 | Past the card |
|---|---|---|---|
| Quiet: 3 of 9, each run alone with `--only` | 0.74 to 0.81 | 3.5 to 4.9 | 2.0 to 2.2 |
| Slow: 6 of 9, every one of the 4 full runs and 2 alone | 2.0 to 2.6 | 8.8 to 13.4 | 7.0 to 7.8 |

`plains/flight-live` came to 64, 67, 67, 93 and 86 fps in five runs. Read it beside its `columns`: 3752, 3293, 3112, 1182 and 1187. A live flight that falls behind has less world to draw, so its better number is the worse flight.

## Results

Each run's every number is in `history.tsv`. The tables here are what to read.

### 2026-11-03, Stage 17.2: first results, before the cost pass

`build/profile/bin/bench_world --record bench/world/history.tsv`, exit status 0, on the shared machine under the build lock, with the desktop on the same card. In ms, on the card's clock, except where it says. Target: 120 fps. **5 of the 12 rows at render distance 32 met it, and 7 missed. The slowest is `jungle/orbit`, at 34 fps.**

| Row | Way | fps | Mean | p99 | Coarse | Primary | Shade | Post | Readback | Opened in, s | |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `plains/orbit` | settled | 90.8 | 11.01 | 12.03 | 0.65 | 0.58 | 7.70 | 0.81 | 1.28 | 5.8 | MISSED |
| `plains/flight` | settled | 99.1 | 10.09 | 10.66 | 0.47 | 0.58 | 7.07 | 0.69 | 1.28 | 5.9 | MISSED |
| `plains/flight-live` | live | 85.6 | 11.39 | 16.75 | 0.27 | 0.50 | 8.45 | 0.63 | 1.29 | 6.6 | MISSED |
| `plains/orbit-godrays` | settled | 70.5 | 14.18 | 16.07 | 0.72 | 0.60 | 8.28 | 3.30 | 1.28 | 12.2 | MISSED |
| `jungle/orbit` | settled | 34.0 | 29.42 | 33.23 | 0.58 | 0.56 | 26.16 | 0.84 | 1.28 | 11.9 | MISSED |
| `jungle/flight-live` | live | 87.6 | 11.88 | 27.66 | 0.22 | 0.46 | 8.31 | 0.59 | 1.29 | 12.3 | MISSED |
| `jagged_peaks/orbit` | settled | 161.5 | 6.19 | 7.04 | 0.33 | 0.48 | 3.28 | 0.83 | 1.28 | 9.3 | met |
| `jagged_peaks/flight-live` | live | 236.4 | 4.01 | 5.04 | 0.16 | 0.32 | 1.68 | 0.53 | 1.28 | 9.4 | met |
| `deep_ocean/orbit` | settled | 69.9 | 14.30 | 16.75 | 0.78 | 0.57 | 10.80 | 0.88 | 1.28 | 11.4 | MISSED |
| `deep_ocean/flight-live` | live | 128.9 | 7.80 | 11.63 | 0.19 | 0.41 | 4.86 | 0.64 | 1.28 | 9.4 | met |
| `cave/orbit` | settled | 206.6 | 4.84 | 5.30 | 0.05 | 0.34 | 2.35 | 0.82 | 1.28 | 14.1 | met |
| `night/walk-mobs` | settled | 167.2 | 5.98 | 7.58 | 0.05 | 0.49 | 3.14 | 1.03 | 1.28 | 7.8 | met |
| `plains/flight-live-r128` | live | 129.3 | 8.30 | 16.32 | 0.16 | 1.96 | 3.49 | 0.72 | 1.31 | 124.1 | no target |
| `plains/orbit-r512` | settled | 66.3 | 15.08 | 17.86 | 0.78 | 3.07 | 9.06 | 0.88 | 1.28 | 127.8 | no target |

The rest of what the plan's section 2 asks about, from the same run:

- **Tick at 100 mobs, target 5 ms:** in this run the p99 was 9.70 ms and the mean 2.18, so **MISSED**. In a quiet run the same pictures came at a p99 of 3.5 to 4.9, under it. See "Reading these numbers". `bench/mobs` has its own number for the mobs' part alone, on a floor with nothing else going on.
- **Heap calls while timed:** 0 in every row, flights and the walk included.
- **VRAM:** 1798 MiB held for voxels at render distance 32, of which 482 to 825 MiB ever had sections in it. 2004 MiB at 128 and 2164 MiB at 512, which is the far field's 206 and 366 MiB on top. The plan's rough estimate at render distance 32 was about 100 MiB. Textures 2 MiB, frames 189 to 204 MiB. All inside the budgets of 6144, 256 and 256 MiB.

`Opened in` at render distance 128 and 512 says what filling a far field costs: two minutes, against 6 s with none. Stage 17.3 owns all of these.

### 2026-11-03, Stage 17.3: after the top-cost pass

The same command, exit status 0, 449.6 s, and the same machine. **11 of the 12 rows at render distance 32 meet 120 fps, and 1 misses: `deep_ocean/orbit`, at 117.0 fps, by 0.21 ms.** The whole account is in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-3-cost-pass.md`.

| Row | Way | fps | Mean | p99 | Coarse | Primary | Shade | Post | Readback | Opened in, s | |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `plains/orbit` | settled | 150.7 | 6.64 | 6.92 | 0.63 | 0.55 | 3.37 | 0.80 | 1.28 | 4.8 | met |
| `plains/flight` | settled | 152.1 | 6.58 | 6.79 | 0.46 | 0.55 | 3.62 | 0.67 | 1.28 | 5.0 | met |
| `plains/flight-live` | live | 129.4 | 7.36 | 9.13 | 0.35 | 0.53 | 4.50 | 0.64 | 1.28 | 5.0 | met |
| `plains/orbit-godrays` | settled | 143.6 | 6.97 | 7.25 | 0.65 | 0.55 | 3.38 | 1.11 | 1.28 | 9.9 | met |
| `jungle/orbit` | settled | 123.5 | 8.10 | 8.60 | 0.53 | 0.52 | 4.97 | 0.81 | 1.28 | 9.0 | met |
| `jungle/flight-live` | live | 154.8 | 6.31 | 8.14 | 0.24 | 0.48 | 3.50 | 0.58 | 1.28 | 9.3 | met |
| `jagged_peaks/orbit` | settled | 174.4 | 5.73 | 6.33 | 0.30 | 0.42 | 2.92 | 0.81 | 1.28 | 8.4 | met |
| `jagged_peaks/flight-live` | live | 235.2 | 4.01 | 5.03 | 0.14 | 0.31 | 1.73 | 0.51 | 1.28 | 8.4 | met |
| `deep_ocean/orbit` | settled | 117.0 | 8.54 | 8.91 | 0.65 | 0.51 | 5.30 | 0.81 | 1.28 | 14.1 | MISSED |
| `deep_ocean/flight-live` | live | 173.0 | 5.65 | 7.13 | 0.16 | 0.37 | 3.08 | 0.58 | 1.28 | 8.3 | met |
| `cave/orbit` | settled | 193.6 | 5.17 | 5.27 | 0.05 | 0.34 | 2.71 | 0.80 | 1.28 | 18.5 | met |
| `night/walk-mobs` | settled | 164.9 | 6.07 | 6.91 | 0.04 | 0.43 | 3.32 | 0.99 | 1.28 | 7.2 | met |
| `plains/flight-live-r128` | live | 152.2 | 6.77 | 9.61 | 0.15 | 1.99 | 2.39 | 0.65 | 1.32 | 105.0 | no target |
| `plains/orbit-r512` | settled | 109.6 | 9.12 | 9.48 | 0.65 | 2.94 | 3.44 | 0.81 | 1.28 | 117.8 | no target |

- **Tick at 100 mobs, target 5 ms:** in this run the p99 was 8.90, so **MISSED**, in the group a hot CPU is held down to. From a cool one, 8 runs of 9 in a row came to 2.97 to 4.05. See "Reading these numbers".
- **Heap calls while timed:** 0 in every row, as before.
- **VRAM:** frames are 204 MiB, 220 at render distance 128 and 512, up 16 MiB for AO's list. The rest as before. All inside the budgets.

A live row now draws more of the world than it did, as the picture costs less, so it can go down when a row keeps up better: `plains/flight-live` made 2303 columns in this run and 1187 in the last.

Picture hashes, which should be the same on a later run unless the pictures were meant to change. A pixel's light is now added up in another order, so most moved, by 1 in 255 on at most 0.001% of channels: `plains/orbit` CB2E45E9179493B0, `plains/flight` 850D8837E1C32C4E, `plains/orbit-godrays` B3456ACD4DE95E4C, `jungle/orbit` 3482D4FFA58BBC2B, `jagged_peaks/orbit` 5DFEC9A05B5E3633, `deep_ocean/orbit` 6CE16CAB894553F4, `cave/orbit` 3825C2ED4113BFFD, `night/walk-mobs` F279DFD63D183B1E, `plains/orbit-r512` 1F9DDD009389C74E.

### 2026-11-03, the phase gate check: what "all effects" and "the game's own threads" come to

The table above has god rays on only in `plains/orbit-godrays`, and a settled row's fps leaves out the CPU. So, one row at a time on the same code, beside the two full runs of that code:

| Row | Effects as above | `--god-rays` | `--live`, god rays off |
|---|---|---|---|
| `plains/orbit` | 149.7 to 150.7 | 142.6 to 143.6 | |
| `plains/flight` | 151.0 to 152.1 | 141.5 | |
| `plains/flight-live` | 129.4 to 134.8 | 133.7 to 135.6 | as it is |
| `jungle/orbit` | 122.5 to 123.5 | **110.2 MISSED** | **112.2 MISSED** |
| `jungle/flight-live` | 153.4 to 154.8 | 125.4 | as it is |
| `jagged_peaks/orbit` | 173.4 to 174.4 | 138.0 | 173.3 |
| `jagged_peaks/flight-live` | 233.6 to 235.2 | 206.5 | as it is |
| `deep_ocean/orbit` | **115.8 to 117.0 MISSED** | **107.4 MISSED** | **109.1 MISSED** |
| `deep_ocean/flight-live` | 169.1 to 173.0 | 139.6 | as it is |
| `cave/orbit` | 193.4 to 193.6 | 144.0, the same picture | |
| `night/walk-mobs` | 163.6 to 164.9 | 143.9 | |

God rays cost 0.3 to 0.5 ms on `plains`, and 0.7 to 1.8 ms on a settled row elsewhere. **So with all effects on, 2 of these 11 views miss 120 fps, by 8% and 11%.** Blank cells were not run. These runs are not in `history.tsv`.

## Plan 005 phase 14: how long the world takes to fill in

Each row now prints `filled in:` and the history has five more columns, `fill_first_s`, `fill_near_s`, `fill_rings_s`, `fill_far_s` and `fill_all_s`: the opening's fill-in report (`run/src/game/fill/`), in seconds from `game_open`. The first column drawn, the rings out to 4 chunks, every ring out to full detail, the far field done (0 with none), and all of it. A settled row settles on one thread, so this is the workers' speed with nothing else in the way.

`eb650bd3: plan 005 phase 14, fill-in times, before the starvation fixes` in `history.tsv`, 2026-11-10, `build/release/bin/bench_world --record bench/world/history.tsv`, exit status 0, on the release preset with other agents' builds queued behind the lock (so the fps are not comparable with the profile rows above). At render distance 32 every row had the rings out to 4 chunks in 0.13 to 0.36 s and all 33 rings in 7.4 to 11.0 s. **With a far field the near rings wait for it:** `plains/flight-live-r128` drew all 33 near rings at 107.8 s and the far field at 145.1 s, and `plains/orbit-r512` at 96.1 s and 149.7 s, against 7.5 s for the same place at 32. That is phase 14's starvation, in the bench.

## Plan 004 phase 11, second pass: from a cool machine

`ffc94238: plan 004 phase 11, second pass, cool machine` in `history.tsv`, 2026-11-08, `build/profile/bin/bench_world --record bench/world/history.tsv`, exit status 0, 602.9 s, started with the CPU idle (load average about 1) and the card idle at 65 degrees C. **3 of 16 rows at render distance 32 met 120 fps** (`jungle/flight-live` 120.7, `jagged_peaks/orbit` 160.3, `jagged_peaks/flight-live` 151.8); `plains/orbit` 85.1, `plains/flight` 102.8, `cave/orbit` 89.8, `night/walk-mobs` 77.8, `deep_ocean/orbit` 41.8. The four weather rows, last, read 23.9 to 25.4 fps, but every pass of theirs, coarse and primary included, which weather does not touch, is 3.4 to 3.7 times `plains/orbit`'s: the card had slowed again by then (`nvidia-smi` mid-run: 1575 MHz of 2100, slowdown reasons power cap and thermal). So they say nothing about weather's cost, which phase 08's like-with-like runs below still give. `night/walk-mobs` tick p99 1.974 ms, worst 2.751 (target 5, met).

## Plan 004 phase 11: the whole table again, with the climate in the game

`8fadcbd0: plan 004 phase 11, before tuning` in `history.tsv`, 2026-11-08, `build/profile/bin/bench_world --record bench/world/history.tsv`, exit status 0, 716.5 s, straight after a 107-minute CPU survey and the profile build. **0 of 16 rows at render distance 32 met 120 fps; the slowest, `deep_ocean/orbit`, 12.1 fps.** The card was the slowed card phase 08 describes below: just after the run `nvidia-smi` read 77 °C and 300 MHz idle, and the shade pass alone took 28 ms a frame on `plains/orbit`. The rows' fps: `plains/orbit` 21.3, `plains/flight` 35.5, `plains/flight-live` 21.6, `plains/orbit-godrays` 18.1, `jungle/orbit` 14.3, `jungle/flight-live` 42.3, `jagged_peaks/orbit` 46.4, `jagged_peaks/flight-live` 52.9, `deep_ocean/orbit` 12.1, `deep_ocean/flight-live` 29.0, `cave/orbit` 32.7, `night/walk-mobs` 28.2. Weather, all in the same run: `plains/orbit` 21.3 clear, rain 20.5, snow 20.2, thunder 20.4, fog 21.3 fps: rain and snow cost 1.8 and 2.6 ms a frame of 47, as phase 08 measured within the card's noise. **The sim tick of `night/walk-mobs`: mean 0.816, p99 2.064 ms**, against 5 ms: met. These frame rates say more about the card's state than about the climate, which runs on the CPU; Ahmed decides whether to rerun on a cold card.

## Plan 004 phase 08: what weather costs

`004 phase 08, before` and `004 phase 08, after` in `history.tsv`, on 2026-11-06, are the whole table before the renderer knew weather and after. **The card now slows to about a third after some 30 s of steady load** (79 to 82 degrees C, and every pass slows alike), so a later row of a run reads slower than an earlier one whatever it does, and the last rows of the `after` run, the four weather rows among them, were measured slowed. Rows with no weather, which is every row but those four, are the same pictures with the same post pass: `jagged_peaks/orbit` post 1.62 ms before and 1.61 after, `deep_ocean/orbit` 2.41 and 2.43, `cave/orbit` 3.04 and 2.99; with no weather the kernels never look at any of it.

To compare like with like, each weather row was run in its own process right after `=plains/orbit` in its own, in turn (`tools/test-or-build.sh sh` over `--only =plains/orbit` and `--only =plains/orbit-<kind>`), and the post pass taken against the shade pass of the same run, which nothing of weather touches:

| Row | Post, ms | Shade, ms | Post over shade | Post added at the clear row's shade |
|---|---|---|---|---|
| `plains/orbit` (no weather) | 2.36 | 8.80 | 0.268 | |
| `plains/orbit-rain` (8 mm an hour) | 3.05 | 9.41 | 0.324 | 0.49 |
| `plains/orbit-snow` (3 mm an hour, snow) | 6.83 | 19.21 | 0.356 | 0.75 |
| `plains/orbit-thunder` (30 mm an hour and a bolt every 2 s) | 7.67 | 21.58 | 0.355 | 0.71 |
| `plains/orbit-fog` (fog 0.9) | 6.58 | 24.02 | 0.274 | 0.0 (beside a clear row at 0.281) |

So at 1080p and render distance 32: clear weather and fog cost nothing measurable, rain about 0.5 ms a frame (the plan's budget), snow and a thunderstorm about 0.7 ms (over it). The plan's way down, if Ahmed wants it, is fewer layers (3) or drops at half resolution. Ahmed decides with these numbers.

