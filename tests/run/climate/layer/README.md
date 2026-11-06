# tests/run/climate/layer

**Purpose:** ctest `run_climate_layer`. Tests `run/src/climate/layer/`: each layer's colours as its README gives them, by hand and drawn on a straight coast; the latitude lines on exactly the rows of 0, ±23.5 and ±66.5 degrees at 16 blocks a pixel from y = -40,000 to 40,000, and nothing else; unbuilt tiles grey; and each layer at 512 by 512 on seed 2198175988018515243, hashed. Phase 04's three: current arrows blue off the 30° S continent's west coast with their head north, red off its east coast with their head south, a grey cross on its neutral north coast, and land unchanged; pressure lines on exactly the rows a field rising linearly northward crosses 1020, 1016, 1012, 1008 and 1004 hPa, with the ramps' end colours and grey with no line where unbuilt; wind arrows on open sea at 15° N pointing west-south-west over the speed's colour; and currents, pressure and wind on days 15, 105, 196 and 288 at 256 by 256 on the same seed, hashed. The hashes are printed and each held to a second draw; they are pinned only after Ahmed has looked at the pictures (`bench_climate --pictures`), as `run_ui_menu` does.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and the job system's workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/climate/fixtures/ground.h`, `run/src/climate/` and its parts, `run/src/terrain/`, `run/src/job/`. No graphics card.
