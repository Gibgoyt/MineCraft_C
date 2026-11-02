# tests/run/math/ivec3

**Purpose:** ctest `run_math_ivec3`. Tests `run/src/math/ivec3/`: arithmetic, floor division and modulo on negatives against an
independent 64-bit reference across a whole range of values, the limits of int32, the floor from float32, and that a bad divisor
asserts where assertions are on.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main, and child processes for the assertion tests.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/ivec3/`.
