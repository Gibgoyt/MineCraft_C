# run/src/profiler

**Purpose:** the built-in profiler, as `perf` is not on this machine: per-thread scopes, the card's kernels beside them, a trace to look at and a table to read.
**Tier:** Owner.
**Owns:** `owned_region`, holding every lane, every lane's records, and the room it writes out from. All reserved at `profiler_init`, so no scope touches the heap.
**Lifetime:** Process, for a run started with `--profile`. Started first and stopped last, as `job` and `game` hold lanes in it.
**Threads:** a lane has one writer at a time, and no atomics. `profiler_lane_claim` from any thread. What reads the lanes, the last three groups of functions below, only once those threads have been joined or their jobs waited for.
**Public functions** (`run/include/profiler/index.h`):

- `profiler_limits_default`, `profiler_init`, `profiler_deinit`: a pair, and how much it keeps. Deinit is safe on a zeroed `profiler_t`.
- `profiler_lane_claim`, `profiler_lanes_claim`: a lane for a thread, or a row of them for a pool. `nullptr` if it is not started or none is left, and every call takes `nullptr` and does nothing.
- `profiler_begin`, `profiler_end`, through the macros `PROFILER_BEGIN` and `PROFILER_END`: a scope, nested in what is open on that lane.
- `profiler_begin_at`, `profiler_end_at`: the same at a time the caller gives, for tests.
- `profiler_span_add`: something that has already happened, on a clock of its own: the card's kernels.
- `profiler_summary_read`: what was kept, by scope name, most self time first.
- `profiler_trace_write`, `profiler_trace_dump`: all of it in Chrome's trace format.
- `profiler_summary_write`: the table for a person.

**May be called by:** anything above `heap`, `time`, `file` and `log`.
**Depends on:** `heap`, `time`, `file`, `log`, `assert`, `types`, generated `machine/index.h`, glibc.

## Running it

    tools/test-or-build.sh cmake --build --preset profile
    timeout 120 build/profile/bin/minecraft_c --headless --test-world --ticks 300 --profile /tmp/trace.json

The table goes to stderr at exit, and the trace opens in `about:tracing` or at `ui.perfetto.dev`. A release build has no scopes in it, so it refuses `--profile` and says which preset to build. The debug, sanitize and tsan presets keep them, which is what lets ctest see them work.

## Why it is the way it is

**A lane is passed by hand.** A `thread_local` found through a `static` would be a pointer hiding outside `app_t`, and `plans/CODING_STANDARD.md` section 4 allows that only where the OS gives no other way in. So `game_t` holds the lanes of its threads, and `job_worker_lane` gives a job its worker's.

**Records are written when a scope ends,** with its start, its length and its self time, and go round a ring. The newest are kept, so a long run keeps its end, and what went is counted per lane and shown in both outputs. A summary covers what was kept, not what was lost: when `lost` is not zero, the counts are of the last part of the run.

**Self time is duration less the scopes directly inside.** Nesting past `PROFILER_DEPTH_MAX` is counted as `too deep`, and its time stays with its parent. An end with nothing open is counted as `unmatched`, and a scope still open at the dump as `open`. A clean run has zero of all of these, and a number there means the scopes in the code are wrong, not the times.

**Times in a trace are whole nanoseconds shown as microseconds,** by integer arithmetic, so a trace's numbers are exactly what was recorded.

**Kernels have their own lane,** `card`. Their lengths are the card's own clock, `frame_timings_t` from `cuda/src/frame/`. That clock is not the host's, so where they sit on the timeline is put there by hand: end to end, the last ending as the draw came back to the host. Trust their lengths, and their place only to within the wait around them.

**A scope name is a string literal,** as a record keeps only the pointer. Records of one name in two literals still make one row, as the table falls back on the text.
