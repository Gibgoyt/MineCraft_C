# tests/run/climate/cell

**Purpose:** ctest `run_climate_cell`. Tests `run/src/climate/cell/` through the Owner, `run/src/climate/`, on real workers: the phase 03 file's worked examples (288 blocks, direction (-0.6, -0.8) and κ 0.1747 three cells west and four south of a sea; 32 and 0.0211 beside it; 8192 and 0.99575 with no sea); a straight coast rising by 64 a cell, and the same mirrored for the sea's distance to land; flags for sea, a large lake (3.1 km²) and a small one (1.05 km²), and none on land, with lakes never lowering κ; `z_ground` the mean of 16 samples; latitude phase 02's at each cell centre, and exactly 90 at the pole through `climate_sample_at`; the blend's worked example (0.2625) and the same blend through the Owner with the warp off; the warp within 32 blocks and really on; a tile's cells the same bytes whether built alone, after its neighbours, or after page jobs built its window; 1 and 16 workers giving the same bytes over 8 by 8 tiles, held to a pinned digest, with no two neighbouring cells more than 64 blocks apart in sea distance (no steps at tile borders); negative coordinates at -1, -1024 and -1025; a read false until its tiles are built, writing nothing; and the game's column gate: a column is not asked for until `climate_area_ready`, and `climate_schedule` builds the player's tile first, never more than 4 at once.
**Tier:** test.
**Owns:** nothing outside the test arena; each test's climate region is released before it ends.
**Lifetime:** One run.
**Threads:** main, and the job system's workers (1 or 16).
**Public functions:** none; a test program.
**May be called by:** ctest. The release build's run must print the same digest as the debug build's.
**Depends on:** `tests/harness/`, `tests/run/climate/fixtures/ground.h`, `run/src/climate/` and its parts, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`. No graphics card.
