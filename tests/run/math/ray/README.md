# tests/run/math/ray

**Purpose:** ctest `run_math_ray`. Tests `run/src/math/ray/`: the slab test on all six faces, in both directions, parallel to an axis,
starting inside, grazing, limited by a t range, and with NaN; agreement with a brute-force march along many random rays; and
`ray_from_ndc` through a real camera.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/ray/`, `run/src/math/`.
