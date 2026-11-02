# tests/run/math/vec3

**Purpose:** ctest `run_math_vec3`. Tests `run/src/math/vec3/` against values worked out by hand, the cross product's handedness
and identities on many random inputs, normalizing where there is no usable length, and that the build does not fuse a dot
product into an FMA, which would change results.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/vec3/`.

The FMA check is worth running in release as well as debug, because `-flto` generates the code at the link.
