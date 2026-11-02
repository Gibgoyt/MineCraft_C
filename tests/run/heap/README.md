# tests/run/heap

**Purpose:** ctest `run_heap`. Tests `run/src/heap/`: alignment, rounding, zeroing, huge page advice as the kernel reports
it, the counters, the budget, a real `mmap` failure, misuse aborting, and the counters under threads.
**Tier:** test.
**Owns:** nothing past each test, which releases what it reserves. The counters must read zero between tests.
**Lifetime:** One run.
**Threads:** main, 8 threads in one test, and child processes it forks and waits for.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, and `/proc/self/smaps` and `/proc/self/statm` as independent witnesses of what is mapped.

Not covered: a failed `munmap` while trimming a new mapping. Provoking it takes `vm.max_map_count` mappings, which is over
a million on this machine.
