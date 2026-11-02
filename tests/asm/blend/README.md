# tests/asm/blend

**Purpose:** ctest `asm_blend`: `asm/src/blend/` against a plain-division definition for every source, destination and
alpha, against its scalar reference through every path, and between guard pages.
**Tier:** none; tests are not tiered.
**Owns:** the regions each test reserves, for that test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/src/blend/`, `simd/reference`, `tests/asm/support/`.
