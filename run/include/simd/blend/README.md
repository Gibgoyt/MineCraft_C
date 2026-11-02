# run/include/simd/blend

**Purpose:** C prototypes of the NASM functions in `asm/src/blend/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `blend_over_batch`, `blend_premul_batch`.
**May be called by:** `ui`, `render`, `run/src/simd/`, tests and benchmarks.
**Depends on:** `asm/src/blend/`.
