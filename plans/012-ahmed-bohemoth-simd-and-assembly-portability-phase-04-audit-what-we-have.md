# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 4: Audit what we have

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012, and for phases 02 and 03.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md` (Issues found 1, 2
and 6), then the master `plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 1.1, 1.3 items 4, 5 and 8,
and Q6 and Q7), then phase 03's file for `history.tsv`.

## Goal

**Every kernel in `asm/` is one the game runs, at a length where it wins, and the constants question is settled by a
measurement.**

- **The 11 kernels with no game caller** (master 1.1) are each either wired into a real caller, with that caller's time
  measured before and after in `bench/simd/history.tsv`, or deleted with their prototype, reference, tests, bench rows and
  README lines. Nothing is kept "in case".
- **Popcount's short arrays have a rule.** At the lengths a caller uses, the kernel wins, or that caller uses
  `stdc_count_ones` (C23 `<stdbit.h>`; with `-march=skylake` GCC emits `popcnt`). The prototype's "for 16 words and up"
  is held by a gated bench row.
- **The constants.** One measurement compares constants in `.text` (today) with constants in `MC_RODATA` under a scoped
  `[warning -reloc-rel-dword]`, on the kernels with the most constants (noise, blend). They move only if the gain is beyond
  noise; otherwise `.text` stays and the reason stays in `asm/include/conventions/index.inc:9-10`, rewritten with this
  phase's numbers.

## Where this fits

**The vision.** "I am not sure if we are properly writing assembly SIMD!" Part of "properly" is the standard's own rule:
"Benchmarks must show that each kernel beats its scalar C reference. If one does not, it gets deleted"
(`plans/CODING_STANDARD.md:327`). A kernel nothing calls has no gain in the game at all.

**What it reads:** phase 02's layer (every kept kernel is already ported) and phase 03's harness (a removed kernel's
recipe is removed with it, so `every_exported_function_has_a_recipe` keeps holding).

**What later phases read from it:** a smaller, all-used `asm/`, and the decided popcount and rodata rules, which phase 05's
kernels follow.

**What this phase must NOT build:**
- No new kernel (phase 05).
- No change to what a kept kernel computes.
- No caller wired just to keep a kernel alive. The caller must be a hot path, and its time must drop.

## Ahmed's words

- "I am not sure if we are properly writing assembly SIMD!"

---

## Background, explained

### The 11, with a candidate caller and a default

Read 2026-11-17. "Gain" is from `bench/simd/README.md`, 2026-11-02.

| Kernel | Gain in bench | Candidate caller (to measure) | Default if the candidate does not win |
|---|---|---|---|
| `copy_stream_batch` | 8.32x (1080p) | None. It is one row of `copy_stream_rows_batch` (`asm/src/copy/index.asm:18-24`), which has 3 callers | **Delete** |
| `blend_premul_batch` | 16-25x | None found: `run/src/ui/compose/index.c:99` uses `blend_over_batch`, and no `run/src/` file but `simd/` names premultiplied | **Delete** |
| `bitmask_popcount_batch` | 0.84x at 8 words, 1.76x at 64 | `render_encode_section` (`run/src/render/encode/index.c:115-121`) counts a section's voxels with `stdc_count_ones` over 64 brick words. The count equals the popcount of `occupied` (64 words, a length where the kernel wins) | Wire it there if the encode's time drops; else **delete** |
| `bitmask_and_batch`, `_or_`, `_andnot_` | 1.3x at 8 words, 2.1x at 64 | To find: section mask combining (light, render occlusion). None found by grep | **Delete** |
| `bitmask_from_nonzero_u8_batch` | 43.68x (4096) | To find: a byte-per-voxel array turned into a mask (light levels?) | **Delete** |
| `bitmask_from_equal_u16_batch` | 28.56x (4096) | To find: "every voxel of block X" (water, a palette entry) in encode or tick | **Delete** |
| `vec_batch_transform_points`, `_directions`, `_mul_vec4` | 3-6x | To find: CPU-side transforms of many points (entity bounds, culling). Rendering transforms run on the card | **Delete** |

A deletion removes, for each kernel:
- its lines in `asm/src/<k>/index.asm` and `asm/include/<k>/index.inc`, the whole directory when empty;
- its prototype in `run/include/simd/<k>/index.h`;
- its reference in `run/src/simd/reference/`;
- its check in `run/src/simd/index.c`;
- its test in `tests/asm/<k>/`;
- its recipe in `tests/asm/abi/`;
- its rows in `bench/simd/`;
- its README lines, and `plans/REPO_STRUCTURE.md` section 8's row (left for phase 07 to write).

### Popcount below 16 words

Measured gains (master 1.3 item 5): 1 word 0.46x, 2 0.50x, 4 0.66x, 7 0.67x, 8 0.84x, 12 1.00x, 15 0.94x, 16 1.07x. The
README already records that a scalar path inside the kernel "did not cure it (8 words 0.95x, 7 words 0.58x), because the
reference has the same instruction at one per cycle and no call to make".
- **The rule:** a caller with fewer than 16 words uses `stdc_count_ones`.
- **The bench:** the gated row stays at 24 words.
- **The prototype's comment:** it gains "callers below 16 words: use stdc_count_ones".

