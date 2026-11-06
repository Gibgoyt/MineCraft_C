# tests/run/climate/cache

**Purpose:** ctest `run_climate_cache`. Tests `run/src/climate/cache/`: slots follow the coordinates modulo the side, for negative ones too (tile (-1, 70) and (63, 6) both in slot 447); the keep square; a tile outside it is never cached and never pushes out a kept one; a far tile is replaced by a near one in its slot; a reader holding a slot stops its eviction, which then succeeds once the reader lets go; and 8 reader threads against one thread swapping two tiles in one slot 100,000 times, each tile's 256 words a pattern from its key, where every read that succeeds sees one key's pattern whole.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and 9 short-lived threads it joins in the race test.
**Public functions:** none; a test program.
**May be called by:** ctest. Also run by hand under the `tsan` preset: `tools/test-or-build.sh timeout 600 ctest --preset tsan -R '^run_climate_cache$' --output-on-failure`, which is what finds a memory order that is too weak.
**Depends on:** `tests/harness/`, `run/src/climate/cache/`, pthreads. No graphics card.
