# run/include/simd/reference

**Purpose:** the header of `run/src/simd/reference/`: scalar C versions of the kernels.
**Tier:** Arena, as its source.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `simd_reference_<kernel>`, one per kernel.
**May be called by:** `run/src/simd/`, `tests/asm/`, `bench/simd/`.
**Depends on:** `run/src/simd/reference/`.
