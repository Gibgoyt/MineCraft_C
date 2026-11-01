# cmake/hello/simd

**Purpose:** proves NASM links, with an AVX2 batch sum in the standard's required form.
**Tier:** Borrow. Uses the pointer and length it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main.
**Public functions:** `mc_hello_simd_sum_batch`, adds up an array of `uint32_t`.
**May be called by:** `cmake/hello/main`.
**Depends on:** nothing.
