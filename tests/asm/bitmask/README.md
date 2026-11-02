# tests/asm/bitmask

**Purpose:** ctest `asm_bitmask`: `asm/src/bitmask/` against its scalar reference and an independent count, at every count
and density, at the popcount's widening boundaries, with `dst` aliasing an input, and between guard pages.
**Tier:** none; tests are not tiered.
**Owns:** the regions each test reserves, for that test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/src/bitmask/`, `simd/reference`, `tests/asm/support/`.
