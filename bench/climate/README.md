# bench/climate

**Purpose:** what a climate tile costs (plan 004 phase 03's target: one cold tile on one worker under 100 ms), and the phase gate's pictures of the four climate layers.
**Tier:** none; benchmarks are not tiered.
**Owns:** the climate's region through its `climate_t`, and one heap region for the pictures, both released before it exits.
**Lifetime:** Process.
**Threads:** main, and 1 or 16 job workers.
**Public functions:** `main`.

- `tools/test-or-build.sh timeout 300 build/release/bin/bench_climate`: the timings. Exit status 0, 1 if any cold tile's best is over 100 ms, or 2 if it could not start.
- `tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --pictures /tmp/mc_c_climate 2198175988018515243`: `sea_distance.png`, `continentality.png`, `water.png` and `latitude.png` (latitude drawn over water), 1024 by 1024 pixels at 16 blocks a pixel, 16 km a side round (0, 0), with `display_png_encode` and the same `climate_layer_draw` the Atlas will use. It draws no terrain: it is not a second map tool. It also prints the shares of land, sea, large lake and small lake cells.

**May be called by:** people.
**Depends on:** `climate` and its `layer/`, `terrain`, `job`, `thread`, `heap`, `time`, `display/headless/png`.

Only a release build's numbers mean anything: debug is at `-O0`.

**What it measures:**

- **Cold tile, 1 worker:** a fresh climate each time (both tables emptied by `climate_open_ground`), so the tile's job builds all 289 mask pages of its window (73,984 estimates) and its 4,096 ground samples. 9 tiles in different places, best of 3 each. Prints ms, estimates, estimates a second and the share of the time in the ground function (`terrain_height_estimate_area`).
- **Warm tile, 1 worker:** the tile east of one just built, so 17 of its 289 pages are new: what walking costs.
- **Spawn precompute, 16 workers:** `climate_prepare_area` over render distance 32 plus 2 km round (0, 0), with the tile and page counts.

## Results

Appended as they are taken, newest first.
