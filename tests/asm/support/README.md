# tests/asm/support

**Purpose:** what the tests under `tests/asm/` share: buffers between guard pages, and a repeatable random source.
**Tier:** none; tests are not tiered.
**Owns:** nothing past what each test holds in its `guard_t`.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** `guard_init`, `guard_deinit`, `test_random`, `test_random_fill`.
**May be called by:** `tests/asm/*`.
**Depends on:** `tests/harness/`, `heap`.
