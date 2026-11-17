# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 3: A checkasm-grade harness

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012, and for phase 02.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md` (Issues found 10
and 11), then the master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 1.2, 1.3 item 6, 3, 4,
and Q5), then phase 02's file ("How the layer is tested on Linux without Windows").

## Goal

**`asm_abi` catches every calling-convention mistake checkasm catches on x86-64, for both ABIs, on every exported asm
function, and a kernel added without a check fails the test.**

- **Random callee-saved values.** rbx, rbp and r12-r15 get fresh random 64-bit values each run, from a seed the test
  prints, and are checked on return.
- **Win64, run on Linux.** Every kernel is assembled a second time with phase 02's `-DMC_ABI_FORCE_WIN64` and called with
  the Win64 convention. rdi, rsi and xmm6-xmm15 are checked too, and the output must equal the SysV build's, byte for byte.
- **Narrow arguments get garbage above them.** Each 32-bit or narrower argument gets `0xdeadbeef` above its width, as
  checkasm's `CLOBBER_UPPER` does, and the output must not change. Today that is `noise3_batch8`'s `uint32_t seed` and
  `bitmask_from_equal_u16_batch`'s `uint16_t value`.
- **Stack canaries.** 8 words (64 bytes) above the return address and the stack arguments are checked after the call.
- **Entry alignment.** Each kernel runs at both legal 16-byte entry alignments: `rsp + 8` a multiple of 32, and 16 more
  than one. A kernel that assumes 32 faults or differs.
- **Flags.** DF is clear and MXCSR's control bits (6-15) are unchanged on return.
- **No dirty YMM** (existing, XINUSE).
- **Complete by construction.** The list of functions comes from the headers: a generated catalog of every prototype in
  `run/include/simd/*/index.h`. Each needs an argument recipe in the test, and a prototype without one fails
  `every_exported_function_has_a_recipe`.
- **The checker is checked.** A test-only object holds 7 deliberately broken functions, one per mistake, and each must
  fail exactly its own check (checkasm's README shows the same idea).
- **`bench/simd` counts cycles.** `rdtsc` comes through a new batch function `cpuid_tsc_read_batch` (Q5). Each row prints
  TSC ticks per element beside ns per element, with the TSC frequency measured in the same run. Rows are appended to
  `bench/simd/history.tsv`, as `bench/world/history.tsv` keeps its runs.

## Where this fits

**The vision.** "I am not sure if we are properly writing assembly SIMD!" This phase is what makes the answer checkable
by a machine, on every build, for both ABIs, without a Windows machine.

**What it reads from phase 02:**
- the layer;
- the `MC_ABI_FORCE_WIN64` mode, which picks the Win64 table while writing ELF and gives the symbols a `_win64` suffix
  so both builds link into one test.

**What later phases read from it:** phases 04 and 05 add or remove kernels, and this test refuses a kernel without a
recipe; `history.tsv` holds the before and after of every kernel decision.

**What this phase must NOT build:**
- No change to any kernel's code. If the harness finds a real bug, the smallest fix lands in a separate commit, recorded
  here.
- No dependency on the checkasm library. We read its design, not its code (BSD-2 would allow it, but the tree has no
  third-party code, `plans/CODING_STANDARD.md` section 1).
- No MMX/`emms` check: MMX is banned by lint (`rules_asm/index.c:265-268`).
- No cycle counting in the game. `cpuid_tsc_read_batch` is for benches and tests.

## Ahmed's words

- "I am not sure if we are properly writing assembly SIMD!"
- "We should always store findings in plan files, as opposed to keeping it inside Claude's GPU's memory where we are unable
  to access it! You need to document your findings"

---

## Background, explained

### Today's harness (`tests/asm/abi/index.c`, 176 lines)

- Each kernel is entered by `setcontext` with fixed patterns in rbx, rbp and r12-r15 (`:25-30`, `:88-93`), and the stack
  aligned the one ABI way (`:73`).
- Its `ret` lands on `abi_landing`, which saves the registers with `getcontext` before C touches them (`:66-69`), and reads
  `XGETBV(1)` (`:53-59`).
- It checks 6 GPRs and XINUSE bit 2 (`:108-114`) for 15 kernels × 4 counts {0, 3, 9, 200} (`:155-172`).
- It cannot check xmm6-15: glibc's `setcontext` does not load the XMM registers. So the Win64 path needs a probe of our
  own.

### The probe: `asm/src/abi_probe/`

checkasm's `checked_call` is the model (`tests/checkasm/ext/src/x86/checkasm.asm`, master section 4). Ours is written with
phase 02's layer, as a test-only object:

| Function | What it does |
|---|---|
| `abi_probe_call_sysv_batch(const abi_probe_t *calls, uint64_t count)` | For each call: load the record's random values into rbx, rbp and r12-r15; set the stack to the record's alignment; write 8 canary words; load up to 6 arguments; call; then record the registers, canaries, EFLAGS.DF, MXCSR and XINUSE in the record |
| `abi_probe_call_win64_batch(...)` | The same with the Win64 convention: rcx, rdx, r8 and r9; arguments 5 and 6 at `[rsp + 32]`, `[rsp + 40]` above 32 bytes of shadow space; random rdi, rsi and xmm6-xmm15 as well |

- **Arrays, per the standard.** One record per call (`abi_probe_t`), with offsets from `compile/src/struct_offsets/`
  (`plans/CODING_STANDARD.md` section 7: "Numbers are never typed by hand").
- **Not linked into the game.** It goes in its own object library that only `asm_abi` links. `cmake/targets.cmake:38`
  globs `asm/src/*/index.asm` into `mc_asm`, so the glob excludes `abi_probe`.
- **Why asm, not C:** only asm can set xmm6-15 and the flags exactly at the call, and inline asm is banned (`rules_c`).
  It is the second non-SIMD directory after `cpuid`, and section 7 of the standard says so.

### The catalog: `compile/src/simd_catalog/`

A small tool, run at build time like the other generators. It reads each `run/include/simd/*/index.h` and writes
`generated/simd_catalog/index.h`: one X-macro row per prototype, giving the name, the return kind, and each parameter's
kind (pointer, `uint64_t`, `uint32_t`, `uint16_t`, ...).

`asm_abi` uses it two ways:
- **the narrow-argument mask** comes from the parameter kinds, with no hand list;
- **the recipe check**: the test keeps a table of argument recipes by name (buffer sizes and counts, which only a person
  can choose), and fails if a catalog row has no recipe or a recipe has no row.

Today that is 21 rows: the 15 SIMD kernels, `cpuid_read_batch`, `cpuid_xgetbv_batch`, `cpuid_decode_batch`,
`cpuid_guard_batch` (with a required mask of 0, so it returns), `cpuid_guard_preinit`, and the new `cpuid_tsc_read_batch`.

### The bench's cycles

`cpuid_tsc_read_batch(uint64_t *out, uint64_t count)` runs `lfence; rdtsc` per element, `MC_INIT_GPR`, in
`asm/src/cpuid/`.

The TSC counts reference cycles at a fixed rate, not core cycles. So `bench_simd` measures the TSC rate against
`time_now_ns()` over the whole run and prints it, as `bench/world/shake` records the card's clock beside its cycles.

`history.tsv` gets one line per row:

    date  label  preset  commit  kernel  input  kernel_ns  scalar_ns  kernel_tsc  scalar_tsc  gain  tsc_mhz  gated  pass

## Owned paths

This phase may edit these, plus this file:
- `tests/asm/abi/` (`index.c`, `README.md`, `test.cmake`, a new `fixtures/` with the broken functions' source)
- `asm/src/abi_probe/`, `asm/include/abi_probe/`, new, with READMEs; and the test-only broken functions, also in `asm/`,
  under `asm/src/abi_probe/` as their own exported names
- `compile/src/simd_catalog/`, `compile/include/simd_catalog/`, new, with README; `tests/compile/simd_catalog/`
- `compile/src/struct_offsets/` (the `abi_probe_t` row only)
- `asm/src/cpuid/index.asm` and `run/include/simd/cpuid/index.h` (`cpuid_tsc_read_batch` only), their READMEs, and
  `tests/asm/cpuid/` if it exists, else `tests/run/main/cpu_check/`'s test of it
- `bench/simd/` (`index.c`, `README.md`, new `history.tsv`)
- `cmake/targets.cmake` (the catalog's generation, the probe's object library, the `MC_ABI_FORCE_WIN64` objects for
  `asm_abi` only) and `cmake/generated.cmake` if the catalog belongs there
- `plans/CODING_STANDARD.md` section 7 (the second non-SIMD directory, one sentence)

**Not touched:** any kernel's code, `run/src/simd/`, `run/src/simd/reference/`.

## Steps

1. **Confirm the ground.**
   - Does glibc's `getcontext` store MXCSR and EFLAGS where `ucontext_t` exposes them (`uc_mcontext.fpregs->mxcsr`,
     `gregs[REG_EFL]`)? Record the answer.
   - The probe makes it moot, but the existing landing code keeps working meanwhile.
   - *Why: a check built on a field that is never written passes forever.*
2. **The catalog tool** and its test. *Why: completeness first; everything else iterates over it.*
3. **The probe**, both ABIs, with its records and offsets. *Why: the only way to set and read xmm6-15 and the flags at the
   call.*
4. **The broken fixtures**, 7 of them, before the real checks. Each must make its check fail and no other.
   *Why: a check that has never failed has never been tested.*

   | Fixture | Mistake |
   |---|---|
   | `abi_bad_clobbers_r12` | Writes r12 |
   | `abi_bad_dirty_ymm` | Returns with `vzeroupper` skipped |
   | `abi_bad_writes_canary` | Stores 8 bytes above its return address |
   | `abi_bad_win64_xmm7` | Writes xmm7 under the Win64 table |
   | `abi_bad_aligned_stack` | `vmovaps` to `[rsp - 32]` (32-aligned only at one of the two entries) |
   | `abi_bad_sets_df` | `std` before `ret` |
   | `abi_bad_reads_upper` | Uses all 64 bits of a `uint32_t` argument |

   All of them are written in raw instructions, outside `MC_FUNC`, so the lint must list them as allowed. The breach
   names are in the lint's README.
5. **`asm_abi` rewritten** over catalog × recipes × {SysV, Win64} × counts {0, 3, 9, 200} × alignments {0, 16 mod 32} ×
   3 seeds. The seed is printed, and settable with `--seed`. *Why: the same counts as today, so nothing gets weaker.*
6. **`cpuid_tsc_read_batch`** and the bench's cycle columns and `history.tsv`, then one release run recorded as the first
   history line. *Why: a before line for phases 04 and 05.*

## Todo

From the master (section 8, phase 03):

- [ ] Random callee-saved values, per-run seed printed
- [ ] Upper bits clobbered on every narrow argument
- [ ] Stack canaries
- [ ] Both 16-byte entry alignments
- [ ] DF and MXCSR control bits checked
- [ ] The kernel list generated from the headers (`compile/src/simd_catalog/`), with a recipe check
- [ ] `bench/simd`: TSC ticks per element, TSC rate measured, `history.tsv`

Expanded:

- [ ] `asm/src/abi_probe/`: SysV and Win64 checked calls, test-only object
- [ ] The Win64 convention run on Linux through `MC_ABI_FORCE_WIN64` objects; outputs equal SysV's
- [ ] 7 broken fixtures, each caught by exactly its check
- [ ] The `cpuid_*` functions in the list (phase 00 Issues found 11)
- [ ] The first `history.tsv` line, release, under the lock
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test (ctest) | Checks | Numbers | Fails when |
|---|---|---|---|
| `asm_abi` `every_exported_function_has_a_recipe` | Catalog against recipes | 21 rows, 21 recipes, 0 unmatched | A kernel added without a check |
| `asm_abi` `callee_saved_registers_survive_random_values` | rbx, rbp, r12-r15 (and rdi, rsi, xmm6-15 for Win64) | 21 functions × 4 counts × 2 alignments × 3 seeds on SysV; 15 kernels × the same on Win64 | Any register differs |
| `asm_abi` `narrow_arguments_ignore_their_upper_bits` | Catalog-found narrow arguments | 2 today (`seed`, `value`), × 4 counts × both ABIs; output bytes equal clean vs clobbered | A kernel reads garbage |
| `asm_abi` `the_stack_above_the_frame_is_untouched` | 8 canary words | every call above | A write past the frame |
| `asm_abi` `either_entry_alignment_gives_the_same_bytes` | `rsp + 8` ≡ 0 and 16 (mod 32) | every call above; output equal; no fault | A hidden 32-byte assumption |
| `asm_abi` `df_is_clear_and_mxcsr_control_is_kept` | EFLAGS bit 10, MXCSR bits 6-15 | every call above | A flag left changed |
| `asm_abi` `no_dirty_ymm` (existing check, kept) | XINUSE bit 2 | every call above | A missing `vzeroupper` |
| `asm_abi` `win64_output_equals_sysv_output` | Same inputs, both builds | 15 kernels × 4 counts | The Win64 table mis-maps an argument |
| `asm_abi` `each_broken_fixture_fails_only_its_own_check` | The 7 fixtures | 7 × exactly 1 failing check, other checks pass | A check that cannot fail |
| `asm_abi` `this_cpu_can_report_dirty_ymm_state` (existing) | CPUID.(0xD,1):EAX bit 2 | 1 | |
| `compile_simd_catalog` | Fixture headers: plain, multi-line, `const`, narrow types; a malformed prototype refused | 5 cases | The catalog misreads a header |
| `bench_simd` (release, not a ctest) | Cycle columns and history | 1 line per row appended; TSC rate within 1% of `/proc/cpuinfo`'s `tsc` flag rate if printed, else recorded | |

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(asm_abi|compile_simd_catalog|asm_.*)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 900 ctest --preset release -R '^asm_abi$' --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_simd
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed:
1. Runs `tools/test-or-build.sh timeout 900 ctest --preset debug -R '^asm_abi$' --output-on-failure -V` and reads the seed
   and the 7 "caught" lines.
2. Reads the bench table with its new cycle column and the first `history.tsv` line.

He says whether this answers "are we writing SIMD properly" well enough to trust it on every build.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Q5: where `rdtsc` lives | `cpuid_tsc_read_batch` in `asm/src/cpuid/` |
| The probe's stack setup is itself wrong | The broken fixtures prove each check; the SysV probe must reproduce today's 6-register result on today's kernels before anything else |
| `MC_ABI_FORCE_WIN64` objects need distinct symbol names | Phase 02's layer suffixes `_win64` in that mode; recorded if phase 02 did it otherwise |
| Narrow types: SysV callers in practice extend 8- and 16-bit arguments to 32 bits, though the psABI says the bits are "unspecified" | Clobber anyway, as checkasm does for `int`; a kernel must not depend on a compiler's habit |
| The TSC is not core cycles under turbo | Print both ns and TSC ticks, and the measured TSC rate; never call TSC ticks "cycles" in the README |
| A real bug found in a kernel | Smallest fix, own commit, recorded here with the failing check's output |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
