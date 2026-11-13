# bench/landform

**Purpose:** measures the shape of a seed's land as plan 007 section 4 defines it, and draws it (master 3.14): how steep it is and where, the shape of its peaks, its plateaus, escarpments, ranges and knobs, its height bands and sea. Plan 007 phase 01 ran it on today's land; phases 03 to 06 run it on the new. It needs no display and no card, so it is safe to run whenever.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers (about 900 MB reserved, touched as used), released before it exits; one `terrain_t` a seed; a `texture_t` and a `ui_t` for the font.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform [--self-test] [--size-km N] [--step N] [--strip | --no-strip] [--windows N] <directory> [seed...]`. Defaults: 64 km (65,536 blocks) a side round the world's middle, a column every 16 blocks (4096 a side), 64 windows, the strip on, the seed `TERRAIN_SEED_DEFAULT`. The step is a multiple of 4, at most 4096 columns a side, and the windows a square number up to 64. `--self-test` alone needs no directory. Exit status 0, 1 if a picture or the CSV could not be written or the self-test failed, 2 if it could not start.
**May be called by:** people, and ctest `run_terrain_landform_measure` (`--self-test`).
**Depends on:** `measure/` and `draw/` (compiled into this program), `terrain`, `terrain/climate_sample` (the strip's snowline, plan 006's fallback), `texture` and `ui` (the font), `display/headless/png`, `job`, `thread`, `heap`, `time`, `world/column`, `game` (`GAME_WORLD_MIDDLE`).

## What it writes

| File | What |
|---|---|
| `<seed>_relief_64km.png` | The map, 4096 by 4096 at 16 blocks a pixel: shaded relief, sea by depth, contours every 50 thin and every 500 thick. Crosses: red the tallest peak, brown the longest range piece's middle, yellow the largest plateau's middle, magenta a knob on a base of 250 or more, cyan the plains place. x goes right, y down |
| `<seed>_slope_64km.png` | The same map by slope class (below) |
| `<seed>_plateaus_64km.png` | Grey relief; plateau land yellow with a dark rim, its escarpment edge red; range pieces at 500 or over tan with a brown rim |
| `<seed>_profiles.png` | Four cross-sections at true scale, a block a pixel both ways, 4096 blocks long, a grid every 100: through the tallest peak along x and along y, across the largest plateau's edge (or, with none, through the plains place), and along the longest range piece's longest direction |
| `<seed>_strip.png` | x -2048 to 2040, y 0 (bottom) to 38,296 (top), 8 blocks a pixel: shaded relief, contours, latitude every 10°, and the snowline dashed in magenta where the ground crosses it (`draw/README.md`) |
| `<seed>_windows.png` | The 64 windows of real blocks, each 1024 blocks a side at 4 blocks a pixel, by slope class, in an 8 by 8 mosaic in the order they lie on the map |
| `<seed>.csv` | Every number of the table, a row each |

**Slope classes**, of the gradient `g` (rise over run; Ahmed's run:rise is `1/g : 1`):

| Class | g | Run:rise | Colour |
|---|---|---|---|
| Flat | < 0.05 | flatter than 20:1 | `0xE8F0D8` |
| Gentle | 0.05 to 0.2 | 20:1 to 5:1 | `0xA8D890` |
| Moderate | 0.2 to 0.5 | 5:1 to 2:1 | `0xF0E070` |
| Steep | 0.5 to 1 | 2:1 to 1:1 | `0xF0A040` |
| Very steep | 1 to 2 | 1:1 to 1:2 | `0xE06030` |
| Cliff | 2 to 5 | 1:2 to 1:5 | `0xB02020` |
| Sheer | > 5 | steeper than 1:5 | `0x500010` |

## How it knows

- **The profile** is the 2D height before the 3D noise, rivers and caves: `terrain_height_estimate_area`, bit for bit what the density's lattice gets. Land is where it is above 0. The map reads 5 values a column (the column, and 4 blocks either way in x and y), about 100 million a seed.
- **Real blocks** are what Ahmed walks on, from the same stages `bench_world_map` runs (no features, no ore), the top solid block with what grows looked through. Land is where no water lies on top. They are read only in the 64 windows, 258 by 258 columns each with their border.
- **The windows** lie one in the middle of each eighth of the map each way, 16-aligned. Their slope classes are counted both from the profile and from real blocks, over the same columns, 8 blocks across; the largest class difference is a row of the table.
- **The measures** themselves are in `measure/README.md`.
- **The cost** rows: on one worker, best of 3, `terrain_density_profile_points` on the 900 lattice columns of 36 chunks round the tallest peak (each chunk's 5 by 5, as a box asks; 20 calls a round, divided), against `terrain_generate_box_fallback` (every stage, with plan 006's fallback climate sample, which no stage reads yet) of those 36 whole columns. Then its columns a second on 16 workers, best of 3, at the tallest peak and at the plains place (land from 10 to 80 with the least relief in 2048 blocks). Not a change to `bench_terrain`, and not the same places, so not its numbers.

## The self-test

`--self-test` runs every measure but the windows on synthetic ground at 4 blocks a column, and prints what each check got:

| Check | Ground | Wants |
|---|---|---|
| `a_one_to_one_cone_is_all_steep_and_has_concavity_one` | a cone 600 high, radius 600, on a 4 km field at 0 | 95% of its flank (16 blocks off the apex and the rim) at g 1.00 ± 0.02; one peak; concavity 1.00 ± 0.05; W50/H 1.00 ± 0.05 |
| `a_power_two_and_a_half_peak_has_concavity_three` | `800 (1 - r/1500)^2.5` | one peak; concavity 3.13 ± 0.2; top decile's median slope over the bottom 3's at least 5 |
| `a_five_by_five_km_plateau_is_one_piece` | a square top at 400, 5120 a side, with a 1:2 edge down to a field at 20, 8 km a side | one piece of 25 km^2 or more and no other; 27.04 ± 1 km^2; 5.20 ± 0.032 km across; escarpment 20.8 ± 0.5 km |
| `a_plane_of_five_to_one_is_all_moderate` | `0.2 x + 0.1 y` | 100% moderate |
| `knobs_are_binned_by_their_base` | that plateau with ten domes of 40 on its top, 400 apart, and ten on its field, 700 apart | 20 knobs: 10 on a base under 150, 10 at 250 or over |
| `the_same_input_gives_the_same_table` | the domes, measured twice on 16 workers and once on 1 | the same bytes of table and CSV |

**The plateau's numbers differ from plan 007 phase 01's** (26.2 km^2, 5.12 km across, 20.5 km of escarpment). A plateau here takes its edge down to 80 below its top (`measure/README.md`), which on a 1:2 edge is 40 blocks more each side: 5.2 km across, 27.04 km^2, 20.8 km round. Measuring only the flat top exactly would need a rule that cuts any rim, which would also cut a real plateau's.

## Results

### 2026-11-13, plan 007 phase 03: plateaus, escarpments and long ranges

On the 8 seeds, in two runs of 4, exit 0: sea 24.7 to 27.1%; reaching 150, 300, 500, 750, 1000: 31.9 to 43.1, 23.7 to 37.2, 11.4 to 22.2, 3.3 to 7.7, 0.06 to 0.15%; top 1126 to 1150; escarpment 5.4 to 30.1 km; the longest range piece at 500 or more 14.0 to 39.8 km; plateau pieces of 25 km^2 still 0, as the tops carry today's hills (phase 04); slope classes as before within a point. The profile 0.38 to 0.43 µs a lattice column. The plan 007 phase 03 file has the rest and the pictures.

### 2026-11-12, plan 007 phase 01: today's land, before any landform change

Release build, through the build lock, plan 003's 8 seeds in two runs of 4 and Ahmed's seed `11107447384583428913` alone, exit status 0 each; 28 to 30 s a seed. The full table, the pictures looked at, and what they say are in `plans/007-ahmed-bohemoth-realistic-landforms-phase-01-measure-todays-terrain.md`, "Today". In short, min to max over the 8:

| Measure | Today |
|---|---|
| Flat / gentle / moderate / steep / very steep / cliff / sheer (profile, windows) | 0.8–1.3 / 7.8–11.6 / 20.7–26.7 / 24.6–26.8 / 21.9–26.7 / 11.0–15.6 / 0.8–1.9 % |
| The same, real blocks | 1.1–1.8 / 6.5–9.5 / 17.0–22.0 / 26.4–28.3 / 22.4–26.2 / 13.5–17.7 / 2.5–4.3 % |
| Profile against blocks, largest class difference | 3.7–4.7 points |
| Steeper than 1:2 in the top 30% of local relief | 10.9–11.5% |
| Top decile's median slope over the bottom 3's | 2.45–2.62 |
| Peaks; concavity, median; W50/H, median | 4678–5007; 1.39–1.44; 0.97–1.06 |
| Plateau pieces; escarpment | 0; 0 km |
| Longest range piece at 500; elongation | 3.9–6.0 km; 2.7–3.3 |
| Knobs; share on a base of 250 or more | 22879–26121; 12.6–15.2% |
| Sea; reaching 500; top (profile) | 25.7–27.4%; 4.58–5.23%; 1166–1181 |
| The profile, a lattice column; its share of a box | 0.32–0.37 µs; 0.08–0.10% |
| Columns a second on 16 workers, tallest place against plains | 843–944 against 1158–2092 |
