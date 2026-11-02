# tests/asm/copy

**Purpose:** ctest `asm_copy`: `asm/src/copy/` against its scalar reference, at every short length and alignment, between
guard pages, with pitches, and on a whole 1080p frame.
**Tier:** none; tests are not tiered.
**Owns:** the regions each test reserves, for that test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/src/copy/`, `simd/reference`, `tests/asm/support/`.
