# tests/asm/noise

**Purpose:** ctest `asm_noise`: `asm/src/noise/` against its scalar reference bit for bit at every scale, between guard pages
and off alignment, and against pinned values, a range, no seams, and no pattern a correlation can see.
**Tier:** none; tests are not tiered.
**Owns:** the regions each test reserves, for that test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/src/noise/`, `simd/reference`, `tests/asm/support/`.
