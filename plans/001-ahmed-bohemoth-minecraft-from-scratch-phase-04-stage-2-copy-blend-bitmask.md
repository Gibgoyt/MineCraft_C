# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 2: Copy, blend and bitmask kernels

**Status:** done   (updated 2026-11-02)

## Goal

NASM conventions are in use, with streaming copy, alpha blend, popcount and mask kernels and C prototypes.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-math-simd.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/simd/index.*` and `README.md` (the Owner tier)
- `asm/{src,include}/copy/`
- `asm/{src,include}/blend/`
- `asm/{src,include}/bitmask/`
- `run/{src,include}/simd/*`
- `tests/asm/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `simd` Owner tier: `run/src/simd/index.c` with paired `simd_init` and `simd_deinit`, owning the regions and arenas its parts borrow
- [x] Create the shared NASM conventions in `asm/include/` per `plans/CODING_STANDARD.md`
- [x] Implement `asm/src/copy/index.asm`, `asm/src/blend/index.asm`, `asm/src/bitmask/index.asm`
- [x] Declare C prototypes in `run/include/simd/<name>/index.h`
- [x] Add tests against scalar C references under `tests/asm/`, registered with names starting `asm_`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

Wiring outside the owned paths, each the smallest that lets this module be called:

- `run/include/main/index.h`, `run/src/main/index.c`: `app_t` gains `simd_t simd` (320 to 448 bytes), `simd_init` runs after the CPU check, and `simd_deinit` runs in reverse order.
- `tests/run/types/index.c`: the game now reserves two regions, so "1 reserved, 1 released" and "leak at exit: 1 regions" became 2.
- `plans/REPO_STRUCTURE.md`: added the `simd` Owner, `simd/reference` and `asm/include/conventions`.

## Results

Run on 2026-11-02 at the commit below, from a deleted `build/`:

- Debug: `asm_bitmask`, `asm_blend`, `asm_conventions`, `asm_copy`, `asm_simd`, `standards_lint` pass. The whole suite is 19 of 19. `pgrep -a minecraft_c` rc 1.
- `tools/test-or-build.sh ctest --preset sanitize` and `--preset release`, `-R '^(asm_|run_types$)'`: 6 of 6 on each.
- `asm_blend` checks `blend_over_batch` for all 2^24 source, destination and alpha values against plain division, not only against the reference.
- Tests catch broken kernels. Each change was made, run and undone: a rounding constant of 127 fails `asm_blend`, `asm_simd` and `run_types`, since the game then exits at the self-check; 33 popcount rounds before widening fails `asm_bitmask` (its random-input self-check still passes, so `asm_simd` does not catch this one); a tail mask of all ones gives SIGSEGV in `asm_blend` at the guard page; a dropped 8-byte tail step fails `asm_copy`.

Each kernel against its scalar reference. This is a throwaway program, not the phase gate's benchmark, which is `bench/simd/` in Stage 04.3. C at `-O3 -march=skylake -fno-tree-vectorize -fno-tree-slp-vectorize`, one run pinned to CPU 2 under the build lock, best of 200. Copies go to ordinary cached memory, so this says nothing about a scanout buffer.

| Kernel | Input | Kernel | Reference | Gain |
|---|---|---|---|---|
| `copy_stream_rows_batch` | 1080p frame | 0.027 ns/byte | 0.235 | 8.74x (1.90x against `memcpy`, 0.051) |
| `copy_stream_batch` | 1080p frame | 0.027 ns/byte | 0.242 | 9.07x |
| `blend_over_batch` | runs, no partial alpha | 0.131 ns/pixel | 2.342 | 17.86x |
| `blend_over_batch` | runs, 20% partial | 0.181 | 2.333 | 12.86x |
| `blend_over_batch` | all partial | 0.293 | 2.337 | 7.97x |
| `blend_premul_batch` | runs, no partial alpha | 0.233 | 3.895 | 16.69x |
| `blend_premul_batch` | all partial | 0.250 | 3.894 | 15.59x |
| `bitmask_popcount_batch` | 8 / 64 / 4096 words | 0.369 / 0.154 / 0.131 ns/word | 0.393 / 0.285 / 0.285 | 1.07x / 1.86x / 2.17x |
| `bitmask_and_batch` | 8 / 64 / 4096 words | 0.246 / 0.138 / 0.158 | 0.433 / 0.301 / 0.360 | 1.76x / 2.18x / 2.27x |
| `bitmask_or_batch` | 8 / 64 / 4096 words | 0.319 / 0.138 / 0.155 | 0.433 / 0.301 / 0.360 | 1.36x / 2.18x / 2.32x |
| `bitmask_andnot_batch` | 8 / 64 / 4096 words | 0.270 / 0.161 / 0.154 | 0.411 / 0.307 / 0.359 | 1.52x / 1.91x / 2.33x |
| `bitmask_from_nonzero_u16_batch` | 4096 values | 0.017 ns/value | 0.501 | 29.72x |
| `bitmask_from_equal_u16_batch` | 4096 values | 0.017 | 0.501 | 29.83x |
| `bitmask_from_nonzero_u8_batch` | 4096 values | 0.011 | 0.501 | 47.07x |

The popcount's gain at 8 words is 1.07x, which is thin: see Issue 4.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^asm_' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `57cfde7` Gate check: `asm_abi`, and the popcount's stated length (issues 7 and 8).
- `1243e43` Add the streaming copy, alpha blend, popcount and mask kernels in NASM, with C prototypes, scalar references, a simd Owner that checks them at startup, and tests.

## Issues found

1. **Kernel constants could not go in `.rodata`.** Expected: `section .rodata align=32`, the normal place. Actual: `error: 32-bit relative section-crossing relocation [-w+error=reloc-rel-dword]` from `asm/src/blend/index.asm:36` and `asm/src/bitmask/index.asm:25` on, under the build's `-w+all -Werror`. Done: the tables moved to the end of `.text`, as `asm/src/cpuid/` already does, and `asm/include/conventions/` says so. The warning stays on: `.text` needs no relocation, and nothing writes the tables.
2. **The lint holds `simd_t` to the shared-struct rule.** Expected: `sizeof` and `alignof` asserts to be enough for a struct nothing but C uses. Actual: `run/include/simd/index.h:12: [struct_no_offsetof_assert] simd_t is shared with CUDA, NASM or files`, since it treats everything under `run/include/simd/` as shared with NASM. Done: added the `offsetof` asserts. Left the lint alone.
3. **The parts need no memory, though the todo says the Owner owns what they borrow.** Every kernel uses the caller's pointers. Done: `simd_t` holds one 2 MiB region and an arena, and only `simd_self_check` uses them today: four 8 KiB buffers, more than a frame may hold on the stack. So a wrong kernel stops the game at startup with exit status 5. `run/src/simd/README.md` says this.
4. **`bitmask_popcount_batch` barely gains on very short arrays.** Expected: about 2x. Actual: 1.07x at 8 words and 1.86x to 2.17x from 64 up (table above). Short arrays have a whole batch's widening and horizontal sum to pay for. Left alone, since it is still a gain and sections use longer arrays. Stage 04.3's `bench/simd/` is the judge, and should measure the lengths real callers use.
5. **Some things the tests cannot see.** A kernel leaving dirty YMM state or clobbering `rbx` would not fail a test. `vzeroupper` is covered by `standards_lint`, and `rbx`, the only callee-saved register used, by review (`asm/src/copy/index.asm` pushes and pops it). That `sfence` orders a non-temporal write before a page flip, and the copy's speed on real scanout memory, are unmeasured here, since agents may not map a dumb buffer. Stage 01.4's Ahmed-only run covers the speed.
6. **A tool refused some compound shell commands** ("too complex to verify that it stays inside the worktree"), including a `git status` chained with other commands. Done: re-ran as plain separate commands, as the refusal itself says to. Nothing was skipped.
7. **Gate check: no test could see a kernel break the ABI, and the lint has a blind spot there.** Issue 5 said so for dirty YMM state and `rbx`, and left it to the lint and review.
   - Expected: `standards_lint`'s `asm_vzeroupper` to catch a missing `vzeroupper`.
   - Actual: it reads the `.asm` text only. `bitmask_and_batch`, `bitmask_or_batch` and `bitmask_andnot_batch` have their YMM use inside `BITMASK_BINARY_BODY`, in the `.inc`, so with the `vzeroupper` deleted from `bitmask_and_batch` the build passes the lint and every test.
   - Done: added ctest `asm_abi` in `tests/asm/abi/`. It enters each of the 15 SIMD kernels through `setcontext` with known values in `rbx`, `rbp`, `r12`-`r15`, lands on a function reached by the kernel's `ret`, and checks those registers and `XGETBV` register 1's AVX bit, for counts 0, 3, 9 and 200. Mutants, built together and undone: the `vzeroupper` above, `xor ebx, ebx` in both vec3 transforms, `xor ebp, ebp` in the popcount, `xor r12d, r12d` in `or`, `xor r15d, r15d` in `andnot`, and the copy's `push rbx` and `pop rbx` deleted. Each kernel was named in the failures, and `bitmask_and_batch` only at counts 9 and 200, the ones that use YMM. The lint is unchanged, because it would have to expand macros; this test covers what it misses. Passes in debug, release and sanitize.
8. **Gate check: Issue 4's "left alone" is settled with numbers.** See Stage 04.3 issue 9.
