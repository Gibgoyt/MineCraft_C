# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 1: Types, assert, log and main

**Status:** done, with one known limit that Ahmed may want closed (issue 1)   (updated 2026-11-02)

## Goal

Sanctioned types, a CPUID check against build flags, and the entry point exist.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/types/`
- `run/{src,include}/assert/`
- `run/{src,include}/log/`
- `run/{src,include}/main/`
- `tests/run/types/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Define `float32_t` and `float64_t` in `run/include/types/index.h`, the single typedef site, using the Stage 01.1 result for CUDA
- [x] Implement `run/src/assert/index.c` and `run/src/log/index.c`
- [x] Write `run/src/main/index.c` with `int32_t main(int32_t, char **)` and `app_t app` on its stack
- [x] Check CPUID against the build's SIMD flags at startup, and exit with a clear message on mismatch (on any CPU that gets as far as the check: issue 1)
- [x] Add `tests/run/types/index.c`, registered as ctest `run_types`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_types$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

From a clean tree, `tools/test-or-build.sh cmake --preset debug` has to come first (issue 8).

### Results, 2026-11-02

GCC 16.2, nvcc 13.4.92, on the i9-10900K. From a deleted `build/`, the four commands above all pass, and `pgrep -a minecraft_c` prints nothing (rc 1).

- `run_types`: 38 tests, 0 failed, 1483 checks. Full `ctest` passes 8 of 8 on all four presets: debug, sanitize, release and profile.
- `minecraft_c` `NEEDED`: `libm.so.6`, `libc.so.6`, `ld-linux-x86-64.so.2`.

Real runs of the debug binary:

    $ minecraft_c                             rc 0
    [info] main/index.c:97: core runtime started and stopped; no game systems exist yet
    $ minecraft_c --report                    rc 0
    MC_THREADS_WORKERS=16 ... cpu required: sse42 avx avx2 fma3 bmi1 bmi2 popcnt lzcnt f16c ... cpu missing: none
    $ minecraft_c --pretend-missing-cpu avx2  rc 3
    [error] main/index.c:57: this CPU does not support what this build was compiled to use. Missing: avx2. Build flags: -march=skylake and cpu.simd in config/machine.yaml. Change them to match this CPU and rebuild.

A real mismatch, not a pretend one: with `avx512: true` temporarily in `config/machine.yaml` and a rebuild, this CPU, which has no AVX-512, gave rc 3 and `Missing: avx512.`. The yaml was restored.

`detect_matches_proc_cpuinfo_on_this_machine` compares our CPUID reading with the kernel's flags, feature by feature, and `cpuid_vendor_matches_the_kernel` does the same for the vendor string.

### Does the test catch anything?

Each row broke one thing, rebuilt, and ran `run_types`. All were restored.

| Break | Result |
|---|---|
| Format attribute off `log_write` | `log_macro_rejects_a_bare_float64`, `log_macro_rejects_a_wrong_type` fail |
| `F32` is plain `x` in C | build fails: the `_Generic` `static_assert` |
| `F32` is `x##f32` under nvcc | `types_header_compiles_on_both_sides_under_nvcc` fails, nvcc exit 2 |
| AVX2 read from bit 6 | `decode_reads_every_bit` fails |
| XCR0 ignored | `decode_wants_the_os_to_save_ymm` fails |
| Leaf 7 trusted past the maximum | `decode_does_not_trust_a_leaf_past_the_maximum` fails |
| `rbx` not saved in the NASM | `run_types` fails |
| `assert_fail` exits instead of aborting | the three assert tests that expect SIGABRT fail |
| Cut log line has no mark | both `log_cuts_*` tests fail |
| `main` ignores a mismatch | `game_exits_with_a_message_on_a_cpu_mismatch` fails, rc 0 and not 3 |

## Commits

Appended as they land:

- `e76cc61` Add the sanctioned types, assert, log and the real entry point with its CPUID check, and settle the printf rule.

## Issues found

**1. Known limit: on a CPU older than `-march`, the check can die before it speaks.**
- Expected: any mismatch exits with the clear message.
- Actual: `main`, `app_init` and the rest are built with `-march=skylake` like everything else. `objdump -d build/release/bin/minecraft_c` finds `vmovdqu`, `vpxor`, `vzeroupper` and `andn` in `app_init`, `vmovaps` in `log_write` and `shlx` in `cpu_check_names`. So a release binary on a CPU without AVX or BMI1 (before 2013) would get SIGILL. A mismatch this code does reach is one like `avx512: true` above, or any feature the startup path happens not to use. Not run on such a CPU: there is none here, and no emulator.
- Done: left, and stated in `run/src/main/cpu_check/README.md` and the standard's section 7, because the fixes each cost something the plan did not ask for, and the target hardware is one known CPU. Inline asm, `target` attributes and per-file flags are all banned, so C cannot avoid this on its own. The way to close it is a small NASM guard in `.preinit_array` that repeats the check using only baseline instructions, then calls `write` and `exit_group`. **Ahmed's decision.**

