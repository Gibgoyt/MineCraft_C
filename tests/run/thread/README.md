# tests/run/thread

**Purpose:** ctest `run_thread`. Tests `run/src/thread/` and `run/src/thread/ring/`: placement on made-up machines, placement
on this one against `/proc/cpuinfo`, real pinning of every role in `config/machine.yaml`, and a ring under load between two
pinned threads.
**Tier:** test.
**Owns:** nothing outside the test arena, and made-up `/sys` trees under the scratch directory it is given.
**Lifetime:** One run.
**Threads:** main, and the pinned threads it starts and joins. They report through memory, and main does the checking,
because harness checks are not thread-safe.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes `build/<preset>/test_tmp/run_thread` as the scratch directory.
**Depends on:** `tests/harness/`, `run/src/thread/`, `run/src/file/`, `/proc/cpuinfo` as an independent reference.

`tests/run/ring/` does not exist, because the stage names one test for threads and rings together: they are one system.
