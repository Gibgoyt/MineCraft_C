# asm/src/cpuid

**Purpose:** runs the CPUID and XGETBV instructions, which C cannot reach because inline asm is banned.
**Tier:** Borrow in spirit: it uses the pointers and counts it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main, at startup.
**Public functions** (`run/include/simd/cpuid/index.h`):

- `cpuid_read_batch`: runs CPUID for an array of leaf and subleaf pairs.
- `cpuid_xgetbv_batch`: runs XGETBV for an array of extended control registers.

**May be called by:** `run/src/main/cpu_check/`.
**Depends on:** nothing. It uses no SIMD register, since it runs before the CPU has been checked.
