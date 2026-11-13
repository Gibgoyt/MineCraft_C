# bench/world_map

**Purpose:** shows what a seed's world really is: a map of about 16 km by 16 km, side-on slices that show caves, and a table of how much is sea, how high the land goes, where the ranges and the caves are, and where a body starts. It needs no display and no card, so it is safe to run whenever. Plan 003 phase 8 tunes the world from this.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits, and one `terrain_t` a seed.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map [--map-only] <directory> [seed...]`. `--map-only` is for tuning: about 18 s a seed, the map and its table with no slices and no cave tiles, so those columns of the table read 0. The default seed is `TERRAIN_SEED_DEFAULT`. A seed is decimal, or hex with `0x`. Exit status 0, 1 if a picture could not be written or a column's check differed, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `terrain`, `game` for its spawn search, `display/headless/png`, `job`, `thread`, `heap`, `time`, `world/column`, and the generated `tables/blocks/` and `tables/biomes/`.

## What it writes

| File | What |
|---|---|
| `<seed>_map.png` | 2048 by 2048, a column every 8 blocks, 16.4 km a side, round the world's middle. x goes right and y goes down |
| `<seed>_slice_spawn.png` | Side-on, 2048 blocks wide through where a body starts, at a voxel a pixel, from the bottom of the world to a little over the highest ground it crosses |
| `<seed>_slice_mountain.png` | The same, through the nearest high ground. Left out if the map has none |

**This is not the game's renderer.** Colours are this program's, typed by hand. For the world as the game's Atlas shows it, every layer, see `atlas/` (`bench_world_map_atlas`, plan 004 phase 09).

**On the map:** land by height, shaded by slope, with a dark line where a height band ends, at 150, 300, 500, 750 and 1000. Green up to 60, yellow at 150, orange at 300, brown at 500, grey at 750, near white at 1000 and over. Sea goes from blue to dark navy with depth, down to 100. Sea ice is pale cyan with a darker hatch. Cyan is a river or a lake. A faint grid every 2048 blocks, and two darker lines through the middle. The magenta cross is where a body starts, and the red one the nearest pixel of high ground (see the table).

**On a slice:** the sky is pale blue. A cave's air is near black, and what a cave holds in water is dark blue, so a cave stands out from the sea. Rock is grey, deep rock dark grey, and anything else in it, such as ore, is pink-brown. Magenta ticks on the left: a short one every 50 blocks and a long one every 250. The dotted white line is the sea.

## How it knows

**A pixel of the map is that column's real blocks, from the stages themselves,** not a sum over the noise. So a bug in hydrology, surface or caves shows here. Each column asks only for a band round where `terrain_hydrology_columns_area` says its ground is, widened if the band's top is ground or there is no ground in it, since a voxel is the same in any box.

**The feature stage is left out of the map, and so is ore.** `terrain_feature_fill_box` works out a chunk and those round it for every box, and with it one seed's map did not finish in 590 s. Without it the map takes about 18 s. The one thing that stage does to a whole sea, its ice, is put in from the biome's own `ice` rate. Trees and plants are looked through either way. So the map does not show ponds, frozen ponds, icebergs or ice spikes.

**One column in 4099 is held to `terrain_generate_box` itself,** every stage, 1536 layers: the same class, the same ice, and the ground within 1, which is the snow. A pond or the like in the real column is counted as left out, and anything else is printed and makes the exit status 1. The first line of a seed's output says how many of each. On 8 seeds it was 2 to 8 left out of 1024, and 0 differed.

**The table:**

