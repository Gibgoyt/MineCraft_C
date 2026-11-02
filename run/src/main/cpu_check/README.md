# run/src/main/cpu_check

**Purpose:** compares what the CPU and OS offer, from CPUID and XCR0, with what this build was compiled to use.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main, at startup.
**Public functions:**

- `cpu_check_required`: the build's features: `MC_CPU_HAS_*`, plus what `-march` lets GCC use by itself.
- `cpu_check_read_raw`, `cpu_check_decode`, `cpu_check_detect`: what the CPU has. `decode` is separate so that tests can
  feed it CPUs and operating systems this machine is not.
- `cpu_check_feature_name`, `cpu_check_feature_from_name`, `cpu_check_names`: names, the same as the yaml's keys.
- `cpu_check_message`: the sentence for a person.

**May be called by:** `run/src/main/`.
**Depends on:** `asm/src/cpuid/` through `run/include/simd/cpuid/`, and the generated `machine/index.h`.

Only a missing feature is a mismatch. A CPU with more than the build uses, such as AVX-512 with `avx512: false`, runs the
build correctly.

**What it cannot catch:** this code and `main` are themselves compiled with `-march=<cpu.arch_flag>`, and the release
build's `app_init` does use AVX and BMI1. On a CPU older than the flag, the program can die of SIGILL before printing the
message. See Stage 03.1 issue 1.
