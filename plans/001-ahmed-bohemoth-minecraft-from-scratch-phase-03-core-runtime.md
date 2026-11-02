# 001-ahmed-bohemoth-minecraft-from-scratch — phase 3: Core runtime

**Status:** done: all four stages verified and the gate passes from a clean build on all four presets. Nothing here waits on Ahmed. On branch `worktree-wf_916eee71-48c-10`, for the orchestrator to merge into the integration branch `worktree-wf_a4b549de-085-20`.   (updated 2026-11-02)

## Goal

The sanctioned types, entry point, heap, arenas, threads, rings, clock, file wrappers and job system exist under `run/`, and they obey the memory law.

**Wave:** W3.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-1-types-assert-log-main.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-2-heap-arena.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-3-thread-time-file.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-03-stage-4-job-system.md`

## Todo

- [x] Stage 03.1: Types, assert, log and main
- [x] Stage 03.2: Heap and arenas
- [x] Stage 03.3: Threads, time and file
- [x] Stage 03.4: Job system
- [x] Phase gate passed

## Verification

**Gate:** Sanitize preset clean; job stress test on all workers; heap counters zero at exit.

    tools/test-or-build.sh cmake --preset sanitize
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 600 ctest --preset sanitize --output-on-failure
    pgrep -a minecraft_c    # must print nothing

### Gate check results, 2026-11-02

On GCC 16.2 and nvcc 13.4.92 on the i9-10900K, from a deleted `build/`, on the code of `a11e658`:

| Check | Result |
|---|---|
| Configure, build and full `ctest`, presets sanitize, debug, release, profile | 14 of 14 on each |
| Every stage's own Verification commands, debug | 8 of 8 tests named by them |
| `ctest -R '^run_' --repeat until-fail:6`, sanitize | all pass |
| `ctest -R '^run_' --repeat until-fail:10`, release | all pass |
| `stress_on_all_workers_runs_every_job_exactly_once` under ThreadSanitizer, by hand as in Stage 03.4 issue 6 | exit 0, 0 reports |
| `pgrep -a minecraft_c`, `pgrep -af "$PWD/build"` | print nothing |

- **Sanitize preset clean:** the row above. `minecraft_c` itself now runs under it, with workers (30 of 30 `--pretend-leak`, 20 of 20 plain).
- **Job stress on all workers:** `run_job`. Each worker has to run jobs, each of 1.5 million submitted jobs and 40 rounds of a spawning tree exactly once, with stealing seen and no heap call while it runs.
- **Heap counters zero at exit:** the real program, not only tests: `heap at exit: 1 reserved, 1 released, 0 bytes live`, exit 0. Skipping the shutdown gives exit 4. Before this check the program reserved nothing, so its zero proved nothing: issue 1.

Mutants tried this round, each restored afterwards. The first group was already caught, the second is what the new tests catch. Rows 1 to 4 were built together, and so were the guard's three, since each is judged by its own test; the two "before this check" rows were also each run alone:

| Mutant | Result |
|---|---|
| Workers not pinned | `every_role_runs_where_it_was_placed` and 3 more fail |
| `MADV_HUGEPAGE` not called | `the_kernel_shows_the_huge_page_advice_and_gives_huge_pages` fails |
| Arena peak not kept | 2 tests fail |
| `time_now_ns` on `CLOCK_REALTIME` | `run_time`, run alone with `timeout 60`, had not finished when stopped: sleeping on a deadline from another clock. Left to the ctest timeout, it would fail there |
| `thread_topology_read` ignores `pin_physical_first` | passed all of `run_thread` before this check; now `the_real_read_applies_pin_physical_first_from_machine_yaml` fails |
| `main` ignores `heap_exit_check` | no test could see it before this check, with nothing reserved; now `game_exits_with_its_own_status_on_a_leak` fails, the only failure in `run_types` |
| Guard not in `.preinit_array` | `the_guard_runs_before_anything_compiled_with_march` fails |
| Guard's mask lacks AVX2 | `the_guard_requires_what_the_build_requires` fails |
| Decode reads AVX2 from bit 6 | `decode_reads_every_bit` and others fail |

## Commits

Appended as they land:

- `a11e658` Start the core runtime from main so the heap check at exit means something, refuse an unsupported CPU before any -march code runs, and fill the test gaps and standard breaches the Phase 03 gate check found.

## Issues found

Found by the gate check of 2026-11-02, whose default assumption was that the stages were not finished. Stubs, TODOs and ticked boxes with no code behind them: none found. All four stages' code is real, and the job and heap code stood up to reading and to mutants. What was wrong:

1. **Nothing called the core runtime, so the gate's third line was empty.**
   - Expected: "heap counters zero at exit" to say something about the program.
   - Actual: `minecraft_c` reserved nothing, so `heap_exit_check` could only pass, and no test could tell if `main` stopped calling it. `job`, `arena`, `time`, `heap_reserve` and `thread_pin_self` had no caller outside tests (`grep -rn` over `run/src`).
   - Done: fixed. `app_t` now holds the `job_system_t`. A plain run reads the topology, starts the workers, pins main, runs one job per worker, stops, logs the heap's counts and checks them. `--report` still starts nothing. Exit 5 is new, for a runtime that will not start. `--pretend-leak` and two `run_types` tests hold the leak check and the counts in place.
   - Left: `thread/ring` still has no caller outside `run_thread`. Nothing in the program yet has two threads with data to pass, and a made-up use would prove nothing. Its first user is the `sim`, `gpu` and `main` loop of Stage 08.4.
2. **Physical-first placement was not tested on the real read.** See Stage 03.3 issue 4.
3. **The CPU check could die before speaking on an older CPU.** Closed: see Stage 03.1 issue 1.
4. **Standard breaches that `standards_lint` does not see, all in tests.**
   - Expected: section 3 rule 2 (padding is `uint8_t` arrays) and section 6 (by-value structs are 32 bytes or less) to hold in every line.
   - Actual: `uint32_t _pad0` or `uint64_t _pad0` at `tests/run/job/index.c:114,368,483,530`, `tests/run/heap/index.c:46` and `tests/harness/index.h:30`. `stats_total` in `tests/run/job/index.c` returned the 64-byte `job_stats_t` by value. `run/` itself had none of these, by a token scan of `run/` and `tests/run/` for banned words, non-array padding, `libc_double_t` outside a cast, and mutable statics.
   - Done: fixed all. The harness line is outside this phase's paths, and it is one line. No lint rule added: the lint belongs to Phase 02, so a rule for padding field types, and the `libc_double_t` one from Stage 03.1 issue 5, remain possible follow-ups.
5. **`tests/run/types/README.md` said the tests never run a mode that could open a display.** Actual: `game_runs_and_exits_cleanly` has always run `minecraft_c` with no arguments, which is harmless only while no mode opens one. Fixed the README, and left a note in Stage 05.1's Issues, since that stage must move the run to headless.
6. **My first `--pretend-leak` was wrong.** Expected: exit 4. Actual: in `ctest --preset sanitize`, `game_exits_with_its_own_status_on_a_leak` hit its 30 s limit (`137 != 4`), and by hand 2 of 12 runs hung and others ended with an ASan `READ of size 1` report. `main` returned with the workers still running, and they read `app.job` on its dead stack. Fixed: that one path ends in `_exit` after the check. 30 of 30 give rc 4 since.
7. **Reviewed and left.** Stage 03.4 issue 4's epoch mutant is still not caught by a test. I read the fence pairing and the stop path in `job_wake_one`, `job_worker_sleep` and `job_deinit` and agree with the reasoning in their comments, which is a reading and not a run. Stage 03.1 issue 3 stands too: no ctest launches a CUDA kernel until Stage 08.1.
8. **How the checks were run.** The worktree guard refused several compound commands, among them a heredoc driving a Python edit script. I split them into plain commands and made the edits with the edit tools rather than move the commands somewhere it could not read them.
