# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 6: arm64 readiness (design only)

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012; may be written any time after phase 02.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md`, then the master
`plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 3, 4 and Q8), then phases 01 and 02 (the arch
define and the layer this mirrors).

## Goal

**When there is an arm64 machine, adding its assembly is a known list of steps, and nothing in the x86-64 tree has to be
rethought.** This phase writes and reviews that design, here in this file. It builds nothing, and adds no file outside
`plans/`. It is done when Ahmed has read the design below and either accepted it or changed it, with his changes
recorded.

## Where this fits

**The vision.** Ahmed: "as of right now, we are only writing x86-64 assembly, whenever we have arm64 in front of us, we
will add its respective assembly, but please make it extremely easy to also have at compile time definition arch
x86-64".

**What it reads:**
- phase 01's `MC_PLATFORM_ARCH_ARM64`, today refused at configure with a sentence;
- phase 02's layer, whose shape (one line of contract, one closing macro) the arm64 side copies.

**What later reads it:** a future plan, written when the hardware exists.

**What this phase must NOT build:**
- No `.S` file, no CMake, no lint change, no yaml key. Every change below is described, not made.
- No NEON intrinsics, ever: `arm_neon.h` stays banned (`compile/src/standards_lint/rules_include/index.c:25`).

## Ahmed's words

- "as of right now, we are only writing x86-64 assembly, whenever we have arm64 in front of us, we will add its respective
  assembly, but please make it extremely easy to also have at compile time definition arch x86-64"

---

## The design

### 1. File shape

| Today | arm64 addition |
|---|---|
| `asm/src/<k>/index.asm`: NASM, x86-64 | `asm/src/<k>/index.aarch64.S`: GNU as through the C preprocessor, AArch64 |
| `asm/include/<k>/index.inc`: NASM macros | `asm/include/<k>/index.aarch64.S`: cpp-included macros (Q8 and Q-A below) |
| `asm/include/conventions/index.inc`: `MC_FUNC` | `asm/include/conventions/index.aarch64.S`: `function` / `endfunc` |

- **Why GAS and not NASM:** NASM has no AArch64.
- **Why `.S`:** the C preprocessor brings in `machine/index.h` the same way C does, so `MC_CPU_HAS_*` and
  `MC_PLATFORM_*` reach the arm64 assembly with no new generator format.
- **Why the `aarch64` infix:** `index.<ext>` stays the one source file a directory holds per architecture, and the name
  says which.

### 2. The conventions layer, arm64 side

Modelled on dav1d's `src/arm/asm.S` (https://code.videolan.org/videolan/dav1d/-/raw/master/src/arm/asm.S, fetched
2026-11-17):

```gas
.macro function name, export=0, align=2
    .macro endfunc
#ifdef __ELF__
        .size   \name, . - \name
#endif
        .purgem endfunc
    .endm
        .text
        .align \align
    .if \export
        .global EXTERN\name
#ifdef __ELF__
        .type   EXTERN\name, %function
        .hidden EXTERN\name
#elif defined(__MACH__)
        .private_extern EXTERN\name
#endif
EXTERN\name:
    ...
```

- In dav1d, `EXTERN` is `_` plus the prefix under `PREFIX` (Mach-O) and the bare prefix otherwise. Ours is empty on
  Linux, so the name is the same as on x86-64 and the C prototype does not change.
- Exported functions also start with `AARCH64_VALID_CALL_TARGET` (`bti c`) when branch protection is on, as dav1d does.
- **Arguments need no renaming.** AAPCS64 passes x0-x7 on Linux and on Windows on Arm alike (master section 3), so the
  arm64 layer is smaller than x86-64's: no register table.
