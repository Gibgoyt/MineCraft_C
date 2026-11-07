# tests/run/climate/constants

**Purpose:** ctest `run_climate_constants`, plan 004 phase 11. Pins every constant of the climate system to the value in the phase 11 file's constant table: its **Tuned** column where a value was changed after the master plan, else the plan's value, or the phase's own where a phase chose one. It checks:

- `every_constant_has_the_value_its_table_gives_*`: one check per named constant, by section (calendar, sun, cells, currents and wind, temperature and rain, seasons and classes, systems, daily weather and the week, the outlook and phases 08 to 10). A failure names the constant, the value found and the value the table gives.
- `derived_constants_follow_their_sources`: `CLIMATE_POLE_Y` is `CLIMATE_TROPIC_Y × 90 / CLIMATE_TILT_DEG` (38 298 to the block), the latitude period four times that, the polar circle `90 - 23.5`; every part's year, week and hours equal the calendar's; the other constants worked out from others.
- `knot_tables_have_every_knot_in_order`: each knot table in `run/include/climate/normals/index.h` has its latitudes rising and every knot the master plan's, and the curves the climate reads pass through them.
- `no_unnamed_number_in_the_climate_code`: the **literal sweep**. It reads every `index.c` under `run/src/calendar/` and `run/src/climate/` as text and fails on any number in a function body, or as an array's size, that is not on the allowlist written at the top of the sweep in `index.c`, one comment per entry: 0, 1, 2, 0.5 (and so -1); the 3 of a smoothstep; 180 and 360; forms of π; an array subscript; the numbers of named definitions outside function bodies; `static_assert` lines and `_padN` members. Each failure prints the file, the line and the literal.
- `the_sweep_catches_an_unnamed_number`: the sweep on a sample with four planted numbers finds exactly those four.

**Changing a value:** a deliberate change means changing the constant, its line in this test and its row in the phase 11 file's table, together, with an entry in that file's Issues found. That is the record phase 11 keeps. A test that fails here after an edit elsewhere means a climate number changed without that record.

**Tier:** test. **Owns:** its test arena. **Lifetime:** one run. **Threads:** main only.
**Public functions:** none; a test program.
**May be called by:** ctest. Runs from the repository root (ctest's working directory), which the sweep's paths are relative to.
**Depends on:** `tests/harness/`, the headers of `run/src/calendar/`, `run/src/climate/` and its parts, `game/`, `game/cover/`, `player/movement/`, `ui/atlas/`, and `file/` (`file_read_whole`). No graphics card, no display.
