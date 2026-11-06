# tests/run/climate/season

**Purpose:** ctest `run_climate_season`. Tests `run/src/climate/season/` (plan 004 phase 05): the sea at 15° N over a warm current at 31.95 °C on day 215 (`s = 1`) and 28.05 on day 32 (`s = 0.775`), 24.05 without the current; the stored season `{39, 68}` without it and `{0, 91}` with it; nothing within 5° or past 25°; quarter-day packing, a southern season wrapping past new year, and "no season" never today; a coast 2016 blocks from a hurricane sea flagged with the sea's days and one 4000 blocks in not; the monsoon flag on the Background's worked example and not on a steady wind or halves of 180 and 70 mm; monsoon today in the wet onshore half; the snow balance at 45° N (1484 ± 16), a 60° coast (325 ± 16), a 75° coast (-250), the equator (9999), falling with latitude; the 14 class cases with the first match winning; and through tiles, the 45° N peak Tundra with its warmest month at the formula's value, sea cells `NONE`, and the South Africa plateau Desert over a real year.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers that build tiles.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `job`, `thread`, `tests/run/climate/fixtures/`. No graphics card.