### Constants: what is true

- From `.rodata`, `vmovdqa ymm0, [k]` fails `-w+all -Werror` with `reloc-rel-dword` under both `elf64` and `win64` (NASM
  3.02, 2026-11-17; phase 00 Issues found 6). It is a warning about a relocation the linker resolves fine.
- `asm/src/cpuid/index.asm:269-271` already scopes one such warning: `[warning push]`, `[warning -reloc-abs-qword]`,
  `[warning pop]`.
- So `MC_RODATA` would open the section and turn the warning off for that file only, and `standards_lint` checks the push
  and pop pair.
- What `.text` costs: the constants share L1i lines and iTLB entries with the code. Whether that matters on Comet Lake for
  kernels of a few hundred bytes is exactly what is measured.

## Owned paths

This phase may edit these, plus this file:
- `asm/src/{copy,blend,bitmask,vec_batch,noise}/`, `asm/include/{copy,blend,bitmask,vec_batch,noise}/`
- `run/include/simd/{copy,blend,bitmask,vec_batch}/`, `run/src/simd/index.c`, `run/src/simd/reference/`
- `tests/asm/{copy,blend,bitmask,vec_batch,simd,abi}/` (the deleted kernels' parts only)
- `bench/simd/` (rows and `history.tsv`)
- `run/src/render/encode/index.c` (the popcount wiring only, if it wins), and any other caller a row above names, after
  its owner's README is read
- `asm/include/conventions/` (`MC_RODATA` and the comment at `:9-10`) and `compile/src/standards_lint/rules_asm/` (the
  warning pair check), only if constants move

**Not touched:** `asm/src/cpuid/`, `asm/src/embed_assets/`, `config/`, `cuda/`.

## Steps

1. **For each candidate caller**: read its owner's README; time it in release with the kernel and without it; record both
   in `history.tsv` with a label. *Why: a gain in `bench_simd` is not a gain in the game until the caller says so.*
2. **Wire the winners**, each in its own commit, with its caller's tests passing. *Why: one caller per diff.*
3. **Delete the rest**, one commit per kernel file. *Why: a deletion is easy to review when it is the only thing in the
   diff.*
4. **Popcount**: the rule in the prototype and README, the bench rows re-run. *Why: the rule must be where the next
   caller looks.*
5. **Constants**: the measurement on noise and blend (release, best of 3, under the lock); move or keep, with the numbers
   in `asm/include/conventions/index.inc`'s comment. *Why: the comment states a reason; this makes it a measured one.*

## Todo

From the master (section 8, phase 04):

- [ ] Each of the 11 uncalled kernels wired into a real caller with a measured gain, or deleted
- [ ] Popcount's short-array rule: `stdc_count_ones` below 16 words; gated row held
- [ ] Constants: measured in `.text` and in `MC_RODATA` with the scoped warning; moved only on a gain

Expanded:

- [ ] `render_encode_section`'s voxel count tried with `bitmask_popcount_batch(occupied, ENCODE_WORDS)`; its time recorded either way
- [ ] Every deletion also removes the reference, self-check, test, ABI recipe and bench rows; `asm_abi`'s recipe check still passes
- [ ] `bench/simd/README.md` results table re-run and replaced, with the date
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test (ctest) | Checks | Numbers | Fails when |
|---|---|---|---|
| `asm_<kernel>` for each kept kernel (existing) | References | unchanged | A kept kernel changed |
| `asm_abi` `every_exported_function_has_a_recipe` | The catalog after deletions | rows = recipes, 0 unmatched | A deletion half done |
| `asm_simd` / `simd_self_check` (existing) | The startup check over what is left | passes | A deleted kernel still checked |
| `run_render_*` (existing, if popcount is wired) | Encode output | bytes and pictures unchanged | The count changed |
| `bench_simd` (release, not a ctest) | Every kept row | every gated row ≥ 1.0x; popcount's 24-word row gated | A kept kernel loses |
| The caller's own timing (release) | Before and after | the caller's time drops beyond the run-to-run spread of 3 runs, or the kernel is deleted | A kernel kept for nothing |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(asm_.*|run_render_.*)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^asm_' --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_simd
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed reads the table of the 11, now with each kernel's verdict and its numbers, and the before-and-after bench table. He
says yes or names a kernel to keep. A kept kernel needs a caller (Q7).

## Risks and open questions

| Risk or question | Default |
|---|---|
| Q7: delete a kernel with a gain but no caller? | Yes (the standard's rule, and nothing kept that nothing runs) |
| Q6: constants in rodata? | Stay in `.text` unless measured faster |
| A deleted kernel is wanted later | Git keeps it; the deletion commit names it, so `git log -S<name>` finds it |
| The popcount wiring changes the encode's output | It cannot: same count, different method; `run_render_*` pictures prove it |
| A candidate caller is in a module owned by an open plan | Read that plan's phase file first; if it is active, record and leave the kernel for that plan |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