- **What it saves:** x19-x29 past the volatile set, and the low 64 bits of v8-v15 ("only the bottom 64 bits of each value
  stored in v8-v15 need to be Callee-saved", AAPCS64). Saves are `stp d8, d9, [sp, #-16]!` pairs, keeping `sp` 16-aligned
  ("SP mod 16 = 0").
- **The contract line:** `function name, export=1` plus a `MC_NVEC`-style count, so the save is automatic, like
  `MC_FUNC`'s.
- **No `vzeroupper`:** NEON has no upper-state penalty, so there is nothing to emit.

### 3. Build selection (`cmake/`)

- `project(minecraft_c LANGUAGES C ASM_NASM CUDA)` (`CMakeLists.txt:4`) becomes `C CUDA`, then
  `enable_language(ASM_NASM)` when `MC_PLATFORM_ARCH_X86_64`, or `enable_language(ASM)` (GCC as the assembler) when
  `MC_PLATFORM_ARCH_ARM64`.
- `cmake/targets.cmake:38`'s glob takes `asm/src/*/index.asm` on x86-64, and `asm/src/*/index.aarch64.S` on arm64.
- `cmake/flags.cmake:44`'s `-march=${MC_CPU_ARCH_FLAG}` stays for x86-64. On arm64 it becomes `-mcpu=${MC_CPU_ARCH_FLAG}`
  plus `-mbranch-protection=standard` if the distribution's toolchain defaults to it (to check on the machine).
- `cmake/check_link.cmake`'s allowed list names `ld-linux-x86-64.so.2` (`plans/REPO_STRUCTURE.md:886`); arm64 adds
  `ld-linux-aarch64.so.1`.

### 4. The C side and the fallback

- **Prototypes stay arch-neutral.** `run/include/simd/<k>/index.h` declares `noise3_batch8(...)` once, for every
  architecture. They already use only `float32_t`, integers and pointers.
- **A kernel with no arm64 source yet falls back to its reference.** A generated or hand-written
  `run/src/simd/fallback/index.c` defines `void noise3_batch8(...) { simd_reference_noise3(...); }` for each kernel
  CMake did not find an `index.aarch64.S` for. So arm64 builds and runs on day one, slowly, and each `.S` file added makes
  its kernel fast. The references already exist for copy, blend, bitmask, noise and vec_batch (`plans/REPO_STRUCTURE.md`
  15.6, line 885), or whatever phase 04 left.
- `bench/simd` then shows each fallback at 1.00x, which is the to-do list.

### 5. What has no arm64 form, and what replaces it

| x86-64 | arm64 |
|---|---|
| `asm/src/cpuid/` (CPUID, XGETBV) | Linux: `getauxval(AT_HWCAP)` / `AT_HWCAP2` from C. No assembly needed, and no inline asm |
| The `.preinit_array` guard, before `-march` code runs | The same idea: a `.preinit_array` entry in an `index.aarch64.S` that reads `AT_HWCAP` through the aux vector on the stack, or a guard in C compiled without `-mcpu` |
| `config/machine.yaml`'s `cpu.simd` keys (sse42 ... avx512) | New keys per arch, e.g. `cpu.simd.neon`, `dotprod`, `sve`. The generator refuses x86 keys under `arch: arm64` |
| `REQUIRE_CPU` in `conventions/index.inc` | `#if !MC_CPU_HAS_DOTPROD` / `#error` in the `.S`, through cpp |
| `asm_abi`'s probe (`asm/src/abi_probe/`) | An `index.aarch64.S` probe: random x19-x29 and d8-d15, canaries, `sp` alignment, as checkasm does on ARM64 ("registers, stack, VFP state", checkasm README) |
| `-fno-tree-vectorize -fno-tree-slp-vectorize` | The same flags. Our rule is the same on every arch |

### 6. The lint

- `rules_tree` (`compile/src/standards_lint/rules_tree/index.c:102-103`) lists `.s` and `.S` as foreign extensions, and
  `:389` requires `index.<extension>`.
  - **Change:** allow exactly `index.aarch64.S` under `asm/src/<k>/` and `asm/include/<k>/`, and nothing else with `.S`.
  - A directory may then hold `index.asm` and `index.aarch64.S`. The rule "only one `index.*` pair member" (`:408`) gains
    "per architecture".
- An aarch64 counterpart of `rules_asm`:
  - every `function` has its `endfunc`;
  - `.section .note.GNU-stack,"",%progbits` is present;
  - no hand-written `ret` inside a `function` that saves registers.
- `rules_include` keeps `arm_neon.h` banned (`:25`).

### 7. The order, when the hardware arrives

1. `platform.arch: arm64` accepted by phase 01's check; CMake selects `ASM`; every kernel falls back to C. The whole
   suite passes on arm64 with no assembly.
2. The arm64 conventions file and probe; `asm_abi` on arm64.
3. Kernels one at a time, in phase 05's order of profile share, each with its bench row.

---

## Owned paths

This file only.

## Steps

1. **Re-read the citations** above on the day: dav1d's `asm.S`, AAPCS64, checkasm's README. Record any change. *Why: the
   design quotes them.*
2. **Check the design against the tree as phase 02 left it**: the layer's names, `cmake/targets.cmake`'s glob, the lint's
   lines. *Why: the line numbers above are from 2026-11-17.*
3. **Ahmed's review**, then his changes recorded in Issues found. *Why: the design is only as good as its acceptance.*

## Todo

From the master (section 8, phase 06):

- [ ] File shape: `asm/src/<k>/index.asm` for x86-64, `index.aarch64.S` (GAS through cpp, dav1d `function`/`endfunc`) for arm64
- [ ] Build: CMake selects sources and the assembler language by `MC_PLATFORM_ARCH_*`
- [ ] Lint: `rules_tree` allows `.S` only as `index.aarch64.S`; the `arm_neon.h` ban stays
- [ ] C side: prototypes arch-neutral, references as the fallback
- [ ] Nothing built

Expanded:

- [ ] What has no arm64 form (cpuid, the guard, `cpu.simd` keys) and its replacement, reviewed
- [ ] Q8 and Q-A answered by Ahmed, or their defaults taken
- [ ] Citations re-checked on the day

## Tests

None: a design. The design's own tests are listed in its section 7 for the future plan.

## Verification

No build, test or code change in this phase.

    git diff --stat main..HEAD -- . ':!plans/'    # prints nothing: only plans/ changed
    git status --short                            # nothing uncommitted

## Gate

Ahmed reads "The design" and answers Q8 and Q-A. His answers go in Issues found with the date.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Q8: `index.aarch64.S` or another spelling | `index.aarch64.S` |
| Q-A: the arm64 include's extension: `index.aarch64.S` in `asm/include/` too, or `.h` | `index.aarch64.S`, so the lint has one rule for both directories |
| Windows on Arm or Apple silicon instead of Linux arm64 | The layer's `EXTERN` and `.private_extern` lines (dav1d's) cover Mach-O; Windows on Arm needs its own unwind format (not researched); recorded for that plan |
| The design goes stale before hardware arrives | Step 2 is repeated by the future plan before it starts |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
