# tests/run/time

**Purpose:** ctest `run_time`. Tests `run/src/time/`: the clock against glibc's directly, across threads, and sleeps
against what the clock then reads, with a signal arriving during a sleep.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main and short-lived threads it joins.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/time/`.

Sleeps are checked strictly on the short side, which is a promise, and loosely on the long side, because another build on
this machine can hold the CPU for a good while.
