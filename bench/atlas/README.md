# bench/atlas

**Purpose:** what the Atlas (plan 004 phase 09) costs, against the phase file's budget: opening it over 100 km² of
generated land, `M` to the first complete picture, under 100 ms; each layer's whole picture at 1920 by 1080 on 16 workers
under 60 ms; the main thread's frame with the Atlas up under 2 ms; a column's write on the sim under 5 µs and its top on
a worker under 20 µs.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for the job system, a `ui_t`, a `texture_t`, a climate, a terrain and the pictures, with what
those reserve, all released before it exits; a temporary directory `/tmp/mc_bench_atlas_*` of tile files, removed at the end.
**Lifetime:** Process.
**Threads:** main and 16 workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas`, so nothing else
shares the CPU. Exit status 0 if it ran, 2 if it could not start. A target missed is printed as `MISSED` and does not change
the status, as `bench/world/README.md` explains.
**May be called by:** people.
**Depends on:** `ui` and `ui/atlas`, `climate`, `terrain`, `texture`, `job`, `thread`, `heap`, `time`, the generated
`tables/blocks`. No card, no display, no input.

What it does:

1. Works out the climate tiles round the view and 8192 blocks beyond (seed 2198175988018515243): temperature now's
   weather reads every tile within 6 km of each place, as the census keeps them built round explored land, and with
   them missing it would time its failures.
2. Writes 20 by 20 level-0 tiles of made-up columns (a smooth land from a few sines, 10 240 blocks a side, 105 km²) into
   the store, saving every 100 tiles so nothing is dropped, and closes it, so every tile and mip is on disk.
3. **Open:** five times, opens the world's store with nothing in memory (the files in the page cache), presses `M` at
   11.3 blocks a pixel (21.7 by 12.2 km on screen, level 3), and times main's frames until the first picture is finished.
4. **Layers:** each of the 26 layers at 1920 by 1080, 5.66 blocks a pixel, three times, on 16 workers in 16 bands, with
   the weather systems' day list built for the now layers inside the time. For each value layer it prints the share of
   pixels that got a value, so a layer timed with nothing to show is seen.
5. **Main's frame:** 200 frames of `ui_atlas_frame` and `ui_atlas_frame_draw` with a picture shown on temperature by day.
6. **Columns:** 20 000 column writes, five rounds; 2000 column tops, five rounds, of each of three columns (plan 007
   phase 02, when the top began measuring cover depth): the real terrain's kind (stone to 60, dirt, grass at 64, a
   flower); the same under an oak canopy on every pixel (leaves 70 to 73, a log at the middle), the forest case; and
   leaves from the floor to 64, where every pixel walks the whole 31 layers of cover, the worst case. Before plan 007
   phase 02 the one column timed was the last of these by mistake: it filled with block 1, `acacia_leaves`, while its
   comment said stone, which cost nothing extra until leaves became cover.

## Results

### 2026-11-12, plan 007 phase 02: contour lines and cover depth

Release build, through the build lock, exit status 0, on the code of plan 007 phase 02 after its column-top table (one
look-up a voxel instead of a shape switch and a flag). The machine was busy (load average 82 falling to 21 over the run:
other agents' work outside the lock), so every number here is some 10% above a quiet run's.

| | Median (slowest) | Target |
|---|---|---|
| Opening over 100 km², M to the first picture | 23.4 ms (24.7) | 100 ms, met |
| `terrain`, now with contour lines | 23.8 ms (41.5) | 60 ms, met |
| `terrain_plain`, the old `terrain` | 11.1 ms (11.6) | 60 ms, met |
| Worst layer | `monsoon` 41.7 ms; the slowest single picture `temperature_now` 74.9 ms | 60 ms by the median, met |
| Main's frame with the Atlas up | 0.725 ms (1.199) | 2 ms, met |
| Column write (sim) | 0.097 us | 5 us, met |
| Column top, the real terrain's kind | 15.4 us (15.6) | 20 us, met |
| Column top, forest (an oak canopy on every pixel) | 17.2 us (17.4) | 20 us, met |
| Column top, 320 of leaves (every pixel walks 31 of cover) | 29.3 us (35.7) | 20 us, **MISSED** |

The contours add about 12 ms to the terrain picture (the ground's band of two intervals against four neighbours, each
pixel). The worst-case column costs 31 more looks for each of its 256 pixels; in two earlier runs that day it was
31.5 us before the table (load average 3 when it started) and 26.5 us after. No real column is leaves to the floor: a forest's cover is a few layers of
leaves over air, which the not-air bitmask passes over, and it meets the target.

### 2026-11-08, plan 004 phase 11, second pass: from a cool machine

From a cool machine (plan 004 phase 11, second pass): through the build lock, after 5 minutes with nothing heavy, load average 1.2 at the start, on the code of `ffc94238`, exit status 0. Opening over 100 km², median **16.1 ms** (slowest 17.0; target 100, met). Worst layer by median `temperature_coldest` **35.6 ms** (`monsoon` 35.5; the slowest single picture `temperature_now` 59.2 ms; target 60, met). Main's frame median 0.680 ms (slowest 1.022; target 2, met). Column write 0.081 us, column top 12.8 us (targets 5 and 20, met).

### 2026-11-07, plan 004 phase 11: every target met

Release build, through the build lock, twice in a row, exit status 0 both.

| | First run | Second run | Target |
|---|---|---|---|
| Opening over 100 km², M to the first picture, median (slowest) | 16.6 ms (18.1) | 16.8 ms (26.1) | 100 ms, met |
| A full picture, worst layer, median (slowest) | 36.5 ms (39.5), `monsoon` | 44.1 ms (56.6), `temperature_day` | 60 ms, met |
| Main's frame with the Atlas up, median (slowest) | 0.575 ms (1.028) | 0.713 ms (0.913) | 2 ms, met |
| Column write (sim), median | 0.079 us | 0.086 us | 5 us, met |
| Column top (worker), median | 13.0 us | 14.0 us | 20 us, met |
