# tests/run/render/encode

**Purpose:** ctest `run_render_encode`. Tests `run/src/render/encode/`: one voxel's bytes worked out on paper, every shape of section against a brute-force pass over every voxel through the same lookup a kernel runs, the order on the wire, padding and overruns, and real workers writing what one thread writes.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main, and the real workers in one test.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/render/encode/`, `run/src/job/`, `run/src/thread/`, `run/src/time/`. No card: nothing here starts CUDA.

`sizes_and_time_on_this_machine` prints a terrain-shaped section's size and encode time, for the stage file, and checks nothing about the time.
