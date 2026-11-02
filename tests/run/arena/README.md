# tests/run/arena

**Purpose:** ctest `run_arena`. Tests `run/src/arena/`: bump order, alignment, running out, huge sizes that must not wrap,
reset, and no heap calls while allocating. In the sanitize preset, also that ASan catches a stray access.
**Tier:** test.
**Owns:** nothing past each test, which releases what it reserves.
**Lifetime:** One run.
**Threads:** main, and child processes it forks and waits for.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/heap/` for the memory.
