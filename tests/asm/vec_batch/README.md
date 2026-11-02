# tests/asm/vec_batch

**Purpose:** ctest `asm_vec_batch`: `asm/src/vec_batch/` against the scalar functions of `math/mat4` bit for bit, between
guard pages, and off alignment. By including the generated `struct_offsets/index.h`, it also has the compiler confirm every
offset `compile/src/struct_offsets/` gave to NASM.
**Tier:** none; tests are not tiered.
**Owns:** the regions each test reserves, for that test.
**Lifetime:** one test.
**Threads:** the test's main thread.
**Public functions:** none.
**May be called by:** ctest.
**Depends on:** `asm/src/vec_batch/`, `simd/reference`, `math/mat4`, `tests/asm/support/`, the generated
`struct_offsets/index.h`.
