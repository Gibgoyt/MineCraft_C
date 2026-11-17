# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 0: The vision, and the plan files

**Status:** not started   (updated 2026-11-17). **Nothing past this phase starts until Ahmed says go.**

**Read this file before any other file of plan 012,** then the master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md`
(sections 0 to 9; Appendix A is the approved outline, verbatim).

## Goal

Two things are true when this phase is done:

1. Every agent working on plan 012 can say in a few sentences:
   - what Ahmed asked: are we writing SIMD assembly properly; Windows or Linux and x86-64 or (later) arm64 known at compile
     time, so each gets its own assembly; what FFmpeg's `cglobal` is, and whether we want one;
   - what the plan builds: compile-time platform defines (01), our own x86inc-style layer (02), a checkasm-grade harness
     (03), an audit of the kernels we have (04), new kernels where a profile says (05), an arm64 design (06), the check (07);
   - what it must not build (Part 4 below).
2. The plan files exist:
   - the master, with the findings written out and the outline verbatim;
   - phase files 00 to 07, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

## Where this fits

**The vision.** Ahmed: "We should always store findings in plan files, as opposed to keeping it inside Claude's GPU's memory
where we are unable to access it! You need to document your findings". This phase is that: the research lives in the repo,
with citations, before any code moves.

**What later phases read from it:** the scope, the rules, and the list of outline errors in Issues found, which every
phase must take as corrected.

**What this phase must NOT build:** any code, CMake, yaml or lint change. Only `plans/012-*`.

---

## Part 1: What Ahmed asked for

His words, 2026-11-17:
- "I am not sure if we are properly writing assembly SIMD!"
- "Please use compile time windows/linux detection so that we can optimize for either"
- "Can we please look at what `cglobal` is properly from ffmpeg assembly headers, maybe we can use something similar here!!"
- "as of right now, we are only writing x86-64 assembly, whenever we have arm64 in front of us, we will add its respective
  assembly, but please make it extremely easy to also have at compile time definition arch x86-64"
- "We should always store findings in plan files, as opposed to keeping it inside Claude's GPU's memory where we are unable
  to access it! You need to document your findings"

Decided by Ahmed, 2026-11-17: **our own small layer modelled on x86inc**, not a vendored `x86inc.asm`. **x86-64 only for
now**; arm64 is designed, not built.

## Part 2: The answer in one paragraph

Our SIMD is right for the one machine we build for: every kernel matches a scalar reference bit for bit, at test time and
at every start, and an ABI test catches clobbered registers and a missing `vzeroupper` (master 1.2). It is not portable,
and silently so: every code file already assembles as a Win64 object with no warning, and would then read its arguments
from the wrong registers, clobber the caller's xmm6-15 and carry no unwind data (master 1.3 items 10 and 11). FFmpeg's
`cglobal` solves the calling-convention half with tables of registers ordered volatile-first and macros that save only
what is used. It leaves out Win64 unwind data, and we will add that (master 2.10 to 2.12).

## Part 3: Rules every phase follows

1. **Every heavy command goes through `tools/test-or-build.sh`**, one command per call, with `timeout` inside the wrapper
   (`.claude/rules/test-or-build-mutex.md`). That includes `cmake --preset`, every build, every `ctest`, `bench_simd`, and
   a bare `nasm` used for a measurement. After tests, `pgrep -af "$PWD/build"` prints nothing. Never leave the game or a
   server running.
2. **Bit-identical or it does not land.** A ported or new kernel gives exactly its reference's bytes; terrain digests do
   not move (`run_terrain_*`, `run_save_world`).
3. **No gain, no kernel** (`plans/CODING_STANDARD.md:327`), measured in a release build under the lock.
4. **Never push. Never merge into main.**
5. **Commit as Ahmed, with `git commit -F -`**: one-sentence messages in the log's style, with no AI trailers (no
   `Co-Authored-By`, no "Generated with" line, no mention of the assistant or its vendor). After each commit,
   `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing. One commit per `plans/`
   file touched.
6. **Findings go in the phase file**, in Issues found, when they happen, with the evidence (README rule 4).
7. **Upstream code is read, not copied.** x86inc is ISC and checkasm BSD-2. We write our own; a phase that copies more
   than a few lines records the source and keeps its notice.

## Part 4: Out of scope

- **No Windows port of the rest of the game.** Display (KMS), input (evdev), threads (`pthread_setaffinity_np`, futex),
  files (`fsync`, `flock`, `dirent.h`), the heap (`mmap`), and `cmake/check_link.cmake` stay Linux. They are listed in
  `plans/REPO_STRUCTURE.md` section 15.6 (lines 875-887). Only `asm/`, the generated platform defines and the headers that
  read them become OS-aware.
- **No Windows replacement for `asm/src/cpuid/`'s guard.** Its `syscall`s and `.preinit_array` stay Linux. Phase 01 makes
  a Windows configure stop before it matters, and phase 06 writes down what a port would need.
