# tests/run/terrain/climate_sample

**Purpose:** ctest `run_terrain_climate_sample`. Checks `run/src/terrain/climate_sample/` on hand-built samples and against the
climate: the master's sizes (24 and 888 bytes) and every number the sample mirrors from the climate (cell size, warp, the
no-snow sentinel, the classes and their thresholds, the lapse, the basin, a month's wave, the read reach); the bilinear blend
worked by hand with the warp off, the snow line over land corners and the edge; 12 °C a 1000 blocks (2700 at sea level, 1200
at 1250) and the basin below 0; rounding halves to even; the integer class against `climate_season_classify` on a grid of
places; the fallback pure and defined on 100,000 points, poles and the world's edge included; the fallback's temperatures and
rain against the climate's formulas at κ 0.5 pole to pole; no snow line at the equator, in the fallback and in the climate's own
snow balance; and 4096 samples the same bytes from 1 worker and 16.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main, and 16 workers for the last test.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/climate_sample/`, `run/src/climate/` (its constants and formulas as the
reference), `job`, `thread`.
