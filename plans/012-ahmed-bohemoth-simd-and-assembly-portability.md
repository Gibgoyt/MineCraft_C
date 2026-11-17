# 012-ahmed-bohemoth-simd-and-assembly-portability: SIMD and assembly portability

**Status:** approved 2026-11-17. Only the plan files exist so far, and nothing past phase 00 starts until Ahmed says go.
**Author:** Ahmed (the asks and decisions), with the research written down here so the whole team can read it.

**How to read this file.** Sections 1 to 6 are the findings. They are sections 1 to 6 of the approved outline, written out
in full and checked again against the tree and against the upstream sources on 2026-11-17. Every repository fact carries
its path and line, and every upstream fact carries a URL and a short quote. Sections 7 to 10 are for anyone writing a
kernel: the rules, the phases, the open questions and the sources. **Appendix A is the approved outline, verbatim**
(`plans/README.md` rule 1). Where a finding below disagrees with the outline, the finding is the corrected one, and phase
00's **Issues found** lists each disagreement with its evidence.

---

## 0. What Ahmed asked, and what he decided

His words, 2026-11-17:

- "I am not sure if we are properly writing assembly SIMD!"
- "Please use compile time windows/linux detection so that we can optimize for either"
- "Can we please look at what `cglobal` is properly from ffmpeg assembly headers, maybe we can use something similar here!!"
- "as of right now, we are only writing x86-64 assembly, whenever we have arm64 in front of us, we will add its respective
  assembly, but please make it extremely easy to also have at compile time definition arch x86-64"
- "We should always store findings in plan files, as opposed to keeping it inside Claude's GPU's memory where we are unable
  to access it! You need to document your findings"

Decided by Ahmed, 2026-11-17:

| Decision | Instead of |
|---|---|
| **Our own small macro layer, modelled on x86inc** (`asm/include/conventions/index.inc`) | Vendoring FFmpeg's `x86inc.asm` |
| **x86-64 only for now.** arm64 gets a clean hook and a written design, built when there is arm64 hardware | Writing arm64 kernels now |
| **The ISA stays fixed at compile time**, with the startup guard | Runtime dispatch through function pointers |

---

## 1. What we have today, and is it right?

**Short answer: yes for the one target we build: Linux, x86-64, AVX2, the System V calling convention.** It is not
portable, and the hard-coding is silent: section 1.4 shows that every kernel already assembles cleanly as a Win64 object
and would then be wrong at run time.

### 1.1 The inventory (read 2026-11-17)

15 SIMD kernels in 5 files, plus 5 non-SIMD functions and one exported data word (`cpuid_guard_required`) in `asm/src/cpuid/`, and the blobs in `asm/src/embed_assets/`.

| File | Exported functions | Game caller (outside `run/src/simd/`, tests, bench) | Vector registers | GPRs or stack it saves |
|---|---|---|---|---|
| `asm/src/copy/index.asm` | `copy_stream_batch`, `copy_stream_rows_batch` | rows: `run/src/game/session/index.c:396`, `run/src/render/frame/index.c:964,969`. `copy_stream_batch`: **none** | ymm0-3 | `push rbx` (`:30`). `copy_stream_batch` jumps into `copy_stream_rows_batch` (`:24`) |
| `asm/src/blend/index.asm` | `blend_over_batch`, `blend_premul_batch` | over: `run/src/ui/compose/index.c:99`. premul: **none** | ymm0-3 and ymm9-15 (`asm/include/blend/index.inc:11-17`) | none |
| `asm/src/bitmask/index.asm` | `bitmask_popcount_batch`, `_and_`, `_or_`, `_andnot_`, `_from_nonzero_u8_`, `_from_nonzero_u16_`, `_from_equal_u16_batch` | `from_nonzero_u16`: `run/src/render/encode/index.c:104`, `run/src/ui/atlas/tile/index.c:125`. The other 6: **none** | ymm0-7 | none |
| `asm/src/noise/index.asm` | `noise3_batch8` | `run/src/terrain/noise/index.c:155` | ymm0-15 (`asm/include/noise/index.inc:27-31`) | `push rbp`, `and rsp, -32`, 544 bytes (`:21-24`, `index.inc:52`) |
| `asm/src/vec_batch/index.asm` | `vec_batch_transform_points`, `_transform_directions`, `_mul_vec4` | **none** | ymm0-15 (`asm/include/vec_batch/index.inc:33-39`) | none |
| `asm/src/cpuid/index.asm` | `cpuid_read_batch`, `cpuid_xgetbv_batch`, `cpuid_decode_batch`, `cpuid_guard_batch`, `cpuid_guard_preinit`, `cpuid_guard_required` | `run/src/main/cpu_check/index.c` (the first three); the guard runs from `.preinit_array` | none (`:3`: "No YMM is used") | `push rbx` (`:13`); the guard pushes rbx, r12-r14 and calls `cpuid_decode_batch` (`:140-143`, `:185`) |

**11 of the 15 kernels have no game caller** (grep of `run/`, `cuda/`, `compile/`, `tools/` for each symbol, 2026-11-17):
`copy_stream_batch`, `blend_premul_batch`, `bitmask_popcount_batch`, `bitmask_and_batch`, `bitmask_or_batch`,
`bitmask_andnot_batch`, `bitmask_from_nonzero_u8_batch`, `bitmask_from_equal_u16_batch`, and all three `vec_batch_*`.
They are reached only from `run/src/simd/index.c` (the self-check), `tests/asm/` and `bench/simd/`. The outline said "6 of
15": that is the number of groups in its own list, which holds 11 kernels.

### 1.2 What is right

- **Bit-for-bit references.** Every SIMD kernel has a scalar C twin in `run/src/simd/reference/index.c`, compared byte for
  byte in `tests/asm/<kernel>/` (guard pages, misalignment, tails), and again at every start by `simd_self_check`
  (`run/src/simd/index.c:225-242`, called from `simd_init` at `run/src/main/index.c:576`). It uses 8 misalignments and a
  count 3 short of a full batch, so every tail path runs (`run/src/simd/index.c:24-25`).
- **An ABI test exists.** `tests/asm/abi/index.c` enters each kernel through `setcontext` with known values in rbx, rbp and
  r12-r15 (`:25-30`, `:88-93`), lands its `ret` on `abi_landing` before C can touch a register, checks them (`:108-113`),
  and reads `XGETBV(1)` bit 2 (XINUSE for the AVX state) to catch a missing `vzeroupper` (`:31`, `:57`, `:114`).
- **The lint knows the form.** `compile/src/standards_lint/rules_asm/index.c` follows YMM use through jumps and fails a
  `ret` with live YMM (`:150-206`), requires `default rel` and `.note.GNU-stack` (`:259-263`, `:278-284`), and bans `zmm`,
  MMX and `emms` (`:265-268`). `rules_c` bans inline asm, `_mm*_` intrinsics and `__builtin_ia32_*`
  (`compile/src/standards_lint/rules_c/index.c:69-79`), and `rules_include` bans `*intrin.h` and `arm_neon.h`
  (`compile/src/standards_lint/rules_include/index.c:24-25`).
- **No gain, no kernel.** `bench/simd/index.c` exits non-zero when a gated row shows no gain or a different output
  (`bench/simd/README.md`), as `plans/CODING_STANDARD.md:327` requires.
- **Choices are written down where they are made.** FMA in noise because the reference calls `fmaf`, and no gather because
  of the Downfall microcode (`asm/src/noise/index.asm:1-7`).
- **The startup guard checks the OS, not just the CPU.** `cpuid_decode_batch` only reports AVX when CPUID.1:ECX.OSXSAVE
  (bit 27) is set and XCR0 bits 1 and 2 are on (`asm/src/cpuid/index.asm:113-118`). That is the same check FFmpeg
  (`libavutil/x86/cpu.c:141-144`) and dav1d (`src/x86/cpu.c`, `X(r.ecx, 0x18000000)` then `X(xcr0, 0x00000006)`) make.

