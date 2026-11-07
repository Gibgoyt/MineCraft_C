# bench/climate/survey

**Purpose:** `bench_climate_survey`, plan 004 phase 11's four measurements of the climate over 8 seeds, from the climate functions alone (no chunks, no display, no card). For each seed it surveys a **strip** of the world: x from 0 to 16 384 blocks (one 16 km hurricane spawn square), and y over a whole latitude cycle between the poles, every 64-block cell whose centre lies within ±38 298 blocks: 256 by 1196 cells, about 1255 km².

1. **Climate class shares.** Every land cell of the strip (not sea, not a large lake) by its class (master plan 6.10), as a share of land; the mean, smallest and largest over the seeds. Beside it, the share of each class's cells whose surface biome (`terrain_biome_column_at` at the cell's centre) disagrees with the class's broad kind: a desert or badlands biome in a class that is not Desert or Steppe, or any other biome in Desert or Steppe, pooled over the seeds. No target (master plan 2.6).
2. **Hurricanes a season per warm coast.** A warm coast square is one of the strip's 16 km squares with a sea cell whose current is warm and which has a hurricane season. Over the years asked, every hurricane spawned in it (`climate_systems_square_spawns`, the game's own spawner), per square per season; per coast, a run of neighbouring warm squares; and those making landfall, a track sample over land while of hurricane strength (`CLIMATE_HURRICANE_STORM_VMAX`). Compared with 7.2's 2.4 a square and about 7 a three-square coast.
3. **Thunderstorm days a year by class.** Up to 200 land cells per class per seed, the smallest by a hash of the seed and the cell, each day of the years asked by `climate_weather_day`; days whose kind is Thunderstorm, divided by the years. A class with fewer cells on a seed takes them all and the table says so.
4. **Outlook calibration.** At 200 land places per seed, picked by the same hash, every census of the years asked (52 a year), the outlook's 7 days and every kind (rain, thunderstorms, storms, hurricanes, each named system), then what happened, by the rules of phase 07's calibration test (`tests/run/climate/outlook/`): the bench may read the future, the outlook may not. Chances binned in tenths: count, mean chance, share that happened and two standard errors; bins under 100 are reported, not judged. Then the 60 to 80% bin for each kind and all together, against the plan's bound of 60 to 80%.

It also prints the long-run rain at the thunderstorm cells against each cell's normal (7.4's 15% test, at cells rather than phase 06's boxes), and the climate's memory per km² (6.9's target, 32 KiB).

**Tier:** a bench: Owner of one heap region, released before it exits, and one `terrain_t` and one `climate_t` at a time. **Threads:** main, and the job system's workers.
**Public functions:** `main`. Run, on a release build:

    tools/test-or-build.sh timeout 7200 build/release/bin/bench_climate_survey [--first 0-7] [--seeds 1-8] [--years N] [--workers 1-16] [--width blocks] [--places N] [--per-class N] [--margin blocks] [--record bench/climate/survey/history.tsv]

The defaults are 8 seeds, 10 years, 16 workers, 16 384 blocks, 200 places, 200 cells a class and a 5120-block margin. The seeds are plan 003 phase 8 stage 1's eight, Ahmed's `2198175988018515243` first; `--first K --seeds N` takes N of them from the K-th (from 0), so a long survey can be run in parts. `--margin` is the tiles built round each band for the places: 5120 is past the 4200 blocks a thunderstorm cell can come from in a day, and 9216 printed the same bytes on 2 seeds (plan 004 phase 11, Results). **The tables go to stdout as Markdown, and are the same bytes on 1 worker or 16:** each row of cells, each place and each square-year is one job writing only its own counts, summed in a fixed order afterwards, and each band of tiles is built before its jobs read it. Times go to stderr. `--record` adds one line to a TSV (with a header if the file is new). Exit 0, or 1 if a cell's tile was not built (a bug) or the record could not be written, or 2 if it could not start.

**How it runs.** Phase A works the strip a band of 256 rows (16 km) at a time: `climate_prepare_area` builds the band and 9 km round it (inside the 64 km keep square), then one job a row reads every cell. Hurricanes need no tiles (their spawner and tracks read latitude and the coarse land mask), one job per warm square and year. Then the cells are put in hash order and picked, and phase C works each band again, its tiles rebuilt, with one job per place: the thunderstorm cells and the outlook places in it.

**May be called by:** Ahmed and the phase files, through the mutex. Not registered with ctest (`cmake/targets.cmake`).
**Depends on:** `climate/`, its `systems/`, `weather/` and `outlook/` parts, `terrain/`, `job/`, `heap/`, `time/`.

## Results

Recorded by plan 004 phase 11, in `plans/004-ahmed-bohemoth-climate-system-phase-11-tuning-and-cost.md`, **Results**, and as lines of `history.tsv` here.
