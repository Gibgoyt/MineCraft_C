# tests/run/climate/warning

**Purpose:** ctest `run_climate_warning`. Tests `run/src/climate/warning/` (plan 004 phase 10): a known hurricane at 51% is named and one at 49% or exactly 50% is not, the most likely of several named, ties to the lower id; two at 30% make "hurricane likely" (51%) and two at 28.5% do not (48.9%), and unborn arrivals alone at 1 - e^-0.7134 (51.0%) warn and at 1 - e^-0.6733 (49.0%) do not; a storm low warns only when no hurricane does, and a high right over the place is never a storm; Hurricane Tavela through the outlook's own per-system chance, the Background's five chances from 6 km east with the forecast error off, and with it on the player 2983 blocks past the day's forecast centre (51.0%, named, the fifth moment the highest) and 3071 (49.0%, none); at whole days the per-system chance is the one `climate_outlook_run` gives, to the bit as a float32_t; on a real world (seed 2198175988018515243, 15 degrees north in hurricane season) the same warnings, byte for byte, from a climate built on 1 worker and on 16; 100 warnings there, the first call on a fresh climate, the slowest and the mean printed, and the mean under 20 ms (five times that under the sanitizers): the phase 10 file asks for the slowest under 20 ms, which is not met (43 ms on release, 81 on debug: its Issues found); and no warning, and false, where no tile is built.
**Tier:** test.
**Owns:** the test arena and climates of its own, closed by the end of each test.
**Lifetime:** One run.
**Threads:** main, and the job system's workers that build the climate's tiles.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts `warning/`, `outlook/` and `systems/`, `run/src/terrain/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`.

The places are within the 4.2 km square the rig prepares, not 40 km out as the phase 10 file's timing test says: a square that wide would take every tile's window of mask pages over 6,400 km², minutes on debug, for a number phase 11 records on release anyway.