### 1.3 What is weak or missing

1. **SysV and ELF are hard-coded.**
   - `cmake/flags.cmake:25`: `set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)`.
   - `asm/include/conventions/index.inc:5-7`: "A kernel may use rax, rcx, rdx, rsi, rdi, r8-r11 and every YMM register
     freely". On Win64 that is wrong three times: rsi and rdi are nonvolatile, xmm6-xmm15 are nonvolatile, and the
     arguments arrive in rcx, rdx, r8, r9 (section 3).
   - `plans/CODING_STANDARD.md:353` states the SysV ABI as the rule.
   - `asm/src/cpuid/index.asm` writes to stderr and exits with raw Linux system calls (`SYS_WRITE 1`, `SYS_EXIT_GROUP 231`
     at `:71-72`; `syscall` at `:201`, `:211`, `:220`, `:223`) and runs from an ELF `.preinit_array` (`:268-272`).
2. **No OS or arch detection anywhere.**
   - No file outside `plans/` names `__linux__`, `_WIN32`, `__x86_64__`, `_M_X64`, `__aarch64__`, `CMAKE_SYSTEM_NAME`,
     `CMAKE_SYSTEM_PROCESSOR` or `__OUTPUT_FORMAT__` (grep, 2026-11-17).
   - `config/machine.yaml:1` is `platform: { os: linux, posix: true }`, and there is no arch key.
   - **The generator already makes per-word defines.** `platform.os` is a choice key (`compile/src/machine_config/index.c:99`),
     and `machine_output_choices` (`:450-475`) writes the macro plus `_<WORD>` for every allowed word. The generated
     `machine/index.inc` today holds `%define MC_PLATFORM_OS "linux"` and `%define MC_PLATFORM_OS_LINUX 1`. Nothing reads
     `MC_PLATFORM_OS_LINUX` but `tests/compile/machine_config/index.c:404`. `MC_PLATFORM_POSIX` is only printed
     (`run/src/main/index.c:1158`), as `plans/REPO_STRUCTURE.md` section 15.1 already says.
3. **No calling-convention layer.** Each kernel writes its own prologue (compare `asm/src/noise/index.asm:21-24` with
   `asm/src/copy/index.asm:30`). There is no `cglobal`, no argument names, no automatic saving, no automatic `vzeroupper`.
   The header says why: "a macro would hide them from it", meaning from `standards_lint` (`asm/include/conventions/index.inc:1-3`).
4. **11 of 15 kernels have no game caller** (1.1).
5. **Popcount loses to scalar below 16 words.** Measured 2026-11-02 (`bench/simd/README.md`): 1 word 0.46x, 2 0.50x, 4
   0.66x, 7 0.67x, 8 0.84x, 12 1.00x, 15 0.94x, 16 1.07x, 24 1.27x, 32 1.40x. The prototype says the kernel is "for 16
   words and up". The outline's "under 12 words" misses the 15-word row.
6. **The ABI test has gaps** (`tests/asm/abi/index.c`).
   - No check of stack alignment as the kernel sees it: the harness sets one alignment (`:73`) and never tries another.
   - No check of DF, of MXCSR's control bits, or of upper-32 garbage in `int` arguments. Only `noise3_batch8`'s seed is a
     32-bit argument today, and the harness passes it zero-extended (`:169`).
   - The callee-saved values are fixed constants, the same every run (`:25-30`).
   - The kernel list is written by hand (`:158-172`), and none of the `cpuid_*` functions are in it.
   - The bench times with `time_now_ns()` (`bench/simd/index.c:352-356`), with no cycle count.
7. **Hot loops are still scalar.**
   - Noise makes one kernel call per octave. Around it, scalar C narrows each coordinate from float64
     (`run/src/terrain/noise/index.c:151-153`) and accumulates `out[i] += value[i] * octave->amplitude` (`:156-158`),
     inside the per-octave loop at `:145-162`.
   - Candidates still to profile: render encode's brick scatter (`encode_bricks_from_occupied`,
     `run/src/render/encode/index.c:26`) and uniform scan; section palette pack and unpack (`run/src/world/section/`); save
     encode (`run/src/save/`); light sweeps (`run/src/world/light/`); LOD downsample (`run/src/render/lod/`); tick nibble
     unpack (`run/src/world/tick/`).
8. **Constants live in `.text`** (`asm/include/conventions/index.inc:9-10`). Checked 2026-11-17 with NASM 3.02 and a
   scratch file: a `vmovdqa ymm0, [k]` with `k` in `.rodata` fails under `-w+all -Werror` with "32-bit relative
   section-crossing relocation [-w+error=reloc-rel-dword]", and fails the same way under `-f win64`. x86inc's
   `SECTION_RODATA` would not change that: the error comes from the warning class, not the section's name (phase 04).
9. **The lint is blind to YMM inside macros.** `rules_asm` looks for the text `ymm` or `YMM` on each line (`:181-183`).
   - A line that uses YMM only through a macro, such as `BLEND_OVER_PIXELS` or a register define like `NOISE_REG_SIGN`,
     is not seen.
   - A line that names `YMM_BYTES` counts as YMM use even when it is only an `add`. That direction is harmless.
10. **New, found while checking: the hard-coding is silent.** All six code files (every `asm/src/*/index.asm` but `embed_assets`, which was not tried) assemble with `nasm -w+all -Werror -f
    win64` (NASM 3.02, 2026-11-17, through `tools/test-or-build.sh`), with no warning. The objects have no `.pdata` or
    `.xdata` section, and `cpuid`'s carries an ELF `.preinit_array` that no Windows loader will run. Such an object would
    link and then break: arguments read from the wrong registers, the caller's xmm6-15 clobbered, and no unwind data. So a
    Win64 build must be made to fail until phase 02 lands, not trusted because it assembles.
11. **New: on Win64 almost every kernel is "nonleaf".** Microsoft: "Leaf functions are functions that don't change any
    nonvolatile registers" (section 3). By that rule, copy (rbx), blend, bitmask, noise and vec_batch (xmm6 and up) would
    all need unwind data. "Kernels stay leaf where possible", the outline's plan, holds for almost none of them.

---

## 2. FFmpeg's `cglobal`, explained

All quotes are from https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/x86/x86inc.asm, fetched 2026-11-17
(1978 lines), unless another URL is given.

### 2.1 What x86inc is

> "This is a header file for the x86inc.asm assembly language, which uses NASM/YASM syntax combined with a large number of
> macros to provide easy abstraction between different calling conventions (x86_32, win64, linux64)."

- **Who uses it:** FFmpeg, x264 and dav1d. The asm-lessons call it "a lightweight abstraction layer used in FFmpeg, x264,
  and dav1d to make an assembly programmer's life easier"
  (https://raw.githubusercontent.com/FFmpeg/asm-lessons/main/lesson_01/index.md).
- **License: ISC.** "Copyright (C) 2005-2024 x264 project ... Permission to use, copy, modify, and/or distribute this
  software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this
  permission notice appear in all copies."
- **Its own limit:** "Currently this works only for leaf functions which load all their arguments into registers at the
  start, and make no other use of the stack. Luckily that covers most use cases."

### 2.2 The `cglobal` line

```nasm
cglobal name, nargs, ngprs, nvec[, stack_size], argnames...
```

From x86inc's own comment:

> "%1 = number of arguments. loads them from stack if needed. %2 = number of registers used. pushes callee-saved regs if
> needed. %3 = number of xmm registers used. pushes callee-saved xmm regs if needed. %4 = (optional) stack size to be
> allocated. ... If the required stack alignment is larger than the known stack alignment the stack will be manually aligned
> and an extra register will be allocated to hold the original stack pointer"

> "e.g. cglobal foo, 2,3,7,0x40, dst, src, tmp declares a function (foo) that automatically loads two arguments (dst and
> src) into registers, uses one additional register (tmp) plus 7 vector registers (m0-m6) and allocates 0x40 bytes of stack
> space."

