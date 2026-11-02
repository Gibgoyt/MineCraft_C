# tests/run/math

**Purpose:** ctest `run_math_owner`. Tests `run/src/math/`: `math_init` against each floating-point mode it must refuse, that a new
thread does inherit the mode, as `main` relies on, and the scalar helpers. Each part has its own test in a sub-directory, which
gives `run_math_vec2`, `run_math_vec3`, `run_math_vec4`, `run_math_ivec3`, `run_math_mat4`, `run_math_quat`, `run_math_aabb` and
`run_math_ray`.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main, and one short-lived thread it joins.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/`, glibc's `fenv.h`. Reaching flush-to-zero takes `fenv_t`'s MXCSR field, so this
one test is x86-64 only, like everything else here today.

The `run_math_` prefix is why `test.cmake` sets the name: the path alone would give `run_math`, and Stage 04.1's verification
selects `^run_math_`.
