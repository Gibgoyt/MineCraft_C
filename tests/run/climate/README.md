# tests/run/climate

**Purpose:** ctest `run_climate`. Tests `run/src/climate/`: latitude 0 at the equator, 23.5 at the tropic (y = 10,000), 66.5 at the polar circle, 90 at each pole and 0 again at y = 76,596; 45 degrees at y = 19,149; the fold back past a pole (y = 30,000, 50,000, 100,000); the repeat every period, with the whole number 153,191 and the exact `CLIMATE_LATITUDE_PERIOD`, for 10,000 y out to the world's edge; an even rise of 0.00235 degrees a block from pole to pole; and the same bits from 16 threads as from one. The checks are the phase 02 file's table for `tests/run/climate/`.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and 16 short-lived threads it joins for the purity test. Each writes only its own slice of an array, and main checks.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/`. No graphics card.

`latitude_does_not_care_about_x` is true by construction: `climate_latitude_deg` takes only y, so it has no test body.
