# tests/run/climate/mask

**Purpose:** ctest `run_climate_mask`. Tests `run/src/climate/mask/`: a page holds the ground at each cell centre (a plane `h = x - 3y`, page (2, -3) exactly); a page from the real terrain is `terrain_height_estimate` at each centre, bit for bit; a window is its 17 by 17 pages in place, built and kept the first time, read from the table the second, and built privately outside the keep square; and a page claimed by someone else is copied privately with the same values while the table keeps the claim.
**Tier:** test.
**Owns:** nothing outside the test arena: the mask table here is the test's own.
**Lifetime:** One run.
**Threads:** main only.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/climate/fixtures/ground.h`, `run/src/climate/mask/`, `run/src/climate/cache/`, `run/src/terrain/`. No graphics card.
