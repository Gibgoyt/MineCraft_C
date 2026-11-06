# tests/run/climate/layer

**Purpose:** ctest `run_climate_layer`. Tests `run/src/climate/layer/`: each layer's colours as its README gives them, by hand and drawn on a straight coast; the latitude lines on exactly the rows of 0, ±23.5 and ±66.5 degrees at 16 blocks a pixel from y = -40,000 to 40,000, and nothing else; unbuilt tiles grey; and each layer at 512 by 512 on seed 2198175988018515243, hashed. The hashes are printed and each held to a second draw; they are pinned only after Ahmed has looked at the pictures (`bench_climate --pictures`), as `run_ui_menu` does.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and the job system's workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/climate/fixtures/ground.h`, `run/src/climate/` and its parts, `run/src/terrain/`, `run/src/job/`. No graphics card.
