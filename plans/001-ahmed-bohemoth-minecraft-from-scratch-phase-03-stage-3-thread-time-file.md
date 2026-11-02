# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3, stage 3: Threads, time and file

**Status:** done   (updated 2026-11-02)

## Goal

Pinning, rings, monotonic clock and file wrappers exist.

**Wave:** W3. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-core-runtime.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/thread/`
- `run/{src,include}/time/`
- `run/{src,include}/file/`
- `tests/run/thread/`
- `tests/run/time/`
- `tests/run/file/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement pinned threads, physical cores first, in `run/src/thread/index.c`, using counts from `machine.yaml`
- [x] Implement single-producer rings under `run/src/thread/`
- [x] Implement the monotonic clock in `run/src/time/index.c`
- [x] Implement file wrappers in `run/src/file/index.c`
- [x] Add tests registered as `run_thread`, `run_time`, `run_file`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

Notes:

- The ring is one producer, one consumer, in `run/src/thread/ring/`. It takes storage from its caller, so it needs no
  `arena_t`, and Stage 03.2 was not on the integration branch when this was written.
- No wiring in `run/src/main/` or `run/src/game/`. Nothing here needs an `_init`. `job` (03.4) and `game` (08.4) are the
  callers. `plans/REPO_STRUCTURE.md` gains the `ring` part and the `tests/run/thread/` row.
- Measured beyond the Verification section: `run_thread`, `run_time`, `run_file` pass on the release preset; all 11
  ctests pass on debug and sanitize; `run_thread` passes built by hand with `-fsanitize=thread`, with no race reported,
  exit 0.
- Not provable here: that `fsync` and the directory sync in `file_write_atomic` survive a power cut. The tests cover a
  killed process and a concurrent reader only.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_thread$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_time$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_file$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `dbab144` Add pinned threads placed physical cores first, single-producer rings, the monotonic clock and crash-safe file wrappers, with their tests.
- `0f38597` Make the file tests catch an in-place write and fail rather than hang, and record Stage 03.3 as done.
- `a11e658` Start the core runtime from main so the heap check at exit means something, refuse an unsupported CPU before any -march code runs, and fill the test gaps and standard breaches the Phase 03 gate check found.

## Issues found

1. **The first kill test for `file_write_atomic` could not fail.**
   - Expected: a test that kills the writer catches a file written in place.
   - Actual: with the temporary name changed to the path itself, so writes went in place, the test still passed, with
     256 KiB versions and 8 kills. Only two other tests failed. One `write` was too short a window for a kill to land in,
     and most of the time went on `fsync`.
   - Done: fixed. Versions are now 4 MiB and the parent also reads the file continuously until each kill. Three runs with
     the same change all fail on `bad_reads` at the test's final count check, and the real code passes.
2. **`run_file` hung instead of failing when signal handling broke.**
   - Expected: removing the EINTR retries fails the two pipe tests.
   - Actual: `timeout 120` killed the run. The side that gave up left its peer blocked on a pipe, and the test was joining
     that peer.
   - Done: fixed. The test closes the finished side's end before joining the peer, with SIGPIPE ignored around it. The
     same change now gives `FAIL read_exact_goes_on...` and `FAIL write_all_goes_on...`, with no hang.
3. **My test expected the wrong status from `file_make_dirs`.**
   - Expected by the test: `FILE_ERROR_NOT_FOUND` for a path that continues past a plain file.
   - Actual: `FILE_ERROR_EXISTS`, from `tests/run/file/index.c:301`. The first component in the way is the file, and that
     status is the documented one for it.
   - Done: changed the test, and made the status's comment in the header say this.

4. **Found by the Phase 03 gate check: the real read's placement order was not tested.**
   - Expected: `this_machine_is_read_correctly` to catch `thread_topology_read` ignoring `threads.pin_physical_first`.
   - Actual: with `MC_THREADS_PIN_PHYSICAL_FIRST != 0` replaced by `false`, `run_thread` passed 100%. This machine numbers its
     cores 0 to 9 and their siblings 10 to 19 (`cpu0/topology/thread_siblings_list` is `0,10`), so rising order already is
     physical first. Only the made-up `/sys` tests saw the difference, and they call `thread_topology_read_from` with the
     flag passed in.
   - Done: fixed. `the_real_read_applies_pin_physical_first_from_machine_yaml` narrows the affinity to both CPUs of a core and
     one CPU of a higher core, where the two orders differ, and restores it. The same mutant now fails it.

The ring's ordering check was also tried: publishing `tail` before copying the element fails both two-thread stress
tests.
