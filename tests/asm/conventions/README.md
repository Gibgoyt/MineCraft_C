# tests/asm/conventions

**Purpose:** ctest `asm_conventions`: `asm/include/conventions/`'s `REQUIRE_CPU`, run through the build's own NASM: each
kernel assembles without a word under the real config, and refuses a config that turns off an instruction set it uses.
**Tier:** none; tests are not tiered.
**Owns:** nothing.
**Lifetime:** one test.
**Threads:** the test's main thread, and the NASM it starts and waits for.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/include/conventions/`, `asm/src/{copy,blend,bitmask}/`.
