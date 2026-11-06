# bench/climate

**Purpose:** what a climate tile costs (plan 004 phase 03's target, which phase 04 keeps: one cold tile on one worker under 100 ms), and the phase gates' pictures of the climate layers.
**Tier:** none; benchmarks are not tiered.
**Owns:** the climate's region through its `climate_t`, and one heap region for the pictures, both released before it exits.
**Lifetime:** Process.
**Threads:** main, and 1 or 16 job workers.
**Public functions:** `main`.

- `tools/test-or-build.sh timeout 300 build/release/bin/bench_climate`: the timings. Exit status 0, 1 if any cold tile's best is over 100 ms, or 2 if it could not start. From phase 05 it also builds each tile straight from its window on one thread and prints each part's share: the window (phase 03's pages), the cells, air (phase 04), heat, rain and season (phase 05), and the snow balance's bisection on its own.
- `tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --pictures /tmp/mc_c_climate 2198175988018515243`: `sea_distance.png`, `continentality.png`, `water.png` and `latitude.png` (latitude drawn over water), 1024 by 1024 pixels at 16 blocks a pixel, 16 km a side round (0, 0), with `display_png_encode` and the same `climate_layer_draw` the Atlas will use. It draws no terrain: it is not a second map tool. It also prints the shares of land, sea, large lake and small lake cells. From phase 04 also `currents.png` (with the latitude lines over it), and `pressure_dayDDD.png` and `wind_dayDDD.png` for days 15, 105, 196 and 288, the same 16 km. From phase 05 also `temperature_mean.png`, `temperature_warmest.png`, `temperature_coldest.png`, `rain_year.png`, `climate_class.png`, `snow_balance.png`, `monsoon.png` (on the first day listed), and for each day of `--days d,d,...` (default one a month: 15, 46, 76, 107, 137, 168, 198, 229, 259, 290, 320, 351) `temperature_day_dDDD.png`, `rain_day_dDDD.png`, `hurricane_season_dDDD.png` and `monsoon_dDDD.png`: stepping through a folder of them is the gate's day slider.
- `tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --synthetic /tmp/mc_c_climate_synthetic`: phase 04's two synthetic continents, 4 km squares of land at 30° S (`south30_*.png`) and 50° N (`north50_*.png`), each drawn 8 km a side at 8 blocks a pixel: currents, and pressure and wind on the four days. They show the master plan's table with no real coast's noise. From phase 05 also the South Africa sketch at 25° S (`south_africa_*.png`), phase 05's layers 16 km round its middle at 16 blocks a pixel; `--days` sets its day layers' days.

**May be called by:** people.
**Depends on:** `climate` and its `layer/` and `air/`, `terrain`, `job`, `thread`, `heap`, `time`, `display/headless/png`.

Only a release build's numbers mean anything: debug is at `-O0`.

**What it measures:**

- **Cold tile, 1 worker:** a fresh climate each time (both tables emptied by `climate_open_ground`), so the tile's job builds all 289 mask pages of its window (73,984 estimates) and its 4,096 ground samples. 9 tiles in different places, best of 3 each. Prints ms, estimates, estimates a second and the share of the time in the ground function (`terrain_height_estimate_area`).
- **Warm tile, 1 worker:** the tile east of one just built, so 17 of its 289 pages are new: what walking costs.
- **Spawn precompute, 16 workers:** `climate_prepare_area` over render distance 32 plus 2 km round (0, 0), with the tile and page counts.

## Results

Appended as they are taken, newest first.

### 2026-11-06, plan 004 phase 05: temperature, rain and seasons cost about 2.3 ms a tile

Release build, through the build lock, seed `0x1234567890ABCDEF`. Exit status 0.

| Measure | Result |
|---|---|
| Cold tile, 1 worker, 9 places, best of 3 | 29.5 to 30.8 ms (target 100). 83 to 87% of the time is the ground function |
| Warm tile, 1 worker (17 new pages) | 7.91 ms at the slowest place |
| Spawn precompute, 16 workers, render distance 32 plus 2 km | 36 tiles and 484 mask pages in 36.8 ms |

A tile's parts, built straight from its window on one thread, mean over the 9 places: window 24.25 ms (80.1%), cells 3.63 ms (12.0%), air 0.10 ms (0.3%), heat 0.002 ms, rain 0.51 ms (1.7%; the walk reads no mask page past the window, as every step is inside it), season 1.78 ms (5.9%), of which the snow balance 1.07 ms (3.5%); 30.3 ms in all.

`--pictures /tmp/mc_c_climate 2198175988018515243 --days 15,46,76,107,137,168,198,229,259,290,320,351`: 68 pictures, the 16 km worked out in 273 ms. `--synthetic /tmp/mc_c_climate_synthetic --days 15,107,198,290`: 41 pictures.

### 2026-11-06, plan 004 phase 03: the target is met with room to spare

Release build, through the build lock, seed `0x1234567890ABCDEF`, after the sea-floor rule. Exit status 0.

| Measure | Result |
|---|---|
| Cold tile, 1 worker, 9 places, best of 3 | 28.8 to 30.7 ms (target 100). 78,080 estimates each, about 2.6 million a second; 91% of the time is the ground function |
| Warm tile, 1 worker (17 new pages) | 5.65 ms at the slowest place |
| Spawn precompute, 16 workers, render distance 32 plus 2 km | 36 tiles and 484 mask pages, 271,360 estimates, in 23.8 ms |

`--pictures` over 16 km round (0, 0), cells: seed 2198175988018515243, 70.5% land, 28.8% sea, 0.0% large lake, 0.8% small lake (155.9 ms to work out); seed 1311768467294899695 (`0x1234567890ABCDEF`), 72.6%, 26.6%, 0.0%, 0.8%; seed 5569200986553504070 (the default), 68.9%, 30.4%, 0.0%, 0.8%.

Before the sea-floor rule (the plan's window-edge rule), the same three seeds gave 0.0%, 0.4% and 0.0% sea and 18.8% to 22.2% large lake, and continentality stepped in straight lines at tile borders: the reason for the change.
