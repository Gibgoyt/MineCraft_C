# tests/run/world/column

**Purpose:** ctest `run_world_column`. Tests `run/src/world/column/` and `run/src/world/` itself: coordinates, the `z` limits with `-251` and `501` refused, random edits against a brute-force array of the whole gameplay range, streaming, and the Owner's init, deinit and reset.
**Tier:** test.
**Owns:** nothing outside the `world_t` each test starts and stops.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/`, `run/src/heap/`, and the generated `tables/blocks/`.

The Owner has no test directory of its own, since a world with no columns has little to show. These tests run on a real `world_init`, so that what they use is what the game uses.
