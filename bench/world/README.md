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

`bench_world --list` prints them. Those at render distance 32 are held to the 120 fps, and `night/walk-mobs` to the tick as well. `plains/orbit-godrays` has the one effect the game leaves off by default, since the target says all of them.

A row is not sound, and the exit status says so, if the last picture has fewer than 8 colours on a grid of 64 by 36 (an eye in a wall is very fast), if anything called the heap while it was timed, or, on the walk, if the minds did no searches, the store fell under 100 or the player died.

## Flags

- `--only <text>`: the rows whose name holds this.
- `--record <file>`: add a line per row to a history. **`bench/world/history.tsv` is the repository's.** New columns go on the end of its first line, and old lines stay.
- `--label <text>`: says what was measured, in the history, such as the commit.
- `--shots <dir>`: the last picture of each row as a PNG, to see what a row is looking at.
- `--trace <file>`: with an `--only` that picks one row, its Chrome trace, and the profiler's table on stdout. Not in a release build.
- `--list`: print the rows, and do nothing.

To add to the history after a change that may move these numbers:

    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --record bench/world/history.tsv --label "<commit>: <what changed>"

## Reading these numbers

Compare a row's card numbers, with its `picture` the same. On this shared machine, with the same pictures, the card's numbers moved up to about 10% between runs. **What runs on the CPU's main thread moved much more:** the `tick` and `past the card` of `night/walk-mobs` come out in two groups, as in the table, with no cause found: see the stage file's issue 1. So take the tick from several runs, and not from one.

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

Picture hashes, which should be the same on a later run unless the pictures were meant to change: `plains/orbit` 80C74A8FF0FD45DA, `plains/flight` 6791AAFFA070C066, `plains/orbit-godrays` B76C95FBB42A4EF5, `jungle/orbit` 7DDE85AE81B7E425, `jagged_peaks/orbit` D72C5736B759759F, `deep_ocean/orbit` 6CE16CAB894553F4, `cave/orbit` 3825C2ED4113BFFD, `night/walk-mobs` FE63E7196A02774C, `plains/orbit-r512` 17F0F18FC001E5DF.
