# tests/run/climate/outlook

**Purpose:** ctest `run_climate_outlook`. Tests `run/src/climate/outlook/` (plan 004 phase 07): Hurricane Tavela (6 km east, west at 3.5 km a day, radius 2.5 km, no forecast error) at the footprint form in force on day 2, both forms against their formulas and the exact form's known values; background rain 0.393, 0.865 and 0 by hand; "at least one" 0.493 by hand; the forecast error's spread of 1 and its repeat; an outlook run on a copy of its input after the climate has closed and its arena is scribbled over giving the same bytes; the same census's input and outlook from a fresh climate twice and from a new process (the test binary run again on itself); a year of censuses at 45° N where no system in an input was born after the census moment or dead by it, while the census day's own list and the coming week hold systems born later, none of which is ever in an input; and the calibration: 2 real seeds, 16 places on coasts from 8° to 55°, 520 censuses each, every (chance, happened) pair of rain (the day over 1 mm), thunder (a cell over the place at an hour of the day), storm, hurricane (at the instant) and each named system (that one over the place at the instant), with every 0.1 bin's count and observed share printed, and the 0.65 to 0.75 bin, with at least 300 pairs, between 0.60 and 0.80.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers that build climate tiles and, in the calibration, work a place each.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `save/climate` (its hash, for digests), `terrain`, `job`, `thread`. No graphics card.
