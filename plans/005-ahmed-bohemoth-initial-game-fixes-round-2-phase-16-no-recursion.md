# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 16: No recursion

**Status:** agent R1's part built and tested (recursion removed); agent R2's lint part in progress   (updated 2026-11-10)

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
- [x] 5. CUDA scan (`rules_c/index.c:645,702`): kernel launches `k<<<...>>>(` count as calls, bodies with qualifiers
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
- `7cf883a1` Block every crash signal while vt_guard's crash handler runs, so it is never entered again from inside itself.

## Issues found

### 2026-11-11, agent R1

**1. `job_spawn` is `[[nodiscard]] bool`.** With both queues full it undoes its `job_group_add`, counts the refusal and
returns false. The worker's counter `jobs_inlined` is now `jobs_refused` (same place, so `job_worker_t` and `job_stats_t`
keep their layouts); `inline_depth` became explicit padding. `job_run` resets the arena after every job and no longer
saves and restores the running priority, which only the inline path needed. `job/index.c` no longer includes `log`.

**2. Only two production callers.** `grep` finds `job_spawn` in the census and the atlas only (plus the job tests).

**3. Census.** Each slot has a `state` (`GAME_CENSUS_SLOT_FREE`, `RUNNING`, `DAYS_OWED`) and `days_next`, in the slot's
old padding (still 192 bytes). Each step, `census_slots_hand_out` gives every free slot a slot job while blocks are left
(`job_try_submit_at`, from the sim), and queues the days of a slot whose `job_spawn` was refused, setting the slot
`RUNNING` before its last day goes in. The slot job still loops over fast-week blocks, which are done in the reading, so
fast weeks cost no extra steps. A full week's block costs at most one sim step of latency per block per slot (up to 256
blocks a batch over 32 slots, so up to 8 steps a batch). `slots_submitted` and `slots_idle` became padding;
`game_census_t` is still 320 bytes. Results are byte-identical: `run_game_census` passed unchanged (31.7 s), as did
`run_climate.*`.

**4. Atlas.** `ui_atlas_t` gains `borrowed_bands`, `bands_owed` and `bands_next` from its `_pad1` (still 1152 bytes); the
band struct is now `ui_atlas_band_t`, declared in the header. `ui_atlas_frame` queues owed bands with `job_try_submit`;
`ui_atlas_draw_wait` waits for the drawing job, queues them with `job_submit`, and waits again.

**5. Job tests.** The stress tree is a loop on main submitting one `job_tree_parent` per node with children (21845), each
spawning its leaf children (`job_tree_leaf`). The test checks every node is visited exactly once per round, nothing is
refused, and `jobs_from_injector` equals the items plus 2 x 20 x 21845. The old overflow test (inline, in the same arena)
and the `job_chain` misuse test are replaced by `a_full_queue_makes_spawn_return_false_and_nothing_runs_inline`: past
4096 + 16384 + workers + 1000 spawns at least 1000 are refused, `jobs_refused` matches, every accepted child runs once, no
child runs on the parent's worker while the parent runs, and every job resets its arena. Tests that cannot fill a queue
use `spawn_with_room`, which ignores the result; their counts and `jobs_refused == 0` catch a refusal.

**6. Crash handler.** `sa_mask` holds all 13 crash signals and `sa_flags` is `SA_ONSTACK`. At the end the handler sets the
signal to `SIG_DFL`, raises it, and unblocks it, so it dies at once. The steps keep their atomic claim, so another thread's
crash goes on from the next step and never repeats one. The test now faults at `KD_TEXT` (first step) and `VT_SETMODE`
(last), each under `abort` and a real segfault: the child dies of `SIGSEGV`, the steps before the fault were done by the
child and none after, the guardian does the give-back, and "the program crashed" is not printed. The child sets
`RLIMIT_CORE` to 0 and the watchdog to 60 s: dying with no handler dumps core, which took longer than the rig's 200 ms
watchdog, so the watchdog answered instead of the pipe closing.

**7. The job-pointer audit.** `job_run` is now called only from `job_worker_main`, which is reached only as a thread entry
from `job_init_count`, and no job calls `job_init` (every caller is setup code in main, benches and tests). So once R2's
lint adds the edge `job_run → f` for each of the 77 submit and spawn sites, no `f` can reach `job_run` again: no job can
run inside itself. Nested queueing (a job submitting or spawning another) never runs on the same stack.

**8. Verification, 2026-11-11.** Debug build: exit 0, `standards_lint: no recursion`. The R1 test set
(`run_job.*|run_game_census|run_ui_.*|run_display_vt_guard|run_climate.*|clang_format|standards_lint`): 33 of 33 passed.
Full debug suite `-I 1,85`: 85 of 85 passed (904 s); `-I 86,400`: 76 of 76 passed (945 s), both without
`run_render_distance_live`. Release configure and build: exit 0, lint clean. Release `run_render_distance_live`: passed
(32.8 s). `git grep -n 'SA_NODEFER\|JOB_INLINE_DEPTH_MAX' -- run tests`: nothing. `pgrep -af` of this worktree's build and
`pgrep -a Xvfb`: nothing.
