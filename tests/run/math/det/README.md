# tests/run/math/det

**Purpose:** ctest `run_math_det`. Checks `run/src/math/det/` against glibc's libm on 1,000,000 random points per function
(relative 1e-12 for `det_exp`, absolute 1e-12 for the rest), the exact edges (`det_sin(0) == 0`, `det_cos(0) == 1`, the
atan2 zeros, NaN and infinity), and pins each function by an FNV-1a digest over the result bits of 65,536 fixed inputs, which
must print the same in debug and release.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/math/det/`, libm as the reference only.
