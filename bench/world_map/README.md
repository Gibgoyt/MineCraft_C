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

**This is not the game's renderer.** Colours are this program's, typed by hand.

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

## Results

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
