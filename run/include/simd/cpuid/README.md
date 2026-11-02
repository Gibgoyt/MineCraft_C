# run/include/simd/cpuid

**Purpose:** C prototypes of the NASM functions in `asm/src/cpuid/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main, at startup.
**Public functions:** `cpuid_read_batch`, `cpuid_xgetbv_batch`.
**May be called by:** `run/src/main/cpu_check/`.
**Depends on:** `asm/src/cpuid/`.
