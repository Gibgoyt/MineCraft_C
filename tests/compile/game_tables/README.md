# tests/compile/game_tables

**Purpose:** ctest `compile_game_tables`: the real block tables say what `config/blocks/` says, dirt drops dirt, and `compile/src/game_tables/` refuses each bad file with the right line.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_game_tables`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/game_tables/` (run as a process), `compile/src/yaml/`, the generated `tables/blocks/`, which `cmake/targets.cmake` compiles into this test, and `rm` from the system. It needs no server, no display, no network.
