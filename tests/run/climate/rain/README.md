# tests/run/climate/rain

**Purpose:** ctest `run_climate_rain`. Tests `run/src/climate/rain/` (plan 004 phase 05), mostly on tiles built straight from their windows with the test's hooks: the rain-belt spline through its nine knots, 426.7 at 25° and 1453.8 at 13.26°; the upwind walk meeting a straight coast at step 8 (`d_up = 896`) from 992 blocks in and at step 1 from 32; a sea met past the window's 16 steps with no current, and none met by step 64 (`d_up = 8192`, `M = 0.15`); moisture's and the orographic factor's worked examples, cold winning over warm; Ahmed's South Africa sketch at 25° S with the master plan's assumptions (wind fixed from the east, seasons frozen, `P_belt = 500`): the walks' inputs, then 644, 1111 and 70.3 mm (within 10% of 625, 1125 and 69), the west coast 4.5 mm by 6.7's own rain shadow and 30.0 with `O = 1`, the order, and both dry places Desert; the same with the real belt, 0.853 times each; the real year printed for each place (yearly rain, wettest and driest month, the wind on days 15 and 197); a ridge at 15° N with the windward side at least 3 times the lee's rain; and 10,000 reads on a real seed never negative.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers for the real seed.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `terrain`, `job`, `thread`, `tests/run/climate/fixtures/` (the South Africa and ridge grounds). No graphics card.
