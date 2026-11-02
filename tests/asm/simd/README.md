# tests/asm/simd

**Purpose:** ctest `asm_simd`: the `simd` Owner tier: it reserves one region and gives it back, its self-check passes, and a
failed reservation leaves nothing behind.
**Tier:** none; tests are not tiered.
**Owns:** nothing past each test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `run/src/simd/`, `heap`.
