# tests/run/game/cover

**Purpose:** ctest `run_game_cover`. Tests plan 004 phase 08's snow and ice on loaded ground, `run/src/game/cover/`, on small real worlds with no card: the rule for a column, with the week given by hand.
**Tier:** test.
**Owns:** the test arena, what its worlds take, and a directory of its own under `/tmp` for the save, removed at its end.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/test_world.h`, `run/src/game/cover/`, `run/src/world/`, `run/src/save/world/`.

What it checks, against the thresholds in `plans/004-ahmed-bohemoth-climate-system-phase-08-weather-in-the-world.md`:

- **Ice:** a week of 6 cm freezes the top of every still lake column and changes nothing else (one edit each); 3 cm changes nothing; 1 cm melts every one back to a source; running it twice logs no edit; water with a level never freezes; ice with no water under it is left; the sea freezes only over 30 cm.
- **The save:** frozen, flushed as an autosave would, and opened again: the ice comes back from the deltas.
- **Streaming in:** a column that comes back as generated is frozen and white like its neighbours; a chunk not loaded is never touched.
- **Snow:** a layer on every top of open grass over 20 mm, all gone at 10; none on leaves, ice, packed ice, snow blocks, lava, slabs, torches or flowers; none under a roof, and a roof put over old snow sees it go at the thaw; a block placed on a layer takes its cell, and the layer is thin, walked through and replaceable; 25, 15, 25, 15 mm keeps the layer all four weeks with no edit, and an unknown week changes nothing.
- **What it costs:** a first snow and ice over 4225 columns, as render distance 32 loads, of flat ground with a pond each: the time and the edits, failing past 10 s (Risks R3).
