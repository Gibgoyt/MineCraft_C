# tests/run/climate/systems/rain

**Purpose:** ctest `run_climate_systems_rain`, master plan 7.4's calibration (plan 004 phase 06). For 8 seeds, each on its real ground, a 4 by 4 km test box at its own latitude (0°, 10°, 15°, 25°, 35°, 45°, 55°, 65° north; the nearest land box within 20 km at that y, else the sea there) with 16 test cells 1 km apart. Every tile the box's cells need is built (the box plus the cells' 4.2 km reach and a square). For each day of 10 years, one day list for the box (big systems 2 km beyond it, cells at it), and every 15 minutes at each test cell the expected rain: the background's expected rate, the normal times `1 - f_sys`, plus the hurricanes', lows' and cells' rain, all times the high's cut. Each cell's 10-year total must be within 15% of its normal. Prints, per seed and cell, the normal, the total, each kind's share, hurricane and low rain-days, cell passages, and the cells' sampling noise `1/√n`. The marked spawn rates in `run/include/climate/systems/index.h` are tuned against it, never the normals.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, which builds the tiles, and the job workers, one site-year a job.
**Public functions:** none; a test program.
**May be called by:** ctest; `test.cmake` gives it 3600 seconds. The phase file runs it on release, where it takes minutes.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `terrain`, `job`, `thread`, `time`. No graphics card.
