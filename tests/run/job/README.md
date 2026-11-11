# tests/run/job

**Purpose:** ctest `run_job`. Tests `run/src/job/`: a stress run on all workers with every job counted, all workers running
at once on their own CPUs, stealing that has no other way to finish, Job arena resets, each queue running out, a full
queue making `job_spawn` return false with nothing run inline, wakeups around going to sleep, no CPU use when idle, priorities running highest first, dropping a priority, the far field's cap with
near work never waiting for it, the far field alone getting more than its cap, workers kept off a capped queue sleeping and
coming back when it lifts, draining at `job_deinit`, failed starts, and misuse aborting.
**Tier:** test.
**Owns:** nothing past each test, which stops its own job system. The heap counters read zero between tests.
**Lifetime:** One run.
**Threads:** main, the `MC_THREADS_WORKERS` workers of each job system, up to three producers, and child processes. Jobs
report through atomics and main does the checking, because harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/job/`, `run/src/thread/`, `run/src/heap/`, `run/src/time/`, and `/proc/self` as
an independent witness of threads and mappings.

`a_worker_failing_to_start_stops_the_ones_already_running` does not exist in the sanitize preset, because ASan needs
mappings that its `RLIMIT_AS` would refuse. It re-runs this program with `TEST_FILTER` to get a process with no cached thread
stacks. The sanitize preset's own addition is checking that the pointer a finished job kept is poisoned.

No preset has ThreadSanitizer, so that run is by hand: the stage file gives the command and the result. A lost wakeup hangs
rather than fails, so it shows up as the ctest timeout. One wakeup bug is known to get past this test: see Stage 03.4 issue 4.
