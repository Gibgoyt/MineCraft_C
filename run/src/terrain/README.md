# run/src/terrain

**Purpose:** makes a world's ground from its seed. Same seed, same bytes, for any number of workers in any order.
**Tier:** Owner.
**Owns:** `owned_tables`, a region of `TERRAIN_TABLES_BYTES`, and `arena_tables` over it, which lends everything computed once per world to the parts.
**Lifetime:** World: one `terrain_t` serves one seed. A new seed is a `terrain_deinit` and a `terrain_init`.
**Threads:** `terrain_init` and `terrain_deinit` on one thread, with no worker reading. In between it does not change, and any worker may read it.
**Public functions:**

- `terrain_init`, `terrain_deinit`: a pair.
- `terrain_generate_box`: every stage, in order, for one box, up to `TERRAIN_MADE_TOP`, with air above that for no work. `run/src/game/` calls it on a worker for each column. Since plan 006 phase 02 it takes the column's climate sample (`climate_sample/`), never null, which no stage reads until phase 03, so no block depends on it yet.
- `terrain_generate_box_fallback`: the same with `terrain_climate_fallback`'s sample for the box, for the tests, benches and test worlds that have no climate.
- `terrain_height_estimate_profile`, `terrain_height_estimate`, `terrain_height_estimate_area`: plan 004's height estimate (master plan 6.1). The ground the 2D splines give, with no 3D density and no chunk: the profile at the four lattice columns round the column, blended along x and then y exactly as a box blends them. On a lattice column (all multiples of 4) it is the profile itself, bit for bit, and the area form only asks there. A pure function of the seed and the column; `run/src/climate/` reads the ground through this and nothing else. The real top stone of a column is always within `min(h - s) - 8` to `max(h + s) + 8` of its four corners' profiles (`tests/run/terrain/density/`). Since plan 007 phase 03 the profile also carries `lift`, the plateau under the column, blended the same way. Since plan 007 phase 05 it carries `rivers`, blended, and `dry`, 1 if any of the four is a dry basin, as a box's voxels take it; `terrain_height_estimate_area` gives each lattice column's own `dry` into an optional array, for the climate.
- `terrain_biome_height_estimate`: `terrain_biome_height` (`biome/`) of the estimate's height and lift, the **biome height**: how high a column stands above the plateau under it. A biome's height axis means this (plan 007 phase 03; plan 006 phase 03 reads it), while the climate cools a plateau by its real height.

**May be called by:** `main`, which starts and stops it, and `game`, which borrows it and makes every column of the world with `terrain_generate_box`. `render/lod` samples the same stages for what the far field shows, on workers. `climate` reads the height estimate, from any thread.
**Depends on:** `heap`, `arena`, and its parts.

## Which land a seed makes

`TERRAIN_GENERATOR_VERSION`, in the header, names the land. Any change that gives any seed other blocks raises it, and a save made on another version is refused: see `run/src/save/world/`. `FEA_DIGEST_OF_GENERATOR` in `tests/run/terrain/feature/` has a row for each version, so a change to the land fails that test until the version goes up with it. One exception so far, open for Ahmed to confirm: plan 005 phase 11's chromium ore changed the land by veins in rock alone, and changed version 1's row rather than the version, so that worlds saved before it still open (that phase's issue 8).

## Parts

| Part | Job | Stage |
|---|---|---|
| `rng` | Seeded streams and per-position hashes | 10.1 |
| `noise` | Fractal noise fields, sampled in bulk on the NASM kernel | 10.1 |
| `fields` | Continentalness, erosion, ridges, temperature, humidity and weirdness at any column | 10.2 |
| `landform` | Plateaus and their escarpments, from macro fields on a 32-block lattice, for the density's height (plan 007 phase 03) | plan 007 |
| `density` | The ground's shape: height splines, 3D density, and bedrock, stone, water and air for any box of voxels | 10.3 |
| `hydrology` | Rivers, oceans, beaches, aquifers and the lava level, on top of the density's blocks | 10.4 |
| `cave` | Cheese, spaghetti and noodle caves cut from any box of voxels | 10.5 |
| `ore` | Deepslate by depth, and the table of rock and ore veins by depth, laid into any box of voxels | 10.5 |
| `biome` | Which of the biomes in `config/biomes/` holds any column or voxel | 10.6 |
| `surface` | What each biome lays on the ground, and lines its caves with | 10.6 |
| `feature` | What grows on the ground, and lakes, ice and snow, across chunks | 10.7 |

A part that needs tables takes them from `arena_tables` in its `_init`, called from `terrain_init`, and adds its noise fields
to `noise_bank` there too. `TERRAIN_NOISE_FIELDS_MAX` says how many fields fit.

## The one rule that keeps worlds the same

**What a job computes for a position comes only from `terrain_t` and that position.** Nothing is written during generation,
no stream carries over from one position to the next, and nothing depends on which worker ran or what ran before.
`tests/run/terrain/seed/` holds that to 1 worker against 16, and to any order of the work.
