# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 2: Our own `cglobal`: the conventions layer

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012, and for phase 01.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md` (Issues found 5 and
7), then the master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md`, all of section 2 (2.11 above all),
sections 3 and 7, and Q3 and Q4. Then phase 01's file for the defines this phase reads.

## Goal

**Every kernel states its contract on one line, and the same source is correct under both x86-64 calling conventions.**

- `asm/include/conventions/index.inc` grows from 27 lines to about 300. It is our own layer, modelled on x86inc, not a
  copy of it, and it provides:

  | Macro | What it does |
  |---|---|
  | `MC_INIT_YMM`, `MC_INIT_XMM`, `MC_INIT_GPR` | Set the vector width for the functions that follow: 32, 16, or none |
  | `MC_FUNC name, nargs, ngprs, nvec[, stack], names...` | Global symbol, alignment, label, prologue, named registers |
  | `MC_RET` | The epilogue for what `MC_FUNC` did, `vzeroupper` under `MC_INIT_YMM`, then `ret` |
  | `MC_ENDFUNC` | The end label, the ELF symbol size, and on Win64 the `.pdata`/`.xdata` entry for a nonleaf function |
  | `MC_RODATA [align]` | `.rodata` on ELF, `.rdata` on Win64 (whether it is used is phase 04's question) |
  | `MC_MANGLE(x)` | Empty on ELF and Win64; one define away from Mach-O's `_` |
  | `MC_ABI_SYSV`, `MC_ABI_WIN64` | 1 or 0, from `__?OUTPUT_FORMAT?__`, cross-checked against phase 01's `MC_PLATFORM_OS_*` |
  | `r0`..`r14` (`q`, `d`, `w`, `b`), argument names (`dstq`, `countd`), `m0`..`m(nvec-1)` and `xm0`..`xm(nvec-1)` | Register names, and only as many vector names as `nvec` declares |

- **Every function in `asm/src/` moves to it.** That is the 15 SIMD kernels and the 5 functions of `cpuid/`; `embed_assets`
  has no code. Each gives exactly the bytes it gave before (the `asm_*` reference tests and `simd_self_check`), and
  `bench/simd` shows no row slower beyond noise.
- **Both formats work.** Each file assembles as `-f elf64` and `-f win64`. The Win64 object carries unwind data that
  decodes back to its own prologue (Q4: proven structurally, not by a Windows unwinder). Phase 01's "Win64 needs phase 02"
  stop is removed, though CMake still refuses a Windows configure (phase 01, Q2).
- **The lint follows.** `MC_RET` counts as `vzeroupper` and `ret`. A hand-written prologue, `ret`, `push` or literal
  register inside an `MC_FUNC` body is a breach. That also closes master 1.3 item 9: YMM hidden in a macro can no longer
  skip a `vzeroupper`.
- **`plans/CODING_STANDARD.md` section 7** says all of this, and the master's section 7 list becomes its rule.

## Where this fits

**The vision.** Ahmed: "Can we please look at what `cglobal` is properly from ffmpeg assembly headers, maybe we can use
something similar here!!" He decided on our own layer, not x86inc itself (2026-11-17).

**What it reads from phase 01:** `MC_PLATFORM_OS_LINUX`/`_WINDOWS` and `MC_PLATFORM_ARCH_X86_64` in `machine/index.inc`,
and the format check that this phase extends.

**What later phases read from it:**
- phase 03's harness checks the Win64 path through `ms_abi`, using `-DMC_ABI_FORCE_WIN64` (below);
- phases 04 and 05 write every kernel with `MC_FUNC`;
- phase 06 mirrors the shape for arm64 (`function`/`endfunc`).

**What this phase must NOT build:**
- No change to what any kernel computes. Bit-identical, or it does not land.
- No new kernel, and no kernel deleted (phase 04 decides that).
- No CPU suffixes, no `cpuflags`, no x86-32, no AVX-512 (master 2.12).
- No vendored x86inc text. Read it, then write ours, crediting it in the header comment (ISC asks only for the notice if
  copied; we do not copy).
- No Windows build of the game (phase 00 Part 4).

## Ahmed's words

- "Can we please look at what `cglobal` is properly from ffmpeg assembly headers, maybe we can use something similar here!!"
- "I am not sure if we are properly writing assembly SIMD!"

---

## Background, explained

### What changes for a kernel author

Before (`asm/src/blend/index.asm:15-18`, `:51-53`):

```nasm
section .text
global blend_over_batch
align 32
blend_over_batch:
    ; rdi = dst (uint32_t *), rsi = src (uint32_t *), rdx = count.
    ...
    vzeroupper
    ret
```

After:

```nasm
section .text
MC_INIT_YMM
MC_FUNC blend_over_batch, 3, 5, 16, dst, src, count, left, masks
    ...                    ; dstq, srcq, countq, leftq, masksq; m0..m15; never rdi or ymm9 by name
    MC_RET
MC_ENDFUNC
```

The master's 2.11 gives what this expands to under each ABI.

### The two register tables (master 2.3)

| rN | Win64 | SysV | Volatile on Win64 / SysV |
|---|---|---|---|
| r0 | rcx | rdi | yes / yes |
| r1 | rdx | rsi | yes / yes |
| r2 | r8 | rdx | yes / yes |
| r3 | r9 | rcx | yes / yes |
| r4 | r10 (arg 5 loaded from `[rsp + 40]`) | r8 | yes / yes |
| r5 | r11 (arg 6 from `[rsp + 48]`) | r9 | yes / yes |
| r6 | rax | rax | yes / yes |
| r7 | rdi | r10 | **no** / yes |
| r8 | rsi | r11 | **no** / yes |
| r9 | rbx | rbx | no / no |
| r10 | rbp | rbp | no / no |
| r11-r14 | r14, r15, r12, r13 | r14, r15, r12, r13 | no / no |

`ngprs` past 7 on Win64, or past 9 on SysV, pushes the extra registers in that order.

### The prologue `MC_FUNC` emits, in order

1. On Win64, load arguments 5 and up from the stack into r4, r5 (`[rsp + 40]`, `[rsp + 48]`, before any push).
2. Push r7..r(ngprs-1) that are callee-saved under this ABI (`PUSH_IF_USED`).
3. On Win64, when `nvec > 6`: save xmm6..xmm(nvec-1). xmm6 and xmm7 go in the caller's shadow space; the rest go in a new
   16-aligned area, which keeps 32 bytes of shadow of its own (`WIN64_SPILL_XMM`).
4. With a `stack` size:
   - on both ABIs, `push rbp` (if not already pushed), `mov rbp, rsp`, `and rsp, -32` under `MC_INIT_YMM`, then
     `sub rsp, stack`;
   - rbp is the frame register (master Q3), because Win64's unwind codes can describe `UWOP_SET_FPREG` and cannot describe
     x86inc's spare-register copy of `rsp`;
   - on Win64 the XMM saves of step 3 happen before rbp is set, at positive offsets, so the unwinder finds them.
5. Define the names: `rNq/d/w/b`, the argument names, `m0..m(nvec-1)`, `xm0..xm(nvec-1)`. Every higher `mN` is left
   undefined, so a kernel that uses more vectors than it declared fails to assemble.

`MC_RET` undoes these in reverse:
- `mov rsp, rbp` and `pop rbp`, or `add rsp`;
- the XMM restores;
- the pops;
- then `vzeroupper` under `MC_INIT_YMM`;
- then `ret`.

A function may have several `MC_RET`s (blend's `.done` is reached by more than one path today).

### Win64 unwind data, emitted by `MC_ENDFUNC`

For every function whose Win64 prologue pushes, saves an XMM register, or allocates (master 1.3 item 11):

```nasm
section .pdata rdata align=4
    dd  name wrt ..imagebase, name.end wrt ..imagebase, name.unwind wrt ..imagebase
section .xdata rdata align=8
name.unwind:
    db  1, prolog_bytes, code_slots, frame_register | (frame_offset << 4)
    dw  ...                 ; UWOP_* codes, descending prolog offset
```

- **The codes:** `UWOP_PUSH_NONVOL` per push; `UWOP_ALLOC_SMALL` (8 to 128 bytes) or `UWOP_ALLOC_LARGE` (136 to 512K-8);
  `UWOP_SAVE_XMM128` per XMM register (offset / 16); `UWOP_SET_FPREG` for rbp.
- **The offsets** come from local labels `MC_FUNC` places after each prologue instruction.
- **The prolog limit:** it must stay under 255 bytes (`.ENDPROLOG` "Must occur in the first 255 bytes"); `%fatal`
  otherwise.
- **The rules quoted:** https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64 (master 2.10). The NASM syntax
  is from the NASM manual, 9.7.2.

A function that saves nothing on Win64 is a leaf and gets no entry ("Leaf functions can be unwound simply by simulating a
return").

### How the layer is tested on Linux without Windows

- **`-f win64`** produces a real COFF object. `asm_conventions` parses its section table, `.pdata` and `.xdata` itself (a
  few dozen lines of C) and checks them against the prologue.
- **`-f elf64 -DMC_ABI_FORCE_WIN64`** makes the layer choose the Win64 table while writing ELF. That object links into a
  Linux test, and phase 03 calls it through a `[[gnu::ms_abi]]` function pointer, so Win64-convention code actually runs.
  The define is accepted only by a test (phase 03 links such objects only into `asm_abi`), and `standards_lint` refuses it
  in `cmake/`.

### The ports, and what each needs

| File | Functions | Win64 nonleaf? | Note |
|---|---|---|---|
| `copy` | `copy_stream_batch`, `copy_stream_rows_batch` | rows: yes (rbx) | `copy_stream_batch` jumps into rows (`:24`); on Win64, rows' 5th and 6th arguments are on the stack, so the jump cannot stay. Default: the row loop becomes a `%macro` both functions expand, one jump fewer and no stack shuffle |
| `blend` | `over`, `premul` | yes (xmm6-15) | `BLEND_LOAD_TAIL_MASK` uses rax and rcx (`asm/include/blend/index.inc:29-34`): rcx is argument 0 on Win64, so they become named scratch registers |
| `bitmask` | 7 | those using ymm6-7 | Count each function's real `nvec`; most will be leaf on Win64 if they stay under 6 vectors |
| `vec_batch` | 3 | yes (xmm6-15) | Matrix rows in ymm6-12 (`asm/include/vec_batch/index.inc:33-39`) |
| `noise` | `noise3_batch8` | yes (xmm6-15, frame) | 6 arguments: `count` and `seed` from the stack on Win64; `seed` is `uint32_t`, so `seedd`. The 544-byte 32-aligned frame uses the rbp form (Q3) |
| `cpuid` | 5 | read and guard: yes | `MC_INIT_GPR`. The guard's system calls and `.preinit_array` are wrapped in `%if MC_PLATFORM_OS_LINUX`, because they mean nothing elsewhere (phase 00 Part 4). The guard calls `cpuid_decode_batch`, so it needs Win64's shadow space: `stack` covers it |

Register defines in `asm/include/<k>/index.inc` (`BLEND_ZERO ymm14`, `NOISE_REG_SIGN ymm11`, ...) become `m14`, `m11`, so
an undercounted `nvec` fails.

## Owned paths

This phase may edit these, plus this file:
- `asm/include/conventions/` (`index.inc`, `README.md`)
- `asm/src/{copy,blend,bitmask,noise,vec_batch,cpuid}/` (`index.asm`, `README.md`) and `asm/include/{copy,blend,bitmask,noise,vec_batch}/index.inc`
- `compile/src/standards_lint/rules_asm/` (`index.c`, `README.md`) and its fixtures under `tests/compile/standards_lint/fixtures/`
- `tests/asm/conventions/` (`index.c`, `README.md`, `test.cmake`, new `fixtures/`)
- `plans/CODING_STANDARD.md` section 7 only

**Not touched:**
- `run/include/simd/` (the prototypes do not change);
- `run/src/simd/reference/`;
- `tests/asm/<kernel>/` (they must pass unchanged, which is the point);
- `tests/asm/abi/` (phase 03);
- `bench/simd/index.c` (phase 03);
- `cmake/` (phase 01 owns the format; this phase needs no CMake change).

## Steps

1. **The layer, alone**, with its fixture tests, before any kernel moves. *Why: the macros are checked by their own tests,
   not by a broken kernel.*
2. **Port `cpuid`** first: GPR only, no vectors, but pushes, a call and stack. *Why: the simplest file that exercises
   push, stack and nonleaf unwind.*
3. **Port `copy`, then `bitmask`, `blend`, `vec_batch`, `noise`**, one commit per file, each with its `asm_<kernel>`, `asm_abi`
   and `asm_simd` passing. *Why: one file at a time keeps a regression in one diff. Noise is last because its frame is the
   hardest case.*
4. **The lint**:
   - `MC_RET` is a `vzeroupper` plus `ret`;
   - in an `MC_FUNC` body, `ret`, `push`, `pop`, `vzeroupper` and literal `ymmN`/`xmmN`/GPR names (other than `rsp`) are
     breaches (`asm_hand_written_frame`, `asm_literal_register`);
   - a `global` in `asm/src/` outside `MC_FUNC` is a breach, except the `:data` globals of `embed_assets`.
   - The comment at `asm/include/conventions/index.inc:1-3` ("a macro would hide them from it") is rewritten, because
     the lint now reads the macro's name.
   - *Why: the rule moves from "write it out so the lint sees it" to "only the layer writes it".*
5. **Remove phase 01's Win64 stop** from `conventions/index.inc`. *Why: the layer makes Win64 objects right.*
6. **`plans/CODING_STANDARD.md` section 7**:
   - the required form becomes the `MC_FUNC` example;
   - "SysV AMD64 ABI" (`:353`) becomes "both x86-64 ABIs, through `asm/include/conventions/`";
   - the master's section 7 lists are added. *Why: the standard is what the next author reads.*
7. **The bench, before and after**, in release, recorded below. *Why: the layer must cost nothing on Linux.*

## Todo

From the master (section 8, phase 02):

- [ ] `asm/include/conventions/index.inc`: `MC_INIT_*`, `MC_FUNC`, `MC_RET`, `MC_ENDFUNC`, `MC_RODATA`, `MC_MANGLE`, `MC_ABI_*`, both register tables, `rN` and named arguments, `mN`/`xmN` limited by `nvec`
- [ ] Prologue and epilogue: callee-saved pushes past the volatile set, Win64 xmm6-15 saves when `nvec > 6`, rbp-frame realignment, `vzeroupper`
- [ ] Win64 `.pdata`/`.xdata` from `MC_ENDFUNC` for every nonleaf function
- [ ] Every kernel ported, one commit per file: cpuid, copy, bitmask, blend, vec_batch, noise
- [ ] `rules_asm` learns `MC_RET`, `MC_FUNC` and the body rules
- [ ] `tests/asm/conventions`: both formats, unwind decoded, fixture refusals
- [ ] `plans/CODING_STANDARD.md` section 7 updated

Expanded:

- [ ] `asm/include/conventions/README.md` rewritten: the macros, the two tables, a worked example, "what not to do"
- [ ] Each kernel's `objdump -d` size per function before and after, recorded here (SysV should change by a few bytes at most)
- [ ] `bench_simd` release, before and after, recorded here, best of 3 runs each side
- [ ] `standards_lint`, `clang_format`, the whole debug suite, and the release `asm_*` tests pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test (ctest) | Checks | Numbers | Fails when |
|---|---|---|---|
| `asm_<kernel>` (existing: `asm_copy`, `asm_blend`, `asm_bitmask`, `asm_noise`, `asm_vec_batch`, `asm_simd`) | Every kernel against its reference, guard pages, misalignment, tails | unchanged counts, all pass | A port changed a result |
| `asm_abi` (existing) | rbx, rbp, r12-r15 kept; no dirty YMM | 15 kernels × 4 counts | The layer's epilogue wrong |
| `asm_conventions` `every_file_assembles_for_both_abis` | NASM `-f elf64` (OS linux) and `-f win64` (OS windows) | 6 files × 2 formats = 12 clean assembles under `-w+all -Werror` | A format breaks |
| `asm_conventions` `win64_unwind_data_matches_each_prologue` | The test parses each COFF object's `.pdata` and `.xdata` | one `RUNTIME_FUNCTION` per nonleaf function; e.g. `blend_over_batch`: 10 `UWOP_SAVE_XMM128` (xmm6-15, each once) and 1 `UWOP_ALLOC_LARGE` of 168; `noise3_batch8`: `UWOP_PUSH_NONVOL` rbp, `UWOP_SET_FPREG`, 10 saves; leaf functions: no entry | Unwind data missing or wrong |
| `asm_conventions` `the_layer_refuses_a_wrong_contract` | Fixtures | 5 refusals: `nargs > ngprs`; `nvec > 16`; `m(nvec)` used; `MC_ENDFUNC` with no `MC_RET`; a prologue over 255 bytes | A wrong count assembles |
| `asm_conventions` `sysv_output_is_today_s` | `objdump -d` of `blend_over_batch` under SysV | the body's bytes equal the pre-port object's, apart from symbol type and size | The layer adds code on Linux |
| `asm_conventions` existing 3 tests | CPU-feature refusals | unchanged | |
| `compile_standards_lint` fixtures | `ret`, `push rbx`, `ymm3`, `vzeroupper` inside `MC_FUNC`; a bare `global` | 5 breaches; 0 on every real file | The lint blind to a hand-written frame |
| `run_terrain_*`, `run_save_world` (existing) | The world's bytes | digests unchanged | Noise moved |
| `bench_simd` (release, not a ctest) | Every row | no gated row below 1.0x; each kernel's ns/element within 5% of before, or the noise shown by a rerun | The layer costs time |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^asm_' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(compile_standards_lint|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^(run_terrain_.*|run_save_world)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^asm_' --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_simd
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed reads:
1. `asm/include/conventions/README.md`'s worked example, and one ported kernel (`asm/src/blend/index.asm`) top to bottom.
2. The Win64 disassembly of `blend_over_batch` (`objdump -d` of the test's COFF object, pasted here) against the master's
   2.11.
3. The bench table before and after.

He says whether the layer reads as simply as he hoped from `cglobal`.

## Risks and open questions

| Risk or question | Default |
|---|---|
| R1: the lint's new body rules refuse something legitimate (e.g. `cpuid` must name `eax`/`ebx`, which `cpuid` writes) | Allow the fixed-register instructions' own registers in `MC_INIT_GPR` files, listed in the lint's README |
| R2 / Q3: Win64 realignment and XMM saves together | rbp frame, XMM saved before `mov rbp, rsp`, at positive offsets; recorded with the decoded codes |
| Q4: unwind data unproven without Windows | Structural test only; written as unproven in `asm/include/conventions/README.md` and phase 07 |
| `copy_stream_batch`'s jump | The shared `%macro` body; phase 04 may delete `copy_stream_batch` anyway |
| Named registers change the SysV bytes | Choose argument and scratch names so SysV maps to today's registers; record any function whose bytes change, with why |
| The layer grows past about 300 lines | Fine if every macro has a test; record the count |
| `MC_ABI_FORCE_WIN64` leaks into a real build | `standards_lint` refuses it outside `tests/` and `cmake/`'s test wiring |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
