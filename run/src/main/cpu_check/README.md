# run/src/main/cpu_check

**Purpose:** compares what the CPU and OS offer, from CPUID and XCR0, with what this build was compiled to use.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main, at startup.
**Public functions:**

- `cpu_check_required`: the build's features: `MC_CPU_HAS_*`, plus what `-march` lets GCC use by itself.
- `cpu_check_read_raw`, `cpu_check_decode`, `cpu_check_detect`: what the CPU has. `decode` is separate so that tests can
  feed it CPUs and operating systems this machine is not. It calls `cpuid_decode_batch`, which the early guard uses too.
- `cpu_check_feature_name`, `cpu_check_feature_from_name`, `cpu_check_names`: names, the same as the yaml's keys.
- `cpu_check_message`: the sentence for a person.

**May be called by:** `run/src/main/`.
**Depends on:** `asm/src/cpuid/` through `run/include/simd/cpuid/`, and the generated `machine/index.h`.

Only a missing feature is a mismatch. A CPU with more than the build uses, such as AVX-512 with `avx512: false`, runs the
build correctly.

**Why it is not the first check:** this code and `main` are themselves compiled with `-march=<cpu.arch_flag>`, and the
release build's `app_init` does use AVX and BMI1. So on a CPU older than the flag, `cpuid_guard_preinit` in `asm/src/cpuid/`
has already refused to go on by the time this could run. This check is what `--report` and `--pretend-missing-cpu` use, and
it also covers what `-march` allows GCC beyond the yaml's list. See Stage 03.1 issue 1.