What `cglobal` itself does (`%macro cglobal 1-2+`, `cglobal_internal`):
- it mangles the name, adding `private_prefix_` and, with `PREFIX`, a leading underscore;
- it appends the CPU suffix (`%1 %+ SUFFIX`, so `_avx2`);
- it emits `global name:function hidden` on ELF;
- it aligns, places the label, and resets the stack bookkeeping (`stack_offset`, `stack_size`, `xmm_regs_used`);
- it runs `PROLOGUE` with the rest of the line.

The asm-lessons example (lesson 1): `cglobal add_values, 2, 2, 2, src, src2`. In the lesson's words: "The next parameter
shows it has two function arguments. The parameter after that shows that we'll use two GPRs in this function, including
the arguments. The parameter after that tells x86util how many XMM registers we are going to use. The following two
parameters are labels for the function arguments."

### 2.3 The register tables: `DECLARE_REG`

`DECLARE_REG n, reg[, stack_offset]` makes `rN`, `rNq`, `rNd`, `rNw`, `rNb` and `rNm` (where argument N lives in memory).
Then `DEFINE_ARGS` aliases the names: with `src` as argument 0, `srcq` is `r0q`, `srcd` is `r0d`, and so on.

| rN | WIN64 | UNIX64 |
|---|---|---|
| r0 | rcx | rdi |
| r1 | rdx | rsi |
| r2 | R8 | rdx |
| r3 | R9 | rcx |
| r4 | R10, stack 40 | R8 |
| r5 | R11, stack 48 | R9 |
| r6 | rax, 56 | rax, 8 |
| r7 | rdi, 64 | R10, 16 |
| r8 | rsi, 72 | R11, 24 |
| r9 | rbx, 80 | rbx, 32 |
| r10 | rbp, 88 | rbp, 40 |
| r11-r14 | R14, R15, R12, R13 | R14, R15, R12, R13 |

The order is chosen so that **the low numbers are the volatile registers**. On WIN64, r0-r6 are volatile (7); on UNIX64,
r0-r8 are (9). On WIN64 the 5th argument is at `[rsp + 40]` on entry: 8 bytes of return address plus the 32-byte shadow
space.

### 2.4 `PROLOGUE` and `PUSH_IF_USED`

```nasm
; WIN64
    PUSH_IF_USED 7, 8, 9, 10, 11, 12, 13, 14
    ALLOC_STACK %4, %3
    %if mmsize != 8 && stack_size == 0
        WIN64_SPILL_XMM %3
    %endif
    LOAD_IF_USED 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
; UNIX64
    PUSH_IF_USED 9, 10, 11, 12, 13, 14
    ALLOC_STACK %4, %3
    LOAD_IF_USED 6, 7, 8, 9, 10, 11, 12, 13, 14
```

`PUSH_IF_USED` pushes `rN` only "`%if %1 < regs_used`". So **a function that asks for 7 GPRs on Win64, or 9 on SysV,
pushes nothing**. `LOAD_IF_USED` loads the arguments that came on the stack into their registers.

### 2.5 `WIN64_SPILL_XMM` and the shadow space

> "Use the shadow space to store XMM6 and XMM7, the rest needs stack space allocated."

```nasm
%assign %%pad %%xmm_regs_on_stack*16 + 32
%assign stack_size_padded %%pad + ((-%%pad-stack_offset-gprsize) & (STACK_ALIGNMENT-1))
SUB rsp, stack_size_padded
...
movaps [rstk + stack_offset +  8], xmm6
movaps [rstk + stack_offset + 24], xmm7
...
movaps [rsp + (%%i-8)*16 + stack_size + 32], xmm %+ %%i
```

xmm6 and xmm7 go into the caller's 32-byte shadow space, which the caller must always allocate. xmm8 and up go into a new
area. That area keeps 32 bytes of shadow space of its own and re-aligns the stack to 16.

### 2.6 `ALLOC_STACK`: realignment

When a function asks for stack and needs more alignment than the ABI gives (`required_stack_alignment`, which is
`(mmsize + 15) & ~15`, so 32 for YMM), x86inc does this:

```nasm
mov rstk, rsp
and rsp, ~(required_stack_alignment-1)
sub rsp, stack_size_padded
```

It keeps the old `rsp` in an extra register, which `SETUP_STACK_POINTER` adds to `regs_used`, or on the stack for a
negative size. On WIN64, `%%pad` also counts the shadow space and "(xmm_regs_used-8)*16 ; callee-saved xmm registers".
Large frames are touched page by page on Windows (`PROBE_STACK`, `STACK_PROBE_SIZE 8192` on WIN64, 0 elsewhere).

### 2.7 `RET` and automatic `vzeroupper`

```nasm
%define vzeroupper_required (mmsize > 16 && (ARCH_X86_64 == 0 || xmm_regs_used > 16 || notcpuflag(avx512)))
%macro RET 0          ; WIN64
    WIN64_RESTORE_XMM_INTERNAL
    POP_IF_USED 14, 13, 12, 11, 10, 9, 8, 7
    %if vzeroupper_required
        vzeroupper
    %endif
    AUTO_REP_RET
%endmacro
```

The SysV `RET` is the same, without the XMM restore. It undoes the stack (`mov rsp, rstkm` or `add rsp`) and pops
`14..9`. **`vzeroupper` is emitted whenever the function was declared under `INIT_YMM`**, so it cannot be forgotten.

### 2.8 `INIT_XMM` / `INIT_YMM`, `m0`, `mmsize`, and the suffix

`INIT_YMM avx2` sets `mmsize 32`, names `m0..m15` as `ymm0..ymm15` (`DEFINE_MMREGS ymm`), and through `INIT_CPUFLAGS`
sets `SUFFIX` to `_avx2`. So one source can be assembled once per ISA (`INIT_XMM sse2` / `INIT_YMM avx2`), giving
`ff_foo_sse2` and `ff_foo_avx2`. The asm-lessons: "x86asm provides the constant mmsize, which lets you know the size of the
SIMD register you are working with" (lesson 2).

### 2.9 Sections, names, and how x86inc learns the target

- **Which ABI:** `WIN64`/`UNIX64` come from the output format, not a define:
  ```nasm
  %if ARCH_X86_64
      %ifidn __OUTPUT_FORMAT__,win32 ... %elifidn __OUTPUT_FORMAT__,win64 ... %elifidn __OUTPUT_FORMAT__,x64
          %define WIN64  1
      %else
          %define UNIX64 1
  ```
  `ARCH_X86_64` itself comes from FFmpeg's generated `config.asm`, which every asm file gets through
  `X86ASMFLAGS += $(IFLAGS:%=%/) -I$(<D)/ -Pconfig.asm` (`ffbuild/common.mak:50`). `configure` chooses
  `X86ASMFLAGS="-f $objformat"` (elf64, win64, macho64...) and adds `-DPREFIX` when the platform has an extern prefix
  (`configure:6763-6764`).
- **NASM's spelling:** NASM 2.15 and later document the macro as `__?OUTPUT_FORMAT?__`: "holds the current output format
  name, as given by the -f option or NASM's default" (NASM manual 6.5,
  https://www.nasm.us/xdoc/3.01/html/nasmdoc6.html). "most standard macros of the form `__?foo?__` have aliases of form
  `__foo__`", and `%clear defalias` removes them. We use the `?` spelling.
- **Read-only data:** `SECTION_RODATA` is `.rdata` on Win32 and WIN64, `.rodata` on ELF, and `.text` on aout and coff.
- **Mangling:** `%ifdef PREFIX %define mangle(x) _ %+ x`. Win64 and ELF have no leading underscore; 32-bit Windows and
  Mach-O do.
- **PIC:** on x86-64, x86inc says `default rel` itself.

### 2.10 What x86inc does not do: Win64 unwind data

**x86inc emits no `.pdata` or `.xdata`.** A search of the file for `pdata`, `xdata`, `UNWIND` and `.seh` finds nothing.
Microsoft requires that data for any function that is not a leaf:

