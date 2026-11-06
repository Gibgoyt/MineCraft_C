# tests/run/climate/sea

**Purpose:** ctest `run_climate_sea`. Tests `run/src/climate/sea/` on synthetic grids: the phase 03 file's 6 by 6 worked example (three components, labelled in row-major order of their first cell, a diagonal joining nothing); deep water makes its whole body sea and touching the window's edge does not (the rule of the phase file's Issues found 17); water of 8192 cells and more is sea, 489 to 8191 a large lake and less a small lake, at the thresholds themselves; a channel one cell wide joins a lake to the sea and a corner does not; the distance transform against a brute-force search on a straight coast, a diagonal coast, an L-shaped bay, a checkerboard, all sea and no sea on 64 by 64 grids, and 200 random sea cells and a round island of radius 40 on the full 272 by 272 window, with every nearest cell at exactly the distance given; the worked example's row, ties to the lower index; and no marked cell giving `CLIMATE_SEA_FAR` everywhere, with guard words round every array.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main only.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/sea/`. No graphics card.
