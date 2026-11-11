# tests/compile/game_tables

**Purpose:** ctest `compile_game_tables`: the real block and item tables say what `config/blocks/` and `config/items/` say, dirt drops dirt, every drop is an item, and `compile/src/game_tables/` refuses each bad file with the right line, for blocks, items, recipes, biomes, ores (plan 005 phase 11: a good tree of ores makes its rows in their order) and mobs, an animal's `mind`, `male`, `female`, yields, `on`, `biomes`, `min_light` and heights among them (plan 005 phase 4), and that a good animal makes its sexes' rows, yields and rules where they belong.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_game_tables`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/game_tables/` (run as a process), `compile/src/yaml/`, the generated `tables/blocks/` and `tables/items/`, which `cmake/targets.cmake` compiles into this test, and `rm` from the system. It needs no server, no display, no network.