| Row | What it counts |
|---|---|
| sea | Water or ice on top at the sea's level, and not a river or a lake |
| ice | Ice on top of water, of any kind. Part of the sea or the inland water. Ice on rock, as a cold peak has, is land |
| inland | Rivers and lakes, and any water with its top not at the sea's level |
| joined, dry | Is it one world or islands? The largest connected piece of all that is not sea, as a share of all that is not sea. Then the same of dry land alone, which a river or a lake cuts |
| 0..60 | Dry land from 0 to 60 |
| `>=150` to `>=1000` | Dry land at least that high. What grows on it does not count. These are shares of the whole map, not of its land |
| top | The highest dry ground on the map |
| typical, far10% | From dry land to the nearest piece of high ground: the median, and what 90% of the land is within, in blocks, rounded up to 64, by two sweeps that read up to 8% long. -1 if the map has none |
| seas, largest | Connected seas of at least 1 km^2, and the largest, in km^2. A seed's own lines say how far across, and how many of the map's sides it reaches |
| high, big, largest, across | Connected dry land at 150 or more: pieces of at least 0.1 km^2, those of at least 1 km^2, the largest in km^2, and how far across that one is, in km. A real range is a big piece that goes on for kilometres |
| steep | Share of the land rising 3 blocks or more for 1 across, over the map's step of 8 |
| floating/km2 | Pieces of rock per km^2 of land in the tiles below, joined by faces to neither the bedrock nor a side of the tile. Ice, snow, plants and trees are not rock. A seed's own lines give the voxels and the largest |
| openings/km2, deep/km2 | See below |
| to land, to high | Straight from where a body starts, to dry land and to a piece of high ground, in blocks, to the map's 8. -1 for none on the map |
| spawn on | The block the body stands on, from `game_spawn_find`, the game's own search |

**Openings:** 64 tiles of 256 by 256 blocks, one in the middle of each eighth of the map each way, about 4.2 km^2 of whole chunks with every stage. A column is a mouth if, down from the sky, through air and what grows, it passes at least 4 voxels a cave cut. Touching mouths are one. `openings/km2` counts those going at least 8 deep, per km^2 of dry land in the tiles, and `deep/km2` those going at least 20. A mouth cut by a tile's edge may count twice, which is small at this size.

**Caves' share of the rock:** for each band of height, what caves cut as a share of what they cut plus the rock left.

**Ore (plan 005 phase 11):** two lines a seed from the same 64 tiles, every stage, caves and all. `ore veins a chunk` is, for each row of `config/ores/`, the veins it starts in a chunk and keeps in the world, from `terrain_ore_chunk_veins`, by the draws the ore stage makes; a mountain-only row such as `emerald` counts only where its chunk is high enough. `ore voxels a chunk` is each ore's blocks, both forms, found in the tiles, and the share in its deepslate form: fewer than veins times size, since caves, water and air take their share and an ore replaces no other ore.

## Results

### 2026-11-13, plan 007 phase 04: floating rock on the 8 seeds

Release, full (not `--map-only`), two runs of 4, exit 0, 0 columns differing. Floating rock per km^2: 2198175988018515243 1911, 1 1464, 42 2098, 20261104 1257, 12636146071196537873 1104, 5569068473609503046 1380, 9999999999999 1630, 18446744073709551557 931, against plan 007 phase 01's 1072 to 1383 (+25%: 1729): over on the two seeds with the most land at 300 or more (51% and 56%), where the plateaus' soft faces are. See the plan 007 phase 04 file, Issues found 9.

### 2026-11-13, plan 007 phase 03: plateaus, `--map-only`, seed 2198175988018515243