> "Leaf functions are functions that don't change any nonvolatile registers. ... To recover nonvolatile registers when an
> exception is handled, nonleaf functions are annotated with static data. ... This data is stored as *pdata* ... which in
> turn refers to *xdata* ... Leaf functions can be unwound simply by simulating a return, so pdata and xdata aren't
> required." (https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention, "Unwindability")

> "Table-based exception handling requires a table entry for all functions that allocate stack space or call another
> function (for example, nonleaf functions)." (https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64,
> "struct RUNTIME_FUNCTION")

NASM gives only the building blocks. It "implements building blocks for generating structures involved in stack
unwinding", and "NASM unfortunately does not facilitate generation of above mentioned detailed information about stack
frame layout". It warns that without `UNWIND_INFO` "the stack unwind procedure goes berserk, abruptly terminating without
any notification whatsoever" (NASM manual 9.7.2, "win64: Structured Exception Handling",
https://www.nasm.us/xdoc/3.01/html/nasmdoc9.html). Its tools are the default `.pdata` (`rdata align=4`) and `.xdata`
(`rdata align=8`) sections and `wrt ..imagebase`, which is "32-bit operand only".

So FFmpeg's Win64 objects carry no unwind data for functions that push or spill. Nothing breaks as long as no exception
or debugger stack walk passes through them. **Our layer will emit it** (phase 02), because a crash dump or profiler that
stops at the first SIMD kernel is exactly what we would be debugging with.

### 2.11 Worked example: `blend_over_batch` today, and what `MC_FUNC` would generate

**Today** (`asm/src/blend/index.asm:15-53`, prologue and epilogue only):

```nasm
global blend_over_batch
align 32
blend_over_batch:
    ; rdi = dst (uint32_t *), rsi = src (uint32_t *), rdx = count.
    vmovdqa BLEND_ALPHA_SHUFFLE, [blend_shuffle_over]     ; ymm15
    ...                                                   ; uses ymm0-3, ymm9-15; BLEND_LOAD_TAIL_MASK uses rax, rcx
.done:
    vzeroupper
    ret
```

