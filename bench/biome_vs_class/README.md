# bench/biome_vs_class

**Purpose:** `bench_biome_vs_class`, plan 006 phase 03's survey and pictures of biomes against the climate. Biomes come from the
climate family first (plan 006 master 2.4), and this measures how well that holds, on real climate from the tiles, with no
chunks, no display and no card.

1. **The survey.** For each seed, a strip from x = 0 to `--width` blocks (16 384 by default, bench_climate_survey's) and pole
   to pole: every 64-block cell whose centre lies between the poles. A land cell (not sea, not a large lake) has its climate
   class (`climate_cell_get`), and the biome the terrain gives the column at its centre (`terrain_biome_column_at` with the
   column's own climate sample, `climate_terrain_sample`). Per class, pooled over the seeds:
   - **Plan 004 disagree**, plan 004 phase 11's measure: a desert or badlands biome in a class that is not Desert or
     Steppe, or another biome in Desert or Steppe. Printed so the "before" stays comparable.
   - **Plan 006 disagree**, the target: the biome is not a member of the family the class names (`config/biomes/`'s
     `families`). Under 5% in every class that has land.
   - The worst single seed of plan 006's measure, and how often the column's own family (worked out at its own height,
     through the warp and the blend) is not the class's: the room the membership has to cover.
2. **The pictures**, for the first `--picture-seeds` seeds (3), with `--pictures <directory>`, each three panels side by side,
   north at the top, 16 blocks a pixel: the biomes (bench_biome_map's colours, lit from the north-west), the climate classes
   (plan 004 phase 05's colours from `ui/atlas/layer`, sea dark blue), and the mismatch (red where plan 006's measure
   disagrees, grey where it agrees, dark blue water).
   - `<seed>_square.png`: 16 km round the equator, x 0 to 16 384, y -8192 to 8192 (3072 by 1024).
   - `<seed>_strip.png`: x 0 to 4096, from the equator to the north pole (768 by 2393).

**Tier:** a bench: Owner of one heap region, released before it exits, and one `terrain_t` and one `climate_t` at a time.
**Threads:** main, and the job system's workers: one job a row.
**Public functions:** `main`. Run, on a release build:

    tools/test-or-build.sh timeout 7200 build/release/bin/bench_biome_vs_class [--first 0-7] [--seeds 1-8] [--workers 1-16] [--width blocks] [--pictures directory] [--picture-seeds N]

The seeds are bench_climate_survey's eight, Ahmed's `2198175988018515243` first. The table goes to stdout as Markdown and is
the same bytes on 1 worker or 16: each row is one job writing only its own places, counted in order afterwards. Times go to
stderr. Exit 0 when plan 006's measure is under 5% in every class and every tile was built, 1 otherwise, 2 if it could not
start.

**How it runs.** Each area is worked a band of 16 km of rows at a time: `climate_prepare_area` builds the band and 1 km round
it (inside the 64 km keep square), then one job a row reads every place.

**May be called by:** Ahmed and the phase files, through the mutex. Not registered with ctest.
**Depends on:** `climate/`, `terrain/`, `display/headless/png`, `job/`, `heap/`, `time/`, and the generated `tables/biomes/`.

## Results

Recorded by plan 006 phase 03 in its phase file, Issues found.
