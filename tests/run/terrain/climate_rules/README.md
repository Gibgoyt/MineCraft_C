# tests/run/terrain/climate_rules

**Purpose:** ctest `run_terrain_climate_rules`. Tests plan 006 phase 04: permanent snow, generated ice, desert sand,
snowy beaches and tree rates follow the climate (master section 2.5), in `run/src/terrain/surface/` and
`run/src/terrain/feature/`, and the far field's cold column agrees with near columns.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and each test starts and stops its own systems.
**Lifetime:** One run.
**Threads:** main, and the job workers that build the climate's tiles.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/`, `run/src/climate/`, `run/src/job/`, `run/src/thread/`,
`run/src/heap/`, and the generated `tables/biomes/` and `tables/blocks/`.

What each test holds, all on Ahmed's seed (the world of `~/Pictures/1.png`) unless it says:

| Test | Holds |
|---|---|
| `the_ragged_edge_is_within_twenty_four_blocks` | Over 100 000 columns the wander is within ±24 blocks and uses the band; `terrain_surface_snow_from` is the line plus it, anywhere in Ice, nowhere with no line; master 2.5's worked example (line 700: top 690 against 688 is snow, top 705 against 718 is grass); snowy slopes' filler, a frozen peak's packed ice and windswept hills' `high` snow give way below the line |
| `ice_follows_the_coldest_month` | -1.5 degrees at sea level: water 60 up freezes (-2.22), at sea level not; the basin heat below 0; and the same through `terrain_feature_cold_column` on a hand-built sample |
| `tree_rates_scale_with_rain` | `forest` (rain_mm 500 to 1800, middle 1150): 1.8 times the middle gives 1.5, 0.4 times gives 0.4; a biome with no `rain_mm` keeps 1 |
| `sand_only_in_desert_and_steppe_and_snowy_beach_only_in_ice_and_tundra` | The table, and 8 seeds pole to pole on the fallback climate |
| `the_equator_has_no_snow_and_no_ice` | Master 5: every climate cell of the 16 km square round (0, 0) has no snow line under the world's top + 24, and at the top a warmest month of 0 degrees or more (no Ice family; Tundra high up is counted and printed, as with a snowline over the top it lays no snow) and a coldest of -2 or more, which is the proof for every column there; then every 64th column read as the stages read it; then not one frozen voxel in the whole of the 3 by 3 chunks round the highest ground found, (0, 0) and x -68, y 149 |
| `the_screenshot_place_is_not_snow` | x -68, y 149's top is not snow or ice |
| `pole_land_is_cold_and_snowy_above_the_line` | At 75 degrees every column above the sea is Ice or Tundra; in 16 chunks no snow under a column's line - 24 outside Ice, and every dry top at or over line + 24 lies under snow unless something grew over it |
| `a_45_degree_mountain_has_snow_only_above_its_line` | The highest column over its own line at 45 degrees (or the first of 50, 55, 60 that has one, printed); 7 by 7 chunks round it held to the same rule |
| `the_far_field_agrees_with_near_columns` | 64 columns at 75 and 50 degrees: hydrology, surface and `terrain_feature_cold_column` give the near column's top block and height, where nothing grew, no lake lies and no cave cut near the top |

The line a column is held to is its `terrain_biome_column_t.snow_line_z`: the climate blended at its 4-block lattice
corner, the very number the surface and the features read.

**What it does not say.** One worker against sixteen is `run_terrain_biome`'s
`one_and_sixteen_workers_give_the_same_bytes`, which runs every stage, these rules included. Nothing here says the snow
line looks right; that is the phase gate's.