**2. Wiring outside this stage's owned paths.**
- `asm/src/cpuid/` and `run/include/simd/cpuid/`, new. CPUID needs an instruction, and inline asm is banned in C; `<cpuid.h>` is that inline asm in a header, and `__builtin_cpu_supports` is GCC's code and not ours. `plans/REPO_STRUCTURE.md` sections 6 and 8 and the standard's section 7 list them. They take arrays, per section 7, and hand-typed struct offsets are avoided by using flat `uint32_t` arrays.
- `cmake/targets.cmake`: removed the hello `main` and `simd` branches and the `minecraft_c_hello` test. `cmake/hello/main/` and `cmake/hello/simd/` deleted, READMEs updated, as the task's "replace it" says.
- `plans/CODING_STANDARD.md`: the printf rule, below. `plans/REPO_STRUCTURE.md`: the new paths.
- `run/src/main/cpu_check/` is a part inside this stage's own path, because tests link everything but `run/src/main/index.c`.

**3. Between now and Stage 08.1, no ctest runs a CUDA kernel.**
- Expected: the toolchain stays exercised, as `cmake/targets.cmake` used to say.
- Actual: `minecraft_c_hello` was the only test that launched a kernel, and it went with the hello `main`. `cmake/hello/gpu/` still compiles and links, and `run_types` compiles `both_sides.cu` for host and device with the build's flags, but nothing runs on the GPU. NASM is still run, by `run_types`.
- Done: accepted. Keeping the old test would keep the program this stage was told to replace. Stage 08.1's tests restore it.

**4. Follow-ups from earlier stages, closed here.**
- Stage 01.1 issue 2 (`F32()`, `F64()`): in `run/include/types/index.h`, written as the standard has them. Checked by `static_assert` on type and by value in C, and under nvcc in device arithmetic.
- Stage 01.1 issue 3 and Stage 01.3 issue 1 (`libc_double_t`, `float64_t` at printf): `libc_double_t` is defined, and section 2 of the standard now has "The printf rule". The compiler is asked directly: `fixtures/` holds the bare and cast forms, judged with the build's flags.
- New measurement for that rule: `float32_t` is rejected too. Stage 01.1 had tried only `_Float64`. `printf_float32_bare.c:6:14: error: format '%f' expects argument of type 'double', but argument 2 has type 'float32_t' {aka '_Float32'} [-Werror=format=]`.
- The `LOG_*` macros carry the same check through a format attribute, so the rule holds for them, which the mutation table shows.
- Stage 02.3 issue 2 (the binary reports its defines): `minecraft_c --report`.
- Stage 02.3 issue 8 (`MC_CPU_HAS_F16C` had no user): it does now.

**5. `libc_double_t` used as something other than a cast is not caught by a machine.**
- Expected: the standard to enforce "only at the call".
- Actual: `libc_double_t x;` compiles and passes the lint. Telling a cast from a declaration needs more than the token checks, and the lint is outside this stage's paths.
- Done: section 10 of the standard says review checks this. A token-level rule in `compile/src/standards_lint/rules_c/` would be a follow-up: reject `libc_double_t` unless it directly follows `(` and precedes `)`.

**6. `tests/run/types/` tests modules other than `types`.**
- Expected: `plans/REPO_STRUCTURE.md` section 10 has tests mirror source paths.
- Actual: this stage owns only `tests/run/types/` and its Verification names only `run_types`, so `assert`, `log`, `main/cpu_check` and the NASM are tested there. Section 10 now says so.
- Done: left. It is easy to split later, since each group is a section of one file.

**7. A mistake of mine in the first mutation run.**
- Expected: removing the format attribute with `sed` to fail the log tests.
- Actual: the test passed. The `[[` in the pattern was a `sed` bracket expression, so nothing had changed, and an `XCR0` row reported its pattern not found.
- Done: re-run with an exact string replacement. Both rows in the table above come from that run.

**8. Verification cannot start from clean as written.**
- Expected: the listed commands work from nothing.
- Actual: `cmake --build --preset debug` needs a configured `build/debug/`, which the list does not make.
- Done: noted under the list. The commands themselves are left as they were.

**9. `minecraft_c` does nothing else yet.**
- With no arguments it checks the CPU, logs that no game systems exist, and exits 0. Nothing exists for it to start until later stages. The KMS fail-fast belongs to Stage 05.1, whose code does not exist yet. The phase file's Stage 03.1 checkbox is left for the phase owner, since that file is outside this stage's paths.