Exit 0. Sea 27.2%, joined 98.0%, dry 96.2%; reaching 150, 300, 500, 750, 1000: 56.4, 52.4, 34.4, 12.3, 0.25% (this map's middle stands on a plateau); top 1155; 17 high pieces, 8 big, the largest 86.7 km^2 and 16.4 km across; steep 13.6%; the spawn on snow_block. Floating rock was not measured (`--map-only`).

### 2026-11-04, before any of plan 003 phase 8's tuning

Release build, 8 seeds, exit status 0. About 50 s a seed: 18 s for the map, 26 s for the cave tiles.

| Seed | Sea | Ice | Inland | 0..60 | >=150 | >=300 | >=500 | Top | Typical | Big high | Largest high | Steep | Openings | Deep | To land | Spawn on |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2198175988018515243 | 45.7% | 21.2% | 5.6% | 26.9% | 6.03% | 0.86% | 0 | 467 | 768 | 0 | 0.6 | 19.5% | 48.25 | 3.71 | 376 | ice |
| 12636146071196537873 | 47.5% | 16.7% | 5.8% | 30.2% | 4.26% | 0.53% | 0 | 459 | 1408 | 0 | 0.3 | 15.8% | 36.46 | 6.08 | 8 | ice |
| 5569068473609503046 (the default) | 49.9% | 15.8% | 5.4% | 27.7% | 4.39% | 0.58% | 0 | 461 | 832 | 0 | 0.3 | 16.8% | 52.42 | 4.72 | 0 | grass_block |
| 1 | 48.4% | 14.6% | 5.3% | 28.2% | 4.60% | 0.61% | 0 | 452 | 896 | 0 | 0.4 | 16.7% | 56.38 | 2.71 | 0 | sand |
| 42 | 47.7% | 16.3% | 5.6% | 30.0% | 4.04% | 0.50% | 0 | 461 | 1856 | 0 | 0.3 | 15.0% | 49.91 | 6.55 | 11 | ice |
| 20261104 | 51.2% | 13.9% | 5.1% | 26.7% | 4.75% | 0.62% | 0 | 456 | 768 | 0 | 0.4 | 17.8% | 47.23 | 4.66 | 0 | sand |
| 9999999999999 | 44.9% | 9.9% | 5.9% | 31.3% | 4.62% | 0.64% | 0 | 472 | 1088 | 0 | 0.5 | 15.9% | 41.21 | 6.45 | 0 | sand |
| 18446744073709551557 | 50.8% | 12.9% | 5.1% | 28.3% | 4.26% | 0.54% | 0 | 474 | 896 | 0 | 0.4 | 15.9% | 41.22 | 4.31 | 8 | ice |

### 2026-11-04, after plan 003 phase 8 stage 2, starting on land

Same 8 seeds and build type, exit status 0. Terrain did not change, and every column of the table but the last two, and `to high` which follows them, is as above. No seed starts on ice now, and every one has 0 to land.

| Seed | Spawn at x, y, z | Spawn on | To land | To high |
|---|---|---|---|---|
| 2198175988018515243 | -280, -280, 5 | snow_block | 0 | 1033 |
| 12636146071196537873 | 8, -8, 38 | snow_block | 0 | 2171 |
| 5569068473609503046 (the default) | -8, -8, 84 | grass_block | 0 | 211 |
| 1 | 216, 216, 1 | sand | 0 | 1666 |
| 42 | 24, 8, 35 | snow_block | 0 | 40 |
| 20261104 | 200, 200, 6 | sand | 0 | 688 |
| 9999999999999 | -344, -344, 6 | sand | 0 | 442 |
| 18446744073709551557 | -8, 8, 34 | snow_block | 0 | 895 |

### 2026-11-04, after plan 003 phase 8 stage 3, continents

Same 8 seeds and build type, exit status 0, 0 columns differing. Height bands barely move, as stage 4 owns mountains. `Joined` and `dry` are the new columns.

| Seed | Sea | Ice | Inland | Joined | Dry | 0..60 | >=150 | Seas of 1 km^2 | Largest sea, km^2 | Reaches of 4 sides | Openings | Spawn at x, y, z | Spawn on |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2198175988018515243 | 29.9% | 15.6% | 2.8% | 98.1% | 95.0% | 44.0% | 5.66% | 10 | 26.2 | 1 | 47.43 | -184, -40, 14 | snow_block |
| 12636146071196537873 | 26.8% | 8.7% | 2.9% | 99.2% | 94.9% | 45.9% | 5.72% | 18 | 14.1 | 1 | 33.64 | 8, 8, 31 | snow_block |
| 5569068473609503046 (the default) | 30.6% | 9.3% | 2.9% | 99.1% | 86.1% | 44.4% | 5.72% | 11 | 23.6 | 1 | 48.00 | -8, -8, 98 | grass_block |
| 1 | 29.6% | 7.7% | 2.8% | 98.6% | 97.3% | 44.6% | 5.58% | 13 | 24.2 | 0 | 42.55 | -40, 56, 1 | sand |
| 42 | 29.1% | 10.7% | 3.0% | 97.8% | 96.7% | 46.0% | 5.40% | 15 | 22.4 | 2 | 36.60 | -8, -8, 32 | snow_block |
| 20261104 | 24.3% | 5.6% | 2.6% | 96.5% | 93.7% | 44.6% | 7.69% | 18 | 8.9 | 2 | 47.80 | 184, 152, 5 | sand |
| 9999999999999 | 23.8% | 5.1% | 2.8% | 99.4% | 98.0% | 44.8% | 7.18% | 12 | 8.1 | 1 | 44.18 | 8, 8, 42 | coarse_dirt |
| 18446744073709551557 | 27.5% | 8.2% | 3.0% | 99.1% | 97.7% | 45.3% | 5.97% | 15 | 14.3 | 0 | 50.34 | -8, 8, 44 | snow_block |

### 2026-11-04, stage 3's last tuning: the first octave's weight 0.3, and the coast at about -0.46

Same 8 seeds and build type, exit status 0, 0 columns differing. All 8 are now inside the sea target, with a mean of 28.2%. 8 other seeds, on `--map-only`, read 21.0 to 28.1%, and 3 of those are under 25%: 21.0 on seed 0, 24.9 on 1234 and 24.7 on 271828.

| Seed | Sea | Ice | Inland | Joined | Dry | 0..60 | >=150 | Seas of 1 km^2 | Largest sea, km^2 | Reaches of 4 sides | Spawn at x, y, z | Spawn on |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2198175988018515243 | 29.9% | 14.4% | 2.6% | 96.5% | 95.6% | 43.5% | 5.89% | 10 | 22.0 | 1 | -184, -40, 10 | snow_block |
| 12636146071196537873 | 28.0% | 9.2% | 2.7% | 96.3% | 95.3% | 44.4% | 5.93% | 18 | 13.2 | 1 | 8, 8, 32 | snow_block |
| 5569068473609503046 (the default) | 29.5% | 9.2% | 2.7% | 99.4% | 97.0% | 44.2% | 6.03% | 18 | 11.8 | 1 | -8, -8, 97 | grass_block |
| 1 | 29.4% | 7.5% | 2.7% | 98.8% | 97.7% | 44.4% | 5.70% | 16 | 9.6 | 0 | 8, 40, 7 | sand |
| 42 | 28.9% | 10.8% | 2.8% | 98.9% | 96.2% | 45.8% | 5.54% | 17 | 11.1 | 0 | -8, -8, 31 | snow_block |
| 20261104 | 25.6% | 6.2% | 2.5% | 99.7% | 95.3% | 43.5% | 7.68% | 21 | 7.5 | 1 | 168, 184, 4 | sand |
| 9999999999999 | 26.3% | 5.3% | 2.6% | 99.6% | 98.6% | 43.0% | 7.03% | 18 | 6.1 | 1 | 8, 8, 50 | stone |
| 18446744073709551557 | 28.1% | 7.7% | 2.8% | 99.5% | 98.4% | 44.4% | 6.08% | 16 | 11.9 | 0 | 8, 8, 24 | snow_block |

### 2026-11-04, after plan 003 phase 8 stage 4, mountains and valleys

Same 8 seeds and build type, in two runs of 4, as 8 now take more than 590 s: exit status 0, 0 columns differing. Reaching 150 is 19.5 to 24.1%, against about 6% before, and the largest piece of it is 6.1 to 8.7 km across. No seed starts above z = 110.

| Seed | Sea | 0..60 | >=150 | >=300 | >=500 | >=750 | >=1000 | Top | Typical | Far 10% | Big high | Largest high, km^2 | Across, km | Steep | Floating | Openings | Spawn at x, y, z | Spawn on |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2198175988018515243 | 29.1% | 27.7% | 22.62% | 12.92% | 5.13% | 1.31% | 0.22% | 1151 | 192 | 1024 | 14 | 12.1 | 8.7 | 10.9% | 729.7 | 44.26 | -200, -40, 12 | snow_block |
| 12636146071196537873 | 27.4% | 28.3% | 21.15% | 12.31% | 5.64% | 1.45% | 0.22% | 1189 | 256 | 1024 | 14 | 11.3 | 6.9 | 10.7% | 695.4 | 33.01 | 40, 56, 110 | snow_block |
| 5569068473609503046 (the default) | 28.8% | 29.5% | 20.87% | 11.84% | 4.84% | 1.31% | 0.19% | 1163 | 256 | 1216 | 12 | 8.8 | 7.6 | 10.5% | 618.5 | 39.73 | -120, 440, 14 | sand |
| 1 | 28.7% | 29.0% | 20.66% | 12.07% | 4.91% | 1.39% | 0.23% | 1209 | 256 | 1152 | 18 | 11.7 | 7.2 | 10.2% | 671.6 | 39.48 | 24, 40, 3 | sand |
| 42 | 28.1% | 29.8% | 21.06% | 12.36% | 4.88% | 1.22% | 0.24% | 1183 | 256 | 1024 | 16 | 11.9 | 7.3 | 10.4% | 721.6 | 42.68 | -88, 88, 10 | snow_block |
| 20261104 | 24.9% | 27.8% | 24.06% | 14.18% | 6.13% | 1.62% | 0.29% | 1163 | 256 | 1152 | 13 | 13.7 | 7.5 | 11.1% | 617.7 | 37.75 | 200, 184, 9 | grass_block |
| 9999999999999 | 25.7% | 29.2% | 21.30% | 12.49% | 5.38% | 1.35% | 0.17% | 1163 | 256 | 1280 | 11 | 14.1 | 8.0 | 10.2% | 580.8 | 36.05 | -184, 200, 29 | grass_block |
| 18446744073709551557 | 27.4% | 30.2% | 19.48% | 11.09% | 4.96% | 1.27% | 0.24% | 1190 | 256 | 1216 | 12 | 8.5 | 6.1 | 10.6% | 633.3 | 44.60 | -24, -24, 55 | snow_block |

### 2026-11-04, after plan 003 phase 8 stage 5, caves

Same 8 seeds and build type, in two runs of 4, exit status 0, 0 columns differing. Only caves and what aquifers hold moved, so the height bands are as stage 4's, within rounding. Openings at least 8 deep are 158 to 181 per km^2, from 33 to 45, and `bench/cave_walk/` says how many of them lead anywhere.

| Seed | Floating/km^2 | Pieces | Voxels | Largest | Openings/km^2 | Deep/km^2 |
|---|---|---|---|---|---|---|
| 2198175988018515243 | 1365.6 | 3675 | 5125 | 397 | 169.07 | 24.90 |
| 12636146071196537873 | 1228.3 | 3426 | 7133 | 833 | 171.38 | 24.38 |
| 5569068473609503046 (the default) | 1237.8 | 3336 | 6198 | 783 | 181.07 | 23.75 |
| 1 | 1258.0 | 3475 | 9614 | 1759 | 169.07 | 22.81 |
| 42 | 1433.1 | 3326 | 6711 | 1582 | 174.94 | 28.01 |
| 20261104 | 1233.1 | 3857 | 16593 | 4494 | 175.84 | 31.97 |
| 9999999999999 | 1095.5 | 3254 | 4572 | 183 | 158.23 | 28.28 |
| 18446744073709551557 | 1200.6 | 3797 | 7853 | 1848 | 177.07 | 33.20 |

### 2026-11-05, plan 003 phase 8's gate: openings read wrong for a while

From `fd38d63` on, `terrain_generate_box` leaves `TERRAIN_CAVE_WANTED_UNCUT` in the mask, and this tool took that for a cut. So openings read 225 to 254 per km^2 and deep ones 61 to 75, slices painted sea and sky a cave wanted as a cave's, and the caves' share of rock read high. No number in this file was recorded in that time. Since the fix, on the same 8 seeds, in two runs of 4, exit status 0, 0 columns differing:

| Seed | Openings/km^2 | Deep/km^2 |
|---|---|---|
| 2198175988018515243 | 161.61 | 24.10 |
| 12636146071196537873 | 170.26 | 24.47 |
| 5569068473609503046 (the default) | 181.45 | 24.56 |
| 1 | 166.10 | 22.71 |
| 42 | 170.54 | 27.17 |
| 20261104 | 170.83 | 31.37 |
| 9999999999999 | 155.80 | 27.79 |
| 18446744073709551557 | 174.69 | 32.77 |