- **No AVX-512**, no MMX (`plans/CODING_STANDARD.md:331`).
- **No runtime dispatch.** The ISA is fixed at compile time with the startup guard (master section 5).
- **No arm64 code.** Phase 06 is a design only.
- **No vendored x86inc.asm** (Ahmed's decision).
- **No change to intrinsics policy.** NASM stays the one way to write SIMD (master section 6).

## Part 5: The phases

| Phase | File | What is true when it is done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the master and phase files 01 to 07 exist |
| 01 | `...-phase-01-compile-time-os-and-arch-detection.md` | `platform.arch`; `MC_PLATFORM_OS_*`/`MC_PLATFORM_ARCH_*` everywhere; configure cross-checks the toolchain; NASM format follows the OS |
| 02 | `...-phase-02-our-own-cglobal-conventions-layer.md` | `MC_FUNC`/`MC_RET`/`MC_ENDFUNC`, both ABIs, Win64 unwind data; every kernel ported bit-identically |
| 03 | `...-phase-03-a-checkasm-grade-harness.md` | `asm_abi` as strict as checkasm on x86-64, plus Win64 conventions via `ms_abi`; cycle bench with history |
| 04 | `...-phase-04-audit-what-we-have.md` | 11 uncalled kernels wired or deleted; popcount's short arrays; the rodata question |
| 05 | `...-phase-05-kernels-where-the-profile-says.md` | Profile first; new kernels that beat their references and keep the world's bytes |
| 06 | `...-phase-06-arm64-readiness-design.md` | The arm64 design, reviewed; nothing built |
| 07 | `...-phase-07-the-final-check.md` | Full suites, bench before and after, Ahmed's gate, `REPO_STRUCTURE.md` |

Order: 01 before 02 (the layer reads the arch and OS); 02 before 03 (the harness checks the layer's Win64 output); 03
before 04 and 05 (every kernel kept or added is checked by it). 06 can be written any time after 02.

---

## Todo

- [ ] Read `plans/README.md`, `plans/CODING_STANDARD.md` section 7, `plans/REPO_STRUCTURE.md` sections 8, 11, 13 and 15,
      and `.claude/rules/test-or-build-mutex.md`
- [ ] Re-verify every repo fact the master cites (paths and lines) and every upstream quote, on the day
- [ ] The master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md`, committed alone, with the outline verbatim as Appendix A
- [ ] Phase files 01 to 07 from the template, every box unticked, status `not started`, each with a "Read first" line linking
      back to this file and the master, one commit each
- [ ] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found:
      `grep -L "## Issues found" plans/012-*phase-*.md` prints nothing
- [ ] Every heavy command in every Verification section is wrapped: `grep -n "test-or-build.sh" plans/012-*phase-*.md`
- [ ] The outline's errors recorded below, each with evidence
- [ ] Report the paths created

## Tests

None: no code changes. The checks are the Verification commands.

## Verification

No build, test or code change in this phase.

    ls plans/012-*
    grep -L "## Issues found" plans/012-*phase-*.md     # prints nothing
    grep -n "test-or-build.sh" plans/012-*phase-*.md    # every heavy command wrapped
    git log --format=%B -9 | grep -iE 'co-authored|anthropic|claude|opus|sonnet'   # prints nothing
    git status --short                                   # nothing uncommitted

## Gate

Ahmed reads the master's sections 1, 2.11, 3 and 7 and says go, or says what to change. A change is recorded here and in
the phase it touches. The master is not edited (README rule 1).

## Risks and open questions

| Risk or question | Default |
|---|---|
| A cited line moves before a phase starts | Each phase re-reads its citations first and records any drift in its Issues found |
| An upstream file changes (x86inc, checkasm, FFmpeg configure) | The master quotes what was fetched on 2026-11-17; a phase that relies on newer text quotes it again with its date |
| Q1 to Q8 (master section 9) | Their defaults, answered in the phase named |

## Commits

Appended as they land:

- (none yet)

## Issues found

Where the approved outline (master Appendix A) is wrong or incomplete, found while checking it on 2026-11-17. The master's
sections 1 to 6 already give the corrected facts; each phase takes these as corrected.

1. **"6 of 15 kernels have no game caller" is 11 of 15.**
   - **Expected:** 6 uncalled kernels.
   - **Found:** the outline's own list names 6 groups, which hold 11 kernels: `copy_stream_batch`, `blend_premul_batch`,
     `bitmask_popcount_batch`, `bitmask_and_batch`, `bitmask_or_batch`, `bitmask_andnot_batch`,
     `bitmask_from_nonzero_u8_batch`, `bitmask_from_equal_u16_batch`, `vec_batch_transform_points`,
     `vec_batch_transform_directions`, `vec_batch_mul_vec4`.
   - **Evidence:** grep for each symbol over `run/ cuda/ compile/ tools/`, outside `run/src/simd/` and
     `run/include/simd/`. The only callers are of the other 4 kernels: `copy_stream_rows_batch`
     (`run/src/game/session/index.c:396`, `run/src/render/frame/index.c:964,969`), `blend_over_batch`
     (`run/src/ui/compose/index.c:99`), `bitmask_from_nonzero_u16_batch` (`run/src/render/encode/index.c:104`,
     `run/src/ui/atlas/tile/index.c:125`) and `noise3_batch8` (`run/src/terrain/noise/index.c:155`).
   - **Done:** master 1.1 and phase 04 use 11.
2. **"Popcount is slower than scalar under 12 words" misses 15 words.**
   - **Found:** `bench/simd/README.md` (2026-11-02) has 12 words at 1.00x and 15 at 0.94x; the first win is 16 words at 1.07x.
   - **Done:** master 1.3 item 5 and phase 04 say "below 16".
3. **`MC_OS_*` / `MC_ARCH_*` would be a second naming scheme.**
   - **Found:** `compile/src/machine_config/index.c:450-475` (`machine_output_choices`) already writes
     `MC_PLATFORM_OS_<WORD>` for each allowed word of the choice key `platform.os` (`:99`). The generated `index.inc` holds
     `%define MC_PLATFORM_OS_LINUX 1` today. Adding `windows` to the word list makes `MC_PLATFORM_OS_WINDOWS`, and a choice
     key `platform.arch` with `x86_64,arm64` makes `MC_PLATFORM_ARCH_X86_64` and `MC_PLATFORM_ARCH_ARM64`, with no new
     code.
   - **Done:** phase 01 defaults to the generator's names (master Q1); Ahmed may choose otherwise at its gate.
4. **"There are zero `MC_PLATFORM_*` consumers" needs a qualifier.**
   - **Found:** `MC_PLATFORM_POSIX` is printed by `run/src/main/index.c:1158` and `compile/src/machine_config/report/index.c:13`,
     and `MC_PLATFORM_OS_LINUX` is checked by `tests/compile/machine_config/index.c:404`. Nothing branches on either.
   - **Done:** master 1.3 item 2 says so.
5. **"Kernels stay leaf where possible" holds for almost none of them on Win64.**
   - **Found:** Microsoft defines a leaf as a function that changes no nonvolatile register, xmm6-15 and rsp included
     (master 2.10). Copy pushes rbx; bitmask uses ymm6-7; blend, noise and vec_batch use ymm6-15 (master 1.1). Each is
     nonleaf on Win64.
   - **Done:** phase 02 emits unwind data for every function whose Win64 prologue saves or allocates anything, not as an
     exception.
6. **"Constants move to rodata if `MC_RODATA` fixes the relocation warning": it will not, on its own.**
   - **Found:** the error is NASM's `reloc-rel-dword` warning class under `-w+all -Werror`. A scratch file with
     `vmovdqa ymm0, [k]` and `k` in `.rodata` fails with "32-bit relative section-crossing relocation
     [-w+error=reloc-rel-dword]" under both `-f elf64` and `-f win64` (NASM 3.02, through `tools/test-or-build.sh`,
     2026-11-17). Renaming the section to `.rdata` changes nothing.
   - **Done:** phase 04 measures first, and moves constants only with a scoped `[warning -reloc-rel-dword]`, as
     `asm/src/cpuid/index.asm:269-271` already does for `reloc-abs-qword`.
7. **The outline's test "assembles every kernel under both `-f elf64` and `-f win64`" proves nothing today.**
   - **Found:** every code file already does (master 1.3 item 10), wrong code and all.
   - **Done:** phase 02's test checks the Win64 object's contents (`.pdata`/`.xdata` present, decoded unwind codes matching
     the prologue), and phase 03 runs Win64-convention code through `ms_abi`.
8. **The outline's "Storsjö: hand-written about 2.5x whole-decoder" is not a figure in the post.**
   - **Found:** the post gives fps: asm about 450-460, GCC autovectorised without asm 185, without vectorisation 128.
     2.5x is 455 / 185, our arithmetic.
   - **Done:** master section 6 says so.
9. **The outline's "FFmpeg disables GCC autovectorisation" is out of date.**
   - **Found:** FFmpeg master `configure:8062-8088` (2026-11-17) adds `-fno-tree-vectorize` only for GCC < 13, two
     loongarch GCC ranges, and arches outside CI. x86 keeps GCC's default. Storsjö's 2024 sentence was true when written.
   - **Done:** master section 6 records both.
10. **checkasm's "random values in callee-saved registers" are fixed constants.**
    - **Found:** `n9:`..`n14:` and WIN64's `x6:`..`x15:` are `dq` literals ("just random numbers to reduce the chance of
      incidental match"); the per-run random seed drives the test inputs, not those registers.
    - **Done:** phase 03 seeds ours per run anyway, which costs nothing and is stricter.
11. **The ABI test also skips the `cpuid_*` functions**, beyond the outline's list of gaps.
    - **Found:** `tests/asm/abi/index.c:158-172` lists only the 15 SIMD kernels.
    - **Done:** phase 03's generated list includes every exported function.
