# tests/run/terrain/landform/measure

**Purpose:** ctest `run_terrain_landform_measure` (plan 007 phase 01 calls it `run_bench_landform_self_check`; ctest names a test by its directory). Runs `bench_landform --self-test`, which builds synthetic ground in memory and puts it through the very measures the seeds go through, at 4 blocks a column: a 1:1 cone, an `h = 800 (1 - r/1500)^2.5` peak, a 5 by 5 km plateau with a 1:2 edge, a plane of `g = 0.2236`, and twenty domes, ten on that plateau and ten on its field. It must say every check held and exit 0.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main; the program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `bench/landform/`. No card, no display, no seed.

The numbers each check wants, and why, are in `bench/landform/README.md`, "The self-test". The program prints what each got, and this test prints the program's output.
