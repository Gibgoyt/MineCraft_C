# bench/world/slope

**Purpose:** plan 009 phase 01: where the steps of a picture go, at Ahmed's spot of `~/Pictures/5.png` (29.44 ms on his card: primary 10.41, shade 14.62). His view is a settled row, `ahmed_slope/night-64`, and with `--steps` every ray kind's steps come back by distance band, as a table and as heatmaps. Experiments are the same row with one thing changed by a flag.
**Tier:** bench: a program on its own, the Root of its own memory, as `bench/world` is.
**Owns:** one heap region per row, for an `app_t`, its samples, the picture, its PNG, the counts read back and the heatmap; and while it counts, one block of the card's frame pool for the counts (`traverse_counts_bytes` of the picture), given back before the world closes.
**Lifetime:** Process. Each row has a process of its own.
**Threads:** the parent, with no threads and no card. In a row's process: main and the job workers; it is settled, so the card is driven from main.
**Public functions:** none; a program, `bench_world_slope`.
**May be called by:** a person, through the build lock, with Ahmed's game closed (`pgrep -af 'build/release/bin/minecraft_c'` prints nothing).
**Depends on:** `main/index.h` for `app_t`, `game` and `game/watch` (the frame's marks, which carry the counts block to the frame), `render`, `gpu_bridge` (`traverse_counts_t`), `climate/sun` and `calendar` (the sun from the clock), `world`, `terrain`, `job`, `thread`, `ui`, `texture`, `math`, `simd`, `display/headless/png`, `file`, `heap`, `time`, `log`.

It is its own program and not rows of `bench_world`, as that file is at the standard's 1500 lines (as `bench_world_shake` is, for the same reason). Its history is `history.tsv` beside this file.

## Running it

    pgrep -af 'build/release/bin/minecraft_c'      # must print nothing: Ahmed plays on this card
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world_slope --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/p01/night64.tsv --shots /tmp/mc_c_009/p01
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world_slope --only =night-64 --features-off 20 --record bench/world/slope/history.tsv --label "shadows off"

One row, one experiment, one wrapper call: plan 005 phase 8 saw a whole run slow the card for the rows after.

## The rows

The spot is `--at -385 347 2` (F3's feet block of 5.png; the eye 1.62 above it), the seed `saves/test_7/world.meta`'s, `14148492376765118422` (that world's save was written while 5.png was taken, at 14:51 on 2026-11-12), the view `--look 8 34` (yaw 8, 0 east and 90 north; pitch 34 up), fitted by eye to 5.png over five shots (the peak with red flowers, the striped slope, the shadowed hollow and the tree at the right fall where they do in 5.png), the date day 92 of year 1 as on 5.png, 1920 x 1080, field of view 70, full detail 32, every effect of play but god rays: his settings. The sun is put where the rows say by the world clock on that day at the eye's latitude, so its azimuth is the clock's: the evening for one below the horizon, the morning for one above. The day does not go on.

| Row | Render distance | Sun |
|---|---|---|
| `ahmed_slope/night-64` | 64 | -13.1 (5.png: the light is the moon, 13.1 above the other horizon) |
| `ahmed_slope/night-32` | 32 | -13.1 |
| `ahmed_slope/day-64` | 64 | +40 |

Each row settles the world, draws 30 warm pictures and 240 measured ones, with the card's clock read before each measured one (so a row says its card time in ms and in Mcycles, ms x MHz / 1000), and the last measured picture is its `picture` hash and shot. A row is not sound, and the exit status says so, with fewer than 8 colours on a grid of 64 by 36.

## Flags

- `--only <text>`: the rows whose name holds this, or `=<name>,...` for just those, with or without `ahmed_slope/`.
- `--at <x> <y> <z>`, `--seed <n>`: another spot or world.
- `--look <yaw> <pitch>`: degrees, the game's convention (`run/src/render/frame/README.md`).
- `--sun <degrees>`: the sun's elevation in place of the row's.
- `--full-detail <chunks>`: full detail in place of his 32 (the table is sized for it).
- `--render-scale <percent>`: drawn at that share of 1920 x 1080 and brought up, as the setting does.
- `--lod-reference`: full detail out to the render distance and no far field: every block everywhere, the picture phase 04 compares against.
- `--features-off <hex>`: take these `SHADE_FEATURE_*` bits away: shadows `20`, alpha `2`, AO `400`, far field `8000`.
- `--steps <file.tsv>`: with one row. After the measured pictures, 8 more at the same place (two of AO's four-frame turns) with a counts block on the card, which the counted copies of the ray kernels fill; the measured times never see it. Then:
  - `<file.tsv>`: per kind (primary, far, sun, ao, alpha, far_past, far_sun, reflect) and band (0-16, 16-64, 64-128, 128-256, 256-512, 512-1024, 1024+ blocks, and "all"), the calls with a step in the band and their steps, each a frame, the mean, and the p50 and p99 of a call's steps in it (from buckets a quarter of an octave wide, so to within a quarter); the far kinds again by level. A step's band is where along its own ray it began: a primary, far, alpha or far_past ray's from the eye, a sun, AO or reflection ray's from its surface. A call is one traversal: a sun ray through three leaves is four.
  - `<file.tsv>.png`: primary steps a pixel a frame, on a log scale from 0 to 512 (dark blue, blue, cyan, yellow, red, white), with the scale under the picture, ticks at 0, 1, 4, 16, 64, 256 and 512. `.far.png` the far field (far rays, its sun rays and the ask past full detail), `.sun.png` sun rays, `.shade.png` AO, alpha holes and reflections, `.all.png` the sum.
- `--record <file>`: add a line per row. `bench/world/slope/history.tsv` is the repository's.
- `--label <text>`, `--shots <dir>`, `--list`.

## Results

See the plan 009 phase 01 file's Results, which these rows filled.
