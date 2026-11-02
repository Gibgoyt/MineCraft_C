# tests/run/world/map

**Purpose:** ctest `run_world_map`. Tests `run/src/world/map/` and the Owner's `world_open` and `world_close`: slots against plain division, requests nearest first and exactly what the view holds, requests out of date, columns leaving and coming back, `z` of `-251` and `501` refused, and a random walk against a brute-force model of the table, at every step.
**Tier:** test.
**Owns:** nothing outside the `world_t` each test starts and stops. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/`, `run/src/heap/`, and the generated `tables/blocks/`.
