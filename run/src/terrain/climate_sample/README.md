# run/src/terrain/climate_sample

**Purpose:** the climate a column job's terrain may read (plan 006 master 2.1 to 2.3, phase 02): a seam, not a dependency.
`terrain` (lint row 9) may not include `climate` (row 8), because the climate reads the terrain's height estimate. So this part
defines plain numbers, `terrain_climate_sample_t`, 6 by 6 cells of 64 blocks round a column, 888 bytes, which the climate copies
its cells into (`climate_terrain_sample` in `run/src/climate/`, called by `game`'s column jobs, the far field and mob spawn), and
which terrain only ever reads through `const terrain_climate_sample_t *`. Where there is no climate, the fallback fills it from
latitude alone.
**Tier:** Arena by depth. It allocates nothing; a borrower of the sample it is handed.
**Owns:** nothing.
**Lifetime:** Values only. A sample lives in a worker's Job arena or on a stack for one job or one step.
**Threads:** any. No shared state.
**Public functions:**

- `terrain_climate_sample_origin`: where a column's sample starts, 112 blocks (`TERRAIN_CLIMATE_WEST_BLOCKS`, equal to
  `CLIMATE_READ_REACH_BLOCKS`) west and south of it, down to a cell corner. 6 cells cover the 240 blocks a column's reads can
  reach (16 + 2 x 112) with room for the alignment.
- `terrain_climate_cell_make`: the one place the climate's floats become the sample's integers: centi-degrees, mm, blocks, by
  `terrain_climate_round` (nearest, halves to even, clamped; the 1.5 x 2^52 add, so no libm).
- `terrain_climate_classify`: plan 004 6.10's first match in integers, the same answer as `climate_season_classify` on every
  place `run_terrain_climate_sample` tries. No dry summer is kept, so Mediterranean comes only from a cell's own class.
- `terrain_climate_warp`: where a read lands: two octaves of hashed value noise on lattices of 256 and 128 blocks
  (`terrain_rng_hash2` of the sample's `warp_seed`), a smoothstep between lattice points, spread as the climate spreads its warp
  (clamped, `u (1.5 - 0.5 u^2)`), to at most 32 blocks each way. All integers, in 65536ths.
- `terrain_climate_at`: the four cells round the warped point, bilinear between cell centres in 64ths, rounded half up. The
  temperatures at the column's own z: minus 12 centi-degrees each 10 blocks (rounded down), and below sea level on land plus the
  basin's 25 each 10 blocks; warm and cold are the mean plus and minus the swing times 0.98862 (a month's mean of the yearly
  wave, `CLIMATE_MONTH_WAVE1`), so they are the warmest and coldest months as the climate's class sees them. The snow line is
  blended over the corners that are not sea, as `climate_snow_balance_blend` does. Class and flags are the nearest cell's. A read
  past the cells takes the edge. With `warp_off` set, the read is at the point itself, for tests.
- `terrain_climate_fallback_cell`, `terrain_climate_fallback`: where there is no climate (test worlds, terrain tests and benches,
  past the built tiles): plan 004 6.6's yearly mean at sea level by latitude, the swing at κ 0.5 with no current, 6.7's rain belt
  at the latitude times moisture 0.6 and orography 1, the driest month a twelfth of the year's rain (no waves), the snow line
  where the warmest month at sea level reaches 0 °C with the lapse (none above the world's top, the floor below its bottom), and
  the class at sea level. Land. A pure function of y; the seed only seeds the warp.
- `terrain_climate_warp_seed`: the 32-bit warp seed of a world's seed.

The fallback needs plan 004's knot tables and latitude. Terrain may not include the climate's headers, so they are copied here,
and `run_terrain_climate_sample` holds each copied number, and the fallback's temperatures and rain against the climate's own
formulas pole to pole, equal to the climate's.

**May be called by:** `terrain` and its parts (from plan 006 phase 03 the biome, surface and feature rules), `climate` (to fill
a sample), and anything from `game` down that fills or reads one.
**Depends on:** `math/det` (the latitude), `terrain/density` (the spline the climate's tables use), `terrain/rng` (the warp's
hash), `machine` (the world's floor and top), `types`.
