# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 16: No recursion

**Status:** agent R1's part built and tested (recursion removed); agent R2's lint part with R2   (updated 2026-11-11)

## Goal

No function in the repository can reach itself, by any path: direct calls, calls through function pointers, jobs, or
signals. `CODING_STANDARD.md` section 4 bans recursion outright, and the lint proves it, including through job pointers
and signal handlers.

**Ahmed's words:** "Let's please now launch an agent to find recursive functions!!!"

## What was found, 2026-11-11

The build says `standards_lint: no recursion`, but its check reads GCC's `-fcallgraph-info`, which records a call through
a pointer as `__indirect_call` with no target. A full search (about 9,300 functions tokenised, a cycle search over all 404
release `.ci` files) found no direct or mutual recursion between named functions, and four cycles through a function
pointer or a signal:

| # | Cycle | Where |
|---|---|---|
| A | `census_day_job` → `census_slot_take` → `job_spawn` → inline `job_run` → `census_day_job` | `run/src/game/census/index.c:150-192`; `run/src/job/index.c:443-490` |
| B | `job_tree` spawns itself; it runs inline when the queues are full | `tests/run/job/index.c:255-263` |
| C | `job_chain` is a misuse test that deliberately hits the depth-32 abort | `tests/run/job/index.c:1264-1292` |
| D | `guard_on_crash` re-enters itself through `SA_NODEFER` | `run/src/display/vt_guard/index.c:172,214-215` |

**Root cause of A to C:** `job_spawn` ran the job inline, on the caller's stack, when both the worker's queue (4096) and
the injector (16384) were full, limited only by `JOB_INLINE_DEPTH_MAX` 32 and an abort. That is recursion by design.

**Ahmed's decision, 2026-11-11:** remove the crash handler's re-entry.

## Todo

### Remove the recursion (agent R1)

- [x] `job_spawn` never runs a job inline: with both queues full it returns `false` (`[[nodiscard]]`), as
  `job_try_submit` does, and the caller keeps the work. `job_run`'s inline path, `JOB_INLINE_DEPTH_MAX`, the depth abort
  and `inline_depth` are gone; header and README say so
- [x] Every `job_spawn` caller handles `false`: the census (below), the atlas, and the job tests
- [x] Census: the sim hands out blocks. A day job never takes the next block; the last day of a block marks it done and
  frees its slot, and the sim's next step gives that slot the next block. A full queue delays a block, or its days, by a
  step. The week's bytes are unchanged (`run_game_census`)
- [x] Atlas: a band `job_spawn` had no room for is queued later by main's draw loop (`ui_atlas_frame`, or at once in
  `ui_atlas_draw_wait`), never run on the drawing job's stack
- [x] `job_tree` builds its tree from a submitter loop on main (a job per node with children, spawning its leaves); no job
  spawns itself
- [x] `job_chain`'s depth-abort misuse test replaced by "a full queue makes `job_spawn` return false, and nothing runs
  inline"; every other job test kept
- [x] Crash handler: no `SA_NODEFER`. Every crash signal is blocked while the handler runs, the restore steps run once
  from a flat loop, and a fault inside a step ends the process with the kernel's default action, every earlier step
  having restored its part. Tests and README updated
- [x] Checked: `git grep -n 'SA_NODEFER\|JOB_INLINE_DEPTH_MAX' -- run tests` prints nothing, and no function passed to
  `job_spawn`, `job_submit*` or `job_try_submit*` can reach itself or a caller through a job

### Lint (agent R2)

- [ ] 1. Job pointers become edges: a token scan in `rules_c/` finds every function passed to `job_spawn`, `job_submit*`
  and `job_try_submit*`, and `graph/index.c` adds the edge `job_run → that function`. A fixture with a self-spawning job
- [ ] 2. Every other indirect call reviewed: a caller with an `__indirect_call` edge fails unless listed in
  `compile/src/standards_lint/indirect_calls.yaml` (caller, pointer type, why its targets cannot reach back), seeded with
  the 132 callers checked by hand. Fixtures for a missing and a listed entry
- [ ] 3. `SA_NODEFER` banned by a lint rule, with no exception. A fixture
- [ ] 4. Coverage: the target regex at `cmake/standards_lint.cmake:21` includes `mc_fuzz_.*` and `mc_soak`
- [ ] 5. CUDA scan (`rules_c/index.c:645,702`): kernel launches `k<<<...>>>(` count as calls, bodies with qualifiers
  between `)` and `{` are recognised, `static` names are keyed by file. Fixtures for each
- [ ] 6. Docs: `CODING_STANDARD.md` section 10 (recursion through indirect calls, the allowlist, `SA_NODEFER`), and the
  lint README line that admits pointers aren't followed

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 2400 ctest --preset debug -R '^(run_job.*|run_game_census|run_ui_.*|run_display_vt_guard|run_climate.*|clang_format|standards_lint)$' --output-on-failure
    tools/test-or-build.sh timeout 2700 ctest --preset debug -I 1,85 -E run_render_distance_live --output-on-failure
    tools/test-or-build.sh timeout 2700 ctest --preset debug -I 86,400 -E run_render_distance_live --output-on-failure
    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 900 ctest --preset release -R '^run_render_distance_live$' --output-on-failure
    git grep -n 'SA_NODEFER\|JOB_INLINE_DEPTH_MAX' -- run tests     # nothing
    pgrep -af "$PWD/build"; pgrep -a Xvfb                           # nothing

After R2 lands: reverting R1's census change in a scratch branch makes `mc_recursion_check` fail and name the cycle.

## Commits

Appended as they land:

- `a2321480` Make job_spawn return false when every queue is full instead of running the job inline, and let the sim hand out census blocks.

## Issues found

### 2026-11-11, agent R1
