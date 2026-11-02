# asm/src/cpuid

**Purpose:** runs the CPUID and XGETBV instructions, which C cannot reach because inline asm is banned, and refuses a CPU
that lacks what the build needs before any code compiled with `-march` can run.
**Tier:** Borrow in spirit: it uses the pointers and counts it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main, at startup.
**Public functions** (`run/include/simd/cpuid/index.h`):

- `cpuid_read_batch`: runs CPUID for an array of leaf and subleaf pairs.
- `cpuid_xgetbv_batch`: runs XGETBV for an array of extended control registers.
- `cpuid_decode_batch`: turns raw records into `CPU_FEATURE_*` bits. The only copy of that decision.
- `cpuid_guard_batch`: returns if the CPU has every feature asked for; otherwise says which are missing on stderr and exits
  with status 3, through system calls alone.
- `cpuid_guard_preinit`: `cpuid_guard_batch` of `cpuid_guard_required`, the yaml's `cpu.simd`. It sits in `.preinit_array`,
  so the dynamic loader runs it first, in every program that links this. Nothing calls it from C.

**May be called by:** `run/src/main/cpu_check/`, and the dynamic loader.
**Depends on:** nothing, past the generated `machine/index.inc`. It uses no SIMD register and nothing past the x86-64
baseline, since it runs before the CPU has been checked.
