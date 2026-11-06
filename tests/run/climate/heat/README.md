# tests/run/climate/heat

**Purpose:** ctest `run_climate_heat`. Tests `run/src/climate/heat/` and the Owner's `climate_normals_at` (plan 004 phase 05): the equator beach at 25.914 to 28.014 °C all year; the 45° N peak at -10.50 °C on day 15 and 8.47 on day 196 (within 10% of the master plan's -10.4 and 8.4), warmest on day 204; land 150 below sea level at 28° N at 34.63 °C (34.5), 40.35 in the afternoon, 30.88 without `B`; 12 °C colder for each 1000 blocks up and 7.40 warmer 200 below sea level on land; the daily range 11.444 dry and 7.104 at 6 mm a day; the snow share's ramp; the south the same as the north half a year later, at 100 places and 12 days on a coast that depends on x alone; the read equal to the formula bit for bit at 64 cell centres and the blend of four cells between them; and `t_mean` stored at sea level, with a read at 600 7.2 °C colder than at 0 and a mine 200 below sea level 7.4 warmer.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers that build tiles.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `job`, `thread`, `tests/run/climate/fixtures/`. No graphics card.
