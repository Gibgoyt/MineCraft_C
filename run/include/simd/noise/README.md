# run/include/simd/noise

**Purpose:** C prototype of the NASM function in `asm/src/noise/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `noise3_batch8`.
**May be called by:** `terrain`, `run/src/simd/`, tests and benchmarks.
**Depends on:** `asm/src/noise/`, `types`.
