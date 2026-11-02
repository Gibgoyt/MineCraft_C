# tests/asm/abi

**Purpose:** ctest `asm_abi`: what no comparison of outputs can see. Every SIMD kernel, at several lengths, must keep `rbx`,
`rbp` and `r12`-`r15`, and must leave no dirty YMM state, which `XGETBV` register 1 reports.
**Tier:** none; tests are not tiered.
**Owns:** nothing beyond the test arena.
**Lifetime:** one test.
**Threads:** the test's main thread, which runs each kernel on a stack of the test's own through `setcontext`.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `run/include/simd/{copy,blend,bitmask,noise,vec_batch,cpuid}/`, `math/mat4`, `tests/asm/support/`.

A new kernel needs its line in the test's list.
