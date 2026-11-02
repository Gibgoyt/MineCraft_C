# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 2: Heap and arenas

**Status:** done   (updated 2026-11-02)

## Goal

Reserve and release exist (2 MiB aligned, `MADV_HUGEPAGE`), with bump arenas and live counters.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/heap/`
- `run/{src,include}/arena/`
- `tests/run/heap/`
- `tests/run/arena/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement `heap_reserve`/`heap_release` in `run/src/heap/index.c`, the only code touching the OS allocator
- [x] Align regions to 2 MiB, apply `MADV_HUGEPAGE`, and keep live counters
- [x] Implement bump arenas with reset in `run/src/arena/index.c`
- [x] Add tests in `tests/run/heap/index.c` and `tests/run/arena/index.c`, registered as `run_heap` and `run_arena`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_heap$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_arena$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `282acbe` Add the heap with its 2 MiB aligned regions, budget and live counters, the bump arenas, their tests, and a leak check at exit.
- `a11e658` Start the core runtime from main so the heap check at exit means something, refuse an unsupported CPU before any -march code runs, and fill the test gaps and standard breaches the Phase 03 gate check found.

## Wiring outside the owned paths

- `run/src/main/index.c`, `run/include/main/index.h`, `run/src/main/README.md`: `main` calls `heap_exit_check` after
  `app_deinit` and exits with the new `MAIN_EXIT_LEAK` (4), which is the "heap counters must read zero at exit" of
  `plans/CODING_STANDARD.md` section 10.
- `plans/REPO_STRUCTURE.md`: the `heap` and `arena` rows, for the two decisions below.

## Decisions the plan left open

- **`arena_init` takes memory; it does not reserve.** The Owner calls `heap_reserve` and passes the region in. The lint
  checks only the words `heap_reserve` and `heap_release` by tier, so an `arena_init` that reserved would have let a
  depth 2 part reach the OS allocator with no breach reported. It also lets one region be split between arenas.
- **`heap_reserve` enforces `memory.process_budget_mib`.** The key was in `config/machine.yaml` with nothing reading it.
- **`arena_alloc` aborts on running out,** as `test_alloc` and `lint_arena_alloc` already do, so it cannot fail in the
  sense of `plans/CODING_STANDARD.md` section 6. `arena_alloc_try` is the form that returns `bool`.

## Issues found

1. **The head trim in `heap_map_aligned` never ran on this kernel.** Expected: a mapping needs trimming at both ends.
   Actual: with a span of `bytes + 2 MiB` the mutant `aligned = start` passed every test, so `head` was always 0.
   Evidence: mutation run, `SURVIVED no head trim keeps alignment wrong`, on Linux 7.2.7. The likely cause is the kernel
   aligning an anonymous mapping whose length is a multiple of 2 MiB; that is inferred, not read from kernel source, but
   changing only the length made this mutant get caught. Fixed: the span is now `bytes + 2 MiB - 4 KiB`, the true minimum.
2. **That fix moved the gap to the tail trim.** Expected: both ends trimmed. Actual: `SURVIVED no tail trim`, because the
   new mapping sits directly below an aligned one, so its end is aligned and `tail` is 0. Fixed in the test, not the code:
   `both_ends_are_given_back_below_a_mapping_that_ends_off_the_boundary` holds a thread stack of an odd length above the
   reservation, which is the layout the running game will have. After both fixes, all 14 mutants of `heap` and `arena` were
   caught by a test, and a leak planted before `main` made `minecraft_c --report` exit 4.
3. **`a_real_mmap_failure...` failed on the sanitize preset only.** Expected: `heap_reserve` returns false. Actual:
   `AddressSanitizer: out of memory: failed to allocate 0x1a0000 ... InternalMmapVector`, since an `RLIMIT_AS` of 1 MiB
   starved ASan's own runtime. Fixed in the test: the limit is now what is mapped plus 8 MiB, and the request is 64 MiB.
4. **The lint's tier check can be stepped around by a name.** It matches tokens containing `arena_alloc`, so a function
   called `arena_try_alloc` would pass unchecked at depth 3. Worked around by naming: every allocation function here
   starts `arena_alloc`, and the arena README says why. The lint itself is out of this stage's paths and was not changed.
5. **Not covered by a test:** a failed `munmap` in the trim, and the logging after it. It takes `vm.max_map_count`
   mappings to provoke (1048576 on this machine, measured in `/proc/sys/vm/max_map_count`). The branches are read, not run.
6. **Known limits, by decision:** arenas have no red zones under ASan, so running from one block straight into the next is
   not caught, because red zones would give the sanitize preset different limits from the build that ships. A stale
   `heap_region_t` copy goes undetected if something else has been mapped at its address since.
7. **Found by the Phase 03 gate check.** Expected: the "leak planted before `main`" run above to be held in place. Actual: it
   was a one-time run. The program reserved nothing, so no test could see `main` stop calling `heap_exit_check`. Fixed:
   `game_exits_with_its_own_status_on_a_leak` in `run_types` now fails on exactly that change. `log_capture_t._pad0` in
   `tests/run/heap/index.c` is now a byte array, per the standard's section 3. See the phase file's issues 1 and 4.
