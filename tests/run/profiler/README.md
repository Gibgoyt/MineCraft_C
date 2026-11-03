# tests/run/profiler

**Purpose:** ctest `run_profiler`. Tests `run/src/profiler/`, its wiring through `run/src/job/`, `run/src/game/` and `run/src/main/`, and the kernel timers of `cuda/src/frame/`.
**Tier:** test.
**Owns:** nothing outside the test arena, and a scratch directory under the build tree.
**Lifetime:** One run.
**Threads:** main, short-lived threads it joins, the job system's workers, and `game`'s real sim and gpu threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the built `minecraft_c`, and the real card, compute only.

What it holds to:

- Self time, counts and the table, worked out by hand on a clock that says what it is told.
- The ring keeps the newest and counts what went. Scopes that do not balance are counted and do not corrupt anything.
- A trace is valid JSON, checked by a parser here with no knowledge of how it was written, and every number in it reads back exactly. Past one buffer of output too. A full disk and a missing directory are reported.
- Eight real threads on eight lanes do not touch each other's, and take nothing from the heap.
- `--profile` on the real program: every lane clean, scopes nested as the code nests them, and each kernel's time against what `render_frame_report` says the same frames took. A kernel a flag turns off is not in the trace.
- The real threads: the sim's work on its lane, the gpu's on its, and one set of kernels for every frame.

The program always runs with `--headless` and stdin from `/dev/null`. It never comes near the display.
