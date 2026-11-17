# 012-ahmed-bohemoth-simd-and-assembly-portability — phase 7: The final check

**Status:** not started   (updated 2026-11-17). Waits for Ahmed's go on plan 012, and for phases 01 to 06.

Read first: `plans/012-ahmed-bohemoth-simd-and-assembly-portability-phase-00-vision-and-plan-files.md`, then the master
`plans/012-ahmed-bohemoth-simd-and-assembly-portability.md` (sections 1, 7 and 9), then the Issues found of phases 01 to 06.

## Goal

Plan 012 is closed:
- **The suites.** The full debug and release suites pass, apart from failures shown to fail on main itself, each named
  with its evidence.
- **The bench.** One table compares every kernel, before plan 012 (`bench/simd/README.md`'s 2026-11-02 table) and after
  (this phase's release run, also appended to `bench/simd/history.tsv`).
- **What stays unproven is written down:** Win64 unwind data never run by a Windows unwinder (master Q4), and the
  Windows and arm64 `CMAKE_SYSTEM_PROCESSOR` spellings (phase 01).
- **The docs.** `plans/REPO_STRUCTURE.md` says what plan 012 changed:
  - section 8's table: `conventions` is the layer; deleted kernels gone; `abi_probe` added;
  - section 13's "A new NASM kernel": `MC_FUNC`, the `asm_abi` recipe, the bench row;
  - section 15.1: `platform.os` and `platform.arch` now have consumers;
  - section 15.6's `asm/` row: both x86-64 ABIs, and the arm64 design in phase 06.
- **The gate.** Ahmed's gate notes are written here in his words.

## Where this fits

**The vision.** Ahmed's "I am not sure if we are properly writing assembly SIMD!" gets its answer as a passing suite, a
bench table, and a harness he can run.

**What this phase must NOT build:**
- No new behaviour. A fix found here is the smallest that makes a test pass, recorded.
- No new kernel, and no Windows or arm64 build.
- No edit to the master (README rule 1). Corrections go in the phase files.

## Ahmed's words

- "I am not sure if we are properly writing assembly SIMD!"
- "We should always store findings in plan files, as opposed to keeping it inside Claude's GPU's memory where we are unable
  to access it! You need to document your findings"

---

## Background, explained

Nothing in plan 012 changes the world's bytes: phase 02's ports and phase 05's kernels are bit-identical by test. So no
generator version moves, no save format changes, and no world needs deleting. Worlds saved before plan 012 open unchanged.
The one yaml change is the new `platform.arch` key (phase 01). A `config/machine.yaml` without it is refused at configure
with "missing key 'platform.arch'", which is the intended behaviour, and it is written in the README of
`compile/src/machine_config/`.

## Owned paths

This file. `plans/REPO_STRUCTURE.md` sections 8, 13, 15.1 and 15.6. `bench/simd/README.md` (the results table). Only the
smallest fixes a failing test needs, in the paths phases 01 to 05 owned, each recorded.

## Steps

1. **Rebase onto main** if it moved, and record it. *Why: the suites must run on what will merge.*
2. **The debug suite, whole.** A failure is run alone, and against main if it fails alone; record both. *Why: a closed
   plan says what passes.*
3. **A release build and the release suite.** *Why: the release build is what Ahmed plays and what the bench means.*
4. **`bench_simd`**, release, under the lock, best of 3 runs; the before-and-after table written into
   `bench/simd/README.md` and below. *Why: the plan promised no kernel slower and every kept kernel winning.*
5. **The Win64 objects once more**: `asm_conventions`' unwind decode passing, plus the `objdump -d` of one kernel pasted
   here. *Why: the closest thing to a Windows test we have.*
6. **`plans/REPO_STRUCTURE.md`** as the Goal lists. *Why: it is how the next agent finds things.*
7. **Ahmed's gate**, then his notes here. *Why: he decides when it is right.*

## Todo

From the master (section 8, phase 07):

- [ ] Full debug and release suites
- [ ] The bench table, before and after
- [ ] Ahmed's gate notes, in his words
- [ ] `plans/REPO_STRUCTURE.md` updated (sections 8, 13, 15.1, 15.6)

Expanded:

- [ ] Each failing test run alone and against main, recorded
- [ ] What remains unproven listed here: Win64 unwind data, the Windows and arm64 processor spellings
- [ ] `plans/012-*` phase files' todos and statuses current; the master left as it is (verbatim)
- [ ] `pgrep -af "$PWD/build"` prints nothing at the end

## Tests

| Test | Checks | Numbers |
|---|---|---|
| Debug suite | Everything | All pass, or each failure shown as load or as main's |
| Release suite | Everything | The same |
| `asm_abi`, `asm_conventions` (release) | Both ABIs, unwind decode | pass |
| `bench_simd` (release) | Every kept kernel | every gated row ≥ 1.0x; no kept kernel's ns/element worse than before beyond 3 runs' spread |

## Verification

    git log --oneline main..HEAD
    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3600 ctest --preset release --output-on-failure -j 2
    tools/test-or-build.sh timeout 600 build/release/bin/bench_simd
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed:
1. Runs `tools/test-or-build.sh timeout 900 ctest --preset debug -R '^asm_abi$' --output-on-failure -V` and reads what it
   checks.
2. Reads one kernel (`asm/src/blend/index.asm`) and `asm/include/conventions/README.md`.
3. Reads the before-and-after bench table.
4. Plays a new world on the release build and says whether it feels as it did, or faster.

His notes go here, in his words. Any change he asks for is recorded as an Issue found or a new plan.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A suite failure that is main's own | Shown on main, left for its owner, recorded |
| A bench row slower than before | Rerun 3 times; if it holds, it is a phase 02 or 04 regression, fixed or the kernel's decision revisited, recorded |
| Q4: Win64 unwind data unproven | Stays written as unproven until a Windows machine runs one exception through a kernel |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