**With the layer (phase 02's design).** The source is written once:

```nasm
MC_INIT_YMM                                              ; mmsize 32: MC_RET will emit vzeroupper
MC_FUNC blend_over_batch, 3, 5, 16, dst, src, count, left, masks
    ; dstq, srcq, countq are the arguments; leftq, masksq are two scratch registers (were rax, rcx)
    ...
    MC_RET
MC_ENDFUNC
```

**What it expands to on Linux (SysV, `-f elf64`):**

```nasm
global blend_over_batch:function (blend_over_batch.end - blend_over_batch)
align 32
blend_over_batch:
    ; r0 rdi = dst, r1 rsi = src, r2 rdx = count, r3 rcx = left, r4 r8 = masks
    ; 5 GPRs <= 9 volatile: nothing pushed. SysV: no vector register is callee-saved: nothing saved.
    ...
    vzeroupper                       ; MC_RET under MC_INIT_YMM
    ret
.end:
```

That is today's code, plus a typed, sized ELF symbol. The layer costs nothing on Linux.

**What it expands to on Windows (Win64, `-f win64`)**, following x86inc's `PROLOGUE`/`WIN64_SPILL_XMM` arithmetic for
5 GPRs and 16 vectors:

```nasm
global blend_over_batch
align 32
blend_over_batch:
    ; r0 rcx = dst, r1 rdx = src, r2 r8 = count, r3 r9 = left, r4 r10 = masks
    ; 5 GPRs <= 7 volatile: nothing pushed
    ; 16 vectors > 6: xmm6-xmm15 are the caller's, 10 of them
    sub     rsp, 168                ; xmm8-15: 8 x 16 = 128, + 32 shadow = 160, + 8 to put rsp back on 16
    movaps  [rsp + 176], xmm6       ; the caller's shadow space (return address at rsp + 168)
    movaps  [rsp + 192], xmm7
    movaps  [rsp + 32], xmm8
    ...                             ; xmm9 .. xmm14
    movaps  [rsp + 144], xmm15
    ...                             ; the body, unchanged
    movaps  xmm15, [rsp + 144]      ; MC_RET: restore in reverse
    ...
    movaps  xmm8, [rsp + 32]
    add     rsp, 168
    movaps  xmm7, [rsp + 24]
    movaps  xmm6, [rsp + 8]
    vzeroupper
    ret
.end:

section .pdata rdata align=4        ; what x86inc does not emit, and MC_FUNC/MC_ENDFUNC will
    dd      blend_over_batch wrt ..imagebase
    dd      blend_over_batch.end wrt ..imagebase
    dd      blend_over_batch.unwind wrt ..imagebase
section .xdata rdata align=8
blend_over_batch.unwind:
    db      1, <prolog bytes>, <code slots>, 0      ; version 1, no handler, no frame register
    ; UWOP_SAVE_XMM128 for xmm15 .. xmm6 (offsets / 16), then UWOP_ALLOC_LARGE 168 / 8, in descending prolog offset
```

The body never changes. Only the register names do, which is why the kernel must use `dstq` and not `rdi`.

**`noise3_batch8` is the hard case.** It has 6 arguments, so on Win64 `count` and `seed` arrive on the stack at
`[rsp + 40]` and `[rsp + 48]`. `seed` is a `uint32_t`, read as a dword. It uses all 16 vectors and wants a 32-byte-aligned
544-byte frame. Its prologue today, `push rbp; mov rbp, rsp; and rsp, -32` (`asm/src/noise/index.asm:21-23`), is already
the form Microsoft's unwind codes can describe (`UWOP_PUSH_NONVOL rbp`, then `UWOP_SET_FPREG`). x86inc instead keeps the
old `rsp` in a spare volatile register, and the unwind data cannot say that. So our Win64 realignment uses rbp as the
frame register (phase 02, risk R2).

### 2.12 What we take, and what we leave

| x86inc has | We take it? | Why |
|---|---|---|
| `cglobal`-style line: nargs, ngprs, nvec, stack, names | **Yes**, as `MC_FUNC` | Ahmed's ask; one line states the whole contract |
| `DECLARE_REG` tables, volatile registers first | **Yes**, the two x86-64 tables only | The heart of the portability |
| `PUSH_IF_USED`, `WIN64_SPILL_XMM`, `ALLOC_STACK` | **Yes** | Correct on both ABIs without thinking |
| `RET` with `vzeroupper_required` | **Yes**, as `MC_RET` | Removes the most common AVX mistake |
| Named arguments (`srcq`, `countd`) | **Yes** | Readable, and portable |
| `SECTION_RODATA`, `mangle`/`PREFIX` | **Yes**, `MC_RODATA` and `MC_MANGLE` (empty on ELF and Win64) | Mach-O later costs one define |
| x86-32, MMX, AVX-512, `INIT_ZMM`, `AVX512_MM_PERMUTATION` | **No** | Not our hardware (`plans/CODING_STANDARD.md:331`) |
| CPU suffixes (`_avx2`), `cpuflags` | **No** | One ISA, fixed at compile time (section 5) |
| `private_prefix` | **No** | Our names are already unique (`<dir>_..._batch`) |
| Instruction emulation macros (`movu`, `mova`, `paddb` wrappers) | **No** | We write VEX instructions by name |
| Unwind data | **We add it**, which x86inc lacks | Section 2.10 |

---

## 3. The ABI table

| | SysV AMD64 (Linux) | Microsoft x64 (Windows) | AAPCS64 (arm64, later) |
|---|---|---|---|
| Integer arguments | rdi, rsi, rdx, rcx, r8, r9 | rcx, rdx, r8, r9, then stack | x0-x7 |
| Callee-saved GPRs | rbx, rbp, r12-r15 | rbx, rbp, rdi, rsi, r12-r15 | x19-x29 (and sp) |
| Callee-saved vector | none | xmm6-xmm15 (upper YMM halves volatile) | low 64 bits of v8-v15 |
| Stack at entry | `rsp + 8` a multiple of 16; 128-byte red zone | 16-byte aligned before the call; 32-byte shadow space the caller allocates; no red zone | `sp` 16-byte aligned |
| Unwind data | not required to run; DWARF CFI only for debuggers (not quoted) | `.pdata`/`.xdata` for every nonleaf function | Linux: DWARF CFI as on x86-64. Windows on Arm: its own (not researched) |
| Direction flag | clear on entry and return | (not researched) | n/a |
| MXCSR / FPCR | control bits callee-saved, status bits not | MXCSR[6:15] nonvolatile, [0:5] volatile | (not researched) |
| `int` argument upper bits | unspecified | "callee can ignore the upper bits" | (not researched) |
| Symbol underscore | none (ELF) | none on Win64 (32-bit Windows has one) | none on Linux ELF; Mach-O adds one |

Sources, quoted:
- **SysV** (https://gitlab.com/x86-psABIs/x86-64-ABI, `low-level-sys-info.tex`, fetched 2026-11-17):
  - "the value of (%rsp + 8) is a multiple of 16 (32 or 64)";
  - "The 128-byte area beyond the location pointed to by %rsp is considered to be reserved";
  - "The direction flag DF in the %rFLAGS register must be clear (set to "forward" direction) on function entry and return";
  - "The control bits of the MXCSR register are callee-saved (preserved across calls), while the status bits are
    caller-saved";
  - "%rbp, %rbx and %r12 through %r15 "belong" to the calling function";
  - for narrow arguments, "the excess bits that would not be present in the memory representation of the type are
    unspecified".
- **Microsoft x64** (https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention, fetched 2026-11-17):
  - "Integer arguments are passed in registers RCX, RDX, R8, and R9";
  - "The caller must always allocate sufficient space to store four register parameters, even if the callee doesn't take
    that many parameters";
  - "The x64 ABI considers registers RBX, RBP, RDI, RSI, RSP, R12, R13, R14, R15, and XMM6-XMM15 nonvolatile";
  - "When present, the upper portions of YMM0-YMM15 and ZMM0-ZMM15 are also volatile";
  - "the stack pointer and malloc or alloca memory, which are 16-byte aligned";
  - "The volatile portion consists of the six status flags, in MXCSR[0:5], while the rest of the register, MXCSR[6:15], is
    considered nonvolatile".
- **AAPCS64** (https://raw.githubusercontent.com/ARM-software/abi-aa/main/aapcs64/aapcs64.rst, fetched 2026-11-17):
  - "The first eight registers, r0-r7, are used to pass argument values into a subroutine";
  - "Registers r19-r29 and SP are Callee-saved";
  - "only the bottom 64 bits of each value stored in v8-v15 need to be Callee-saved";
  - "SP mod 16 = 0. The stack must be quad-word aligned".

---

## 4. checkasm: how dav1d and FFmpeg check their assembly

**What it is.** checkasm is now a standalone library (https://code.videolan.org/videolan/checkasm, README, fetched
2026-11-17), "forked from dav1d's internal copy of checkasm, which was itself a more-or-less up-to-date version of the
various checkasm versions that existed in FFmpeg, x264 and so on". It is licensed **BSD 2-Clause** (`LICENSE`).
- What it catches on x86 and x86-64: "registers, stack, AVX2, MMX/FPU state".
- What it catches on ARM and ARM64: "registers, stack, VFP state".
- Its benchmark prints cycles with a "(vs ref)" column, for example `msac_decode_bool_sse2: 5.4 +/- 0.6 ... ( 1.35x)`,
  with the "Timing source: x86 (rdtsc)".

**How, on x86-64** (FFmpeg's copy, `tests/checkasm/ext/src/x86/checkasm.asm`,
https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/tests/checkasm/ext/src/x86/checkasm.asm, fetched 2026-11-17):

| Check | How (quoted) |
|---|---|
| Callee-saved GPRs | Fixed values `n9:`..`n14:` ("just random numbers to reduce the chance of incidental match") are loaded into r9-r14 before `call t0`, then each is compared after (`cmp r %+ i, [n %+ i]`). On WIN64, `n7`, `n8` too |
| Callee-saved XMM (WIN64) | `x6:`..`x15:` loaded into `m6..m15` before the call and compared after |
| Upper-32 garbage in `int` arguments | `mov r8, 0xdeadbeef00000000`, then `CLOBBER_UPPER r0, 1` .. `r5, 32` ORs it into each argument a per-function mask marks as 32-bit |
| Stack corruption | "write stack canaries to the area above parameters passed on the stack": the inverted return address in 8 slots, checked after the call ("stack corruption") |
| Missing `vzeroupper` | `xgetbv` with ecx = 1, `test al, 0x04` → "missing vzeroupper" |
| Missing `emms` | `fstenv`, then the FPU tag word must be `0xffff` → "missing emms, fpu tag = 0x%x" |

**What we already have, and what we lack** (phase 03):

| checkasm | Us (`tests/asm/abi/index.c`) |
|---|---|
| Callee-saved GPRs | Yes, rbx, rbp, r12-r15 with fixed patterns (`:25-30`) |
| XINUSE `vzeroupper` | Yes (`:114`) |
| Callee-saved XMM on WIN64 | No: we have no Win64 build. Phase 03 calls Win64-convention code through a `[[gnu::ms_abi]]` pointer on Linux |
| Upper-32 clobber | No |
| Stack canaries | No |
| `emms` | Not needed: MMX is banned by lint (`rules_asm/index.c:265-268`) |
| Kernel list | By hand (`:158-172`). checkasm registers each function by name from its test |
| Cycle bench with vs-ref | Wall clock only (`bench/simd/index.c:352-356`) |
| Random seed per run | No: fixed inputs (`ABI_RANDOM_SEED`, `:24`) |

---

## 5. Dispatch: compile time against run time

**FFmpeg and dav1d decide at run time.**
- FFmpeg checks `cpuid` and then `xgetbv`, guarded by OSXSAVE: `if ((ecx & 0x18000000) == 0x18000000) { xgetbv(0, ...);
  if ((xcr0_lo & 0x6) == 0x6)` (`libavutil/x86/cpu.c:141-144`).
- It then fills function pointers per instance, gated at compile time too: `EXTERNAL_AVX2(flags)` expands to
  `(HAVE_ ## cpuext ## suffix && ((flags) & AV_CPU_FLAG_ ## cpuext))` (`libavutil/cpu_internal.h:26-27`,
  `libavutil/x86/cpu.h:73`). An example: `if (EXTERNAL_AVX2_FAST(cpu_flags) ...) param->blend = ff_blend_addition_avx2;`
  (`libavfilter/x86/vf_blend_init.c:138-140`).
- The asm-lessons: "function pointers are C by default and are replaced with a particular instruction set variant ...
  detection is done once and then never needs to be done again" (lesson 3).
- dav1d does the same in `dav1d_get_cpu_flags_x86` (https://code.videolan.org/videolan/dav1d/-/raw/master/src/x86/cpu.c).

**We stay compile-time fixed, with a startup guard.** That matches Ahmed's ask ("compile time ... so that we can optimize
for either") and the single target machine (`config/machine.yaml:4-12`). C is built with `-march=skylake` everywhere
(`cmake/flags.cmake:44`), so a function pointer table would not make the rest of the program run on an older CPU anyway.
The guard (`asm/src/cpuid/index.asm:135-225`, from `.preinit_array`) already gives a clear message instead of SIGILL.

**Runtime dispatch is the recorded option** if we ever ship to unknown CPUs:
- one function-pointer table filled in `simd_init`;
- the C reference as the fallback;
- the C itself built for a baseline `-march`.
Out of scope for this plan (phase 00).

---

## 6. Hand-written assembly, intrinsics, and autovectorisation

| Claim | Source (quoted) |
|---|---|
| Intrinsics are slower | "intrinsics are typically around 10-15% slower than hand-written assembly (intrinsics supporters would disagree), depending on the compiler." (asm-lessons lesson 1) |
| Autovectorisation vs hand-written, dav1d | "around a 2x speedup from this automatic vectorisation, while the hand-written versions could reach 8x." (asm-lessons lesson 1) |
| Big gains are normal | "It's very common to get a 10x or more speed improvement from writing assembly code" (asm-lessons lesson 1) |
| Measured, whole decoder | Martin Storsjö (mstorsjo), 2024-10-18, "Autovectorization measurements from dav1d and ffmpeg", AWS Graviton 3 (AArch64), https://discourse.llvm.org/t/autovectorization-measurements-from-dav1d-and-ffmpeg/82623: "autovectorization gives roughly a 40-50% speedup"; with asm "around 450-460 fps"; "For GCC with assembly disabled, I got 185 fps"; "with assembly disabled and vectorization disabled, I got 128 fps" (Clang: 177 and 118). **The "about 2.5x" for hand-written over autovectorised is our arithmetic (455 / 185), not a figure in the post.** |
| FFmpeg and GCC autovectorisation | Storsjö, 2024: "FFmpeg explicitly disables autovectorization with GCC due to issues". **No longer true on FFmpeg master for x86:** `configure:8062-8088` (fetched 2026-11-17) adds `-fno-tree-vectorize` only for GCC < 13 ("it has historically been buggy"), GCC 14.0-14.3/15.0-15.1 on loongarch, and arches outside CI; x86, arm, aarch64, ppc, riscv, mips keep GCC's default |
| Our own numbers | `noise3_batch8` 19.33x on a 4096-point section; blend 8-25x; copy 8x; `vec_batch` 3-6x; popcount 0.46-2.14x (`bench/simd/README.md`, 2026-11-02) |

**Why our rule stands** (NASM only, `-fno-tree-vectorize -fno-tree-slp-vectorize`, `plans/CODING_STANDARD.md:318`):
1. **One way to write SIMD.** "every SIMD loop in the program can be found in `asm/`". An autovectorised loop moves when
   GCC changes, and is invisible in review.
2. **Testable against references.** Each kernel is one function with a scalar twin, compared bit for bit (1.2).
   Autovectorised code has no such seam.
3. **The lint can enforce it.** Intrinsics, `__builtin_ia32_*`, vector extensions and inline asm are tokens it bans
   (`rules_c/index.c:69-91`).
4. **Determinism.** Same seed, same world (`cmake/flags.cmake:85`). GCC under `-std=c23` does not contract `a * b + c`
   into an FMA (checked 2026-11-17, GCC 16.2.1, `-O3 -march=skylake`: `vmulss` then `vaddss`; under `-std=gnu23`:
   `vfmadd132ss`). A kernel matches the C exactly only when it does the same: FMA where the reference calls `fmaf`, and
   separate multiply and add where the C writes `a * b + c` (phase 05).

---

## 7. For anyone writing a kernel: what to do, and what not to do

This is the short form of `plans/CODING_STANDARD.md` section 7, as it will read after phase 02. Until phase 02 lands, the
"today" column is the rule.

**Do:**
- Write one function per array operation, named `<dir>_<verb>_batch`, with a C prototype in
  `run/include/simd/<dir>/index.h` and a scalar twin in `run/src/simd/reference/`.
- After phase 02: start the function with `MC_FUNC name, nargs, ngprs, nvec[, stack], names...`, end it with `MC_RET`
  and `MC_ENDFUNC`, and use the argument names (`dstq`, `countq`, `seedd`), never `rdi` or `rcx` by hand. Count honestly:
  `ngprs` includes scratch registers, `nvec` is the highest vector register used plus one.
- Use `MC_INIT_YMM` for a file that uses YMM, so `MC_RET` emits `vzeroupper`.
- Treat `int` and `uint32_t` arguments as 32-bit (`countd`): the upper half is garbage on both ABIs.
- Use FMA only where the reference calls `fmaf`, and match the reference's order of operations.
- Put constants after the code in `.text` (phase 04 may change this), aligned to 32.
- Add the kernel to `bench/simd` and show a gain at the lengths the game uses, or delete it.
- Run `asm_abi` and `asm_conventions` (which assembles for both ABIs after phase 02) before committing.

**Do not:**
- Hand-write a prologue, `push`, `vzeroupper` or `ret` in a kernel (after phase 02). The lint will refuse it.
- Use xmm6-15 without counting them in `nvec`: free on Linux, a broken caller on Windows.
- Assume the stack is 32-byte aligned: it is 16. Ask for stack in `MC_FUNC`, and the layer aligns it.
- Use the red zone. Win64 has none, and the layer's stack is portable.
- Call out of a kernel. A call makes it nonleaf, needs shadow space on Win64, and needs unwind data.
- Write intrinsics, inline `asm`, `__builtin_ia32_*`, AVX-512, MMX or a gather.
- Use OS calls in a kernel. `asm/src/cpuid/`'s system calls are the one exception, and they are Linux-only (1.3 item 1).

---

## 8. The phases

| Phase | File | What is true when it is done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This master and the phase files exist; scope and out-of-scope are written; the outline's errors are recorded |
| 01 | `...-phase-01-compile-time-os-and-arch-detection.md` | `platform.arch` exists; `MC_PLATFORM_OS_*` and `MC_PLATFORM_ARCH_*` reach C, CUDA, NASM and CMake; configure fails on a mismatch; the NASM object format follows the OS; `windows.h` is banned |
| 02 | `...-phase-02-our-own-cglobal-conventions-layer.md` | `MC_FUNC`/`MC_RET`/`MC_ENDFUNC` with both register tables, XMM saving, realignment, `vzeroupper`, Win64 unwind data; every kernel ported, bit-identical, bench unchanged; assembles for both formats |
| 03 | `...-phase-03-a-checkasm-grade-harness.md` | `asm_abi` checks random callee-saved values, upper-32 garbage, canaries, entry alignment, DF, MXCSR, and Win64 conventions through `ms_abi`; its list is generated; `bench/simd` records cycles and a `history.tsv` |
| 04 | `...-phase-04-audit-what-we-have.md` | Each of the 11 uncalled kernels is wired with a measured gain or deleted; popcount's short arrays decided; the rodata question settled with a measurement |
| 05 | `...-phase-05-kernels-where-the-profile-says.md` | A profile names the hot loops; each new kernel (likely `noise3_fbm_batch8` first) beats its reference and keeps the world's bytes |
| 06 | `...-phase-06-arm64-readiness-design.md` | A written, reviewed design for arm64: file shape, build selection, lint, C fallback; nothing built |
| 07 | `...-phase-07-the-final-check.md` | Full debug and release suites, the bench table before and after, Ahmed's gate, `REPO_STRUCTURE.md` updated |

---

## 9. Open questions (each with the default a phase takes if nobody answers)

| # | Question | Default | Phase |
|---|---|---|---|
| Q1 | The outline names `MC_OS_LINUX`, `MC_ARCH_X86_64`. The generator already makes `MC_PLATFORM_OS_LINUX` from the yaml's choice word. Which names? | The generator's: `MC_PLATFORM_OS_<WORD>`, `MC_PLATFORM_ARCH_<WORD>`. One mechanism, no aliases | 01 |
| Q2 | Should a `platform.os: windows` configure succeed before the rest of the game is ported? | No. Configure accepts the word, the asm and the headers honour it, and `cmake/flags.cmake` stops with "only asm/ is ported" until a later plan ports the rest | 01 |
| Q3 | Win64 realignment: rbp as frame register (describable) or x86inc's spare register (not)? | rbp, with `UWOP_SET_FPREG` | 02 |
| Q4 | Can our Win64 unwind data be proven correct without a Windows machine? | Only structurally (our test decodes it). Recorded as unproven until a Windows machine runs one exception through a kernel | 02, 07 |
| Q5 | `rdtsc` for the bench: inline asm and `__builtin_ia32_rdtsc` are banned. Where does it live? | A batch function `cpuid_tsc_read_batch` in `asm/src/cpuid/`, the one non-SIMD directory | 03 |
| Q6 | Constants in `.rodata`/`.rdata`? | Stay in `.text` unless phase 04 measures a gain; if moved, scope `[warning -reloc-rel-dword]` in `MC_RODATA` | 04 |
| Q7 | Delete a kernel with a gain but no caller? | Yes, unless phase 04 wires it into a real caller in the same phase (`plans/CODING_STANDARD.md:327`, and nothing should be kept that nothing runs) | 04 |
| Q8 | arm64 source spelling: `index.aarch64.S` (GAS through cpp) or `index.arm64.asm`? | `index.aarch64.S`, dav1d's `function`/`endfunc` style; NASM has no AArch64 | 06 |

---

## 10. Sources

Every URL was fetched on 2026-11-17 unless marked.

- x86inc: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/x86/x86inc.asm
- FFmpeg build: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/ffbuild/common.mak (line 50),
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/configure (lines 6763-6764, 8062-8088)
- FFmpeg dispatch: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/x86/cpu.c,
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/cpu_internal.h,
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/x86/cpu.h,
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavfilter/x86/vf_blend_init.c
- FFmpeg checkasm x86: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/tests/checkasm/ext/src/x86/checkasm.asm
- checkasm: https://code.videolan.org/videolan/checkasm (README.md, LICENSE)
- asm-lessons: https://github.com/FFmpeg/asm-lessons (lesson_01, lesson_02, lesson_03 `index.md`)
- dav1d: https://code.videolan.org/videolan/dav1d/-/raw/master/src/x86/cpu.c,
  https://code.videolan.org/videolan/dav1d/-/raw/master/src/arm/asm.S (`.macro function name, export=0, align=2` with a
  nested `endfunc`, `.hidden`/`.private_extern`, `EXTERN` = `_` + `PRIVATE_PREFIX` under `PREFIX`)
- SysV psABI: https://gitlab.com/x86-psABIs/x86-64-ABI (`x86-64-ABI/low-level-sys-info.tex`)
- Microsoft: https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention,
  https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
- NASM: https://www.nasm.us/xdoc/3.01/html/nasmdoc6.html (6.5 `__?OUTPUT_FORMAT?__`),
  https://www.nasm.us/xdoc/3.01/html/nasmdoc9.html (9.7.2 win64 SEH). Our NASM is 3.02 (`nasm -v`); the 3.01 manual was
  the newest online.
- AAPCS64: https://raw.githubusercontent.com/ARM-software/abi-aa/main/aapcs64/aapcs64.rst
- Storsjö: https://discourse.llvm.org/t/autovectorization-measurements-from-dav1d-and-ffmpeg/82623

**Not verified:**
- Win64 DF and `int`-argument rules beyond the quoted sentence;
- Windows on Arm unwind;
- AAPCS64 FPCR and narrow-argument rules;
- that SysV needs no CFI to run.
Each is marked "(not researched)" or "(not quoted)" where it appears.

---

## Appendix A: the approved outline, verbatim

# Plan: write `plans/012-ahmed-bohemoth-simd-and-assembly-portability*.md`

## Context

Ahmed asked, on 2026-11-17:
- Are we writing assembly SIMD properly?
- Can the game detect Windows or Linux, and x86-64 or (later) arm64, at compile time, so each target gets its own optimised assembly?
- What is FFmpeg's `cglobal` (from `x86inc.asm`, the asm-lessons), and should we have something like it?

He wants **every finding written into `plans/`**, so the whole team can read it in the repo. Nothing is implemented now: this plan only produces the plan files.

The research is done: three read-only agents covered the repo's asm, the build, and upstream FFmpeg, dav1d and checkasm. The plan files carry those findings, verbatim where it matters.

Decided by Ahmed:
- **Our own small macro layer modelled on x86inc**, not a vendored copy of `x86inc.asm`.
- **x86-64 only for now.** arm64 gets a clean hook and a design, and is built when there is hardware.

## Deliverable

One master file plus phase files, following `plans/README.md`: master copied verbatim, every todo unticked, each phase with Goal/Todo/Verification/Commits/Issues found and the richer parts from plans 006/010.

Commits:
- One commit per file, as Ahmed, with `git commit -F -`.
- No AI trailers; check each with `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'`.
- Never push.

Every Verification command goes through `tools/test-or-build.sh`.

Work order: one agent in its own worktree writes all the files. I land them on main.

Files:
- `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (master: the findings plus the plan)
- `plans/012-...-phase-00-vision-and-plan-files.md`
- `plans/012-...-phase-01-compile-time-os-and-arch-detection.md`
- `plans/012-...-phase-02-our-own-cglobal-conventions-layer.md`
- `plans/012-...-phase-03-a-checkasm-grade-harness.md`
- `plans/012-...-phase-04-audit-what-we-have.md`
- `plans/012-...-phase-05-kernels-where-the-profile-says.md`
- `plans/012-...-phase-06-arm64-readiness-design.md`
- `plans/012-...-phase-07-the-final-check.md`

## Master file content (the findings, kept in the repo)

### 1. What we have today, and is it right?

Mostly yes: correct for Linux, AVX2, SysV only.

**What is right:**
- Every kernel is compared bit for bit with a scalar C reference: in tests, with guard pages, misalignment and tails, and again at startup by `simd_self_check` (`run/src/simd/index.c`).
- `tests/asm/abi/` checks rbx, rbp and r12–r15 and the dirty-YMM state with `XGETBV(1)`.
- `rules_asm` lints vzeroupper, `default rel` and GNU-stack.
- `bench/simd` deletes any kernel with no gain.
- The FMA and no-gather choices are documented.

**What is weak or missing** (each with path and line):
1. **SysV and ELF are hard-coded.** `cmake/flags.cmake:25` sets `elf64`. `conventions/index.inc` says "every YMM register freely", which breaks Win64, where xmm6–15 are callee-saved. cpuid uses raw Linux `syscall`s and `.preinit_array`.
2. **No OS or arch detection anywhere.** There are zero `__linux__`, `_WIN32`, `CMAKE_SYSTEM_*` or `MC_PLATFORM_*` consumers. `config/machine.yaml` has `platform.os: linux` but no arch key.
3. **No calling-convention abstraction.** Each kernel hand-writes its prologue. There is no `cglobal`, no named arguments, no automatic saving, no automatic vzeroupper.
4. **6 of 15 kernels have no game caller:** `copy_stream_batch`, `blend_premul_batch`, the popcount/and/or/andnot set, `from_nonzero_u8`, `from_equal_u16`, and all three `vec_batch`.
5. **Popcount is slower than scalar under 12 words** (0.46x at 1 word, 0.84x at 8 words).
6. **The ABI test has gaps.**
   - It doesn't check stack alignment at entry, DF, MXCSR, or upper-32 clobbering of `int` arguments.
   - Its kernel list is maintained by hand.
   - The bench uses wall-clock time, not cycles.
7. **Hot loops are still scalar.**
   - Noise is one kernel call per octave, with a scalar `out[i] += v*amp` accumulate at `run/src/terrain/noise/index.c:145-162`.
   - Render encode: uniform scan, brick scatter.
   - Section palette pack, save encode, light sweeps, LOD downsample, tick nibble unpack.
8. Constants live in `.text`, because NASM's `reloc-rel-dword` warning rejected them in `.rodata`.
9. The lint is blind to YMM use inside macros.

### 2. FFmpeg's `cglobal`, explained

From x86inc.asm, quoted with URLs:
- The `cglobal name, nargs, ngprs, nvec[, stack], argnames…` expansion.
- `DECLARE_REG` tables for UNIX64 and WIN64, including stack-argument offsets.
- `PUSH_IF_USED`: callee-saved registers are pushed only past the volatile set (7 on Win64, 9 on SysV).
- `WIN64_SPILL_XMM` and the shadow space.
- `ALLOC_STACK` realignment.
- `RET` with automatic vzeroupper.
- `INIT_XMM`/`INIT_YMM` with `m0`/`mmsize` and the `_avx2` suffixing.
- `SECTION_RODATA` (`.rdata` on Win64), `mangle`/`PREFIX` underscores.
- WIN64/UNIX64 chosen from `__OUTPUT_FORMAT__`; FFmpeg injects config with `-Pconfig.asm`.
- License: ISC.
- **x86inc emits no Win64 unwind data (`.pdata`/`.xdata`).** That is safe only for leaf functions.

### 3. ABI table

| | SysV AMD64 | Microsoft x64 | AAPCS64 (later) |
|---|---|---|---|
| Integer args | rdi, rsi, rdx, rcx, r8, r9 | rcx, rdx, r8, r9 | x0–x7 |
| Callee-saved GPRs | rbx, rbp, r12–r15 | rbx, rbp, rdi, rsi, r12–r15 | x19–x29 |
| Callee-saved vector | none | xmm6–xmm15 | low 64 bits of v8–v15 |
| Stack | 128-byte red zone | 32-byte shadow space, no red zone | 16-byte aligned |
| Unwind data | not required | required for non-leaf functions | n/a |
| Symbol underscore | none (ELF) | none on Win64 | Mach-O adds one |

### 4. checkasm

How dav1d and FFmpeg check asm (standalone library, BSD-2):
- Random values in callee-saved registers, checked after the call.
- Upper-32 clobber of `int` arguments (`0xdeadbeef00000000`).
- Stack canaries.
- XINUSE vzeroupper check.
- `emms`.
- A cycle benchmark with a "vs ref" column.

### 5. Dispatch

- FFmpeg/dav1d check `cpuid` + `xgetbv` for OS support, then fill a function-pointer table at init, gated by `HAVE_*_EXTERNAL` at compile time.
- We stay **compile-time fixed ISA plus a startup guard**, which matches Ahmed's ask and the single target machine.
- Runtime dispatch is recorded as the option if we ever ship to unknown CPUs.

### 6. Hand-written asm vs intrinsics vs autovectorisation

The claims, each with its source:
- FFmpeg: "intrinsics 10–15% slower"; dav1d "2x vs 8x".
- Storsjö's dav1d measurement: autovectorisation +40–50%; hand-written about 2.5x whole-decoder.

Why our rule (NASM only, `-fno-tree-vectorize`) stands: one way to write SIMD, testable against references, and the lint can enforce it.

## Phases

**00, plan files.** The master holds sections 1–6 above. Phase 00 restates scope and out-of-scope:
- No Windows port of the rest of the game: display, threads, files and input stay Linux, listed in `REPO_STRUCTURE.md` §15.6.
- No AVX-512.
- No runtime dispatch.

**01, compile-time OS and arch detection.**
- **Config:** `config/machine.yaml` gets `platform.arch: x86_64 | arm64` next to `platform.os: linux | windows`. `compile/src/machine_config` emits `MC_OS_LINUX`/`MC_OS_WINDOWS` and `MC_ARCH_X86_64`/`MC_ARCH_ARM64` to `machine/index.{h,hu,inc,cmake}`.
- **CMake:** `cmake/flags.cmake` cross-checks the yaml against `CMAKE_SYSTEM_NAME`/`CMAKE_SYSTEM_PROCESSOR` and fails configure on a mismatch. It picks the NASM object format (`elf64`/`win64`) and the `-march` family from the arch.
- **C:** a header-only `run/include/types/platform/index.h` (reachable by run/, cuda/ and compile/) `static_assert`s that the generated macros agree with the compiler's own (`__x86_64__`/`_M_X64`, `__linux__`/`_WIN32`).
- **Asm:** `.inc` code gets `MC_ARCH_X86_64` and `MC_OS_*` from `machine/index.inc`.
- **Tests:** a mismatched yaml fails configure, and the generated values are checked.
- **Lint:** `windows.h` is banned outside a future platform layer.

**02, our own `cglobal` conventions layer** (`asm/include/conventions/index.inc`, about 300 lines).
- **Interface:** `MC_FUNC name, nargs, ngprs, nvec[, stack], argnames…`, with `MC_RET`, `MC_RODATA`, an `MC_ABI_SYSV`/`MC_ABI_WIN64` table chosen from `__?OUTPUT_FORMAT?__`, named arguments (`srcq`, `countd`), and `r0..rN`.
- **Prologue and epilogue:**
  - Automatic push and pop of callee-saved GPRs past the volatile set.
  - Win64 xmm6–15 saved when `nvec > 6`.
  - Stack realignment.
  - Automatic vzeroupper when the function uses YMM.
  - Symbol mangling.
- **Win64 unwind:** kernels stay leaf where possible. Non-leaf ones get hand-built `.pdata`/`.xdata` from the macro.
- **Lint:** `rules_asm` learns that `MC_RET` emits vzeroupper, and that `MC_FUNC` covers the form checks.
- **Port:** every kernel moves to it, with bit-identical outputs and unchanged bench numbers.
- **Tests:** `tests/asm/conventions` assembles every kernel under both `-f elf64` and `-f win64`, and a disassembly check confirms Win64 saves nonvolatiles.
- **Docs:** `CODING_STANDARD.md` §7 is updated.

**03, a checkasm-grade harness.** `tests/asm/abi` grows to:
- random callee-saved values;
- `int` upper-32 clobbering;
- stack canaries;
- stack alignment at entry;
- DF and MXCSR checks;
- the kernel list generated from the headers, not by hand.

`bench/simd` gains cycle counts (rdtsc/clock64-style, recording the clock as the repo does) and a `history.tsv`.

**04, audit what we have.**
- Each of the 6 unused kernels is either wired into a real caller with a measured gain (popcount vs `stdc_count_ones`), or deleted per the standard.
- Popcount gets a short-array decision.
- Constants move to a proper rodata section if `MC_RODATA` fixes the relocation warning.

**05, kernels where the profile says.**
- Profile first; all game-side.
- Likely candidates:
  - fused multi-octave noise `noise3_fbm_batch8`, which removes the per-octave scalar accumulate and the float64 narrowing loop;
  - render encode uniform scan and brick scatter (BMI2 `pdep`);
  - section palette pack/unpack;
  - light-section sweeps.
- Each kernel must beat its reference in `bench/simd`, or it is not kept.

**06, arm64 readiness (design only).**
- **File shape:** a per-arch source rule, `asm/src/<k>/index.asm` for x86-64 and `index.aarch64.S` (GAS through cpp, dav1d `function`/`endfunc` style) for arm64.
- **Build:** CMake selects sources by `MC_ARCH_*`.
- **Lint:** `rules_tree` allows `.S` only as `index.aarch64.S`, and the `arm_neon.h` ban stays.
- **C side:** prototypes stay arch-neutral, and C references are the fallback.
- Nothing is built until there is arm64 hardware.

**07, final check.** Full debug and release suites, the bench table before and after, Ahmed's gate, and REPO_STRUCTURE updated.

## Critical files to read and cite

- `asm/include/conventions/index.inc`
- `asm/src/*/index.asm`
- `run/src/simd/index.c` and its `reference/`
- `tests/asm/abi/index.c` and `tests/asm/conventions/`
- `bench/simd/`
- `cmake/flags.cmake`, `cmake/machine.cmake`, `cmake/targets.cmake`
- `config/machine.yaml` and `compile/src/machine_config/index.c`
- `compile/src/standards_lint/rules_{asm,c,include,tree}`
- `plans/CODING_STANDARD.md` §7 and `plans/REPO_STRUCTURE.md` §8, §11, §15
- `run/src/terrain/noise/index.c`
- upstream x86inc, checkasm and dav1d URLs (from the research)

## Verification (of the plan files)

```
ls plans/012-*
grep -L "## Issues found" plans/012-*phase-*.md     # prints nothing
grep -n "test-or-build.sh" plans/012-*phase-*.md    # every heavy command wrapped
git log --format=%B -9 | grep -iE 'co-authored|anthropic|claude|opus|sonnet'   # nothing
git status --short                                   # only plans/012-* new
```
