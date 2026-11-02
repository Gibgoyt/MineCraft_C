# tests/run/math/quat

**Purpose:** ctest `run_math_quat`. Tests `run/src/math/quat/`: known rotations, agreement with `mat4_rotation` on many random inputs,
every branch of `quat_from_mat4`, composition order, and `quat_slerp`'s ends, speed and choice of the shorter way round.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/quat/`, `run/src/math/mat4/`, `run/src/math/`.
