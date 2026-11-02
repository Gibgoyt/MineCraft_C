# tests/run/math/mat4

**Purpose:** ctest `run_math_mat4`. Tests `run/src/math/mat4/`: the storage order, products and a determinant worked out by hand,
inverses on many random transforms, singular matrices, aliased output, and the camera: `mat4_look_at` with x east, y north, z up,
and `mat4_perspective`'s depth range.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/mat4/`, `run/src/math/`.
