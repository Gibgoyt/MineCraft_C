# run/src/job

**Purpose:** runs small pieces of work on `MC_THREADS_WORKERS` pinned threads, with idle workers stealing from busy ones.
**Tier:** Owner.
**Owns:** one `heap_region_t`, holding the workers, every queue and every Job arena. The `thread_t` of each worker.
**Lifetime:** Process for the system; Job for each worker's `arena_job`, which is reset after every job.
**Threads:** `job_init` and `job_deinit` on one thread. Workers are started here. `job_try_submit`, `job_try_submit_at`,
`job_priority_drop`, `job_stats_read`, `job_sleeper_count` and the `job_priority_` counters from any thread; `job_submit`,
`job_submit_at` and `job_group_wait` from any thread but a worker; `job_spawn` and `job_dropped` only from inside a job, on
the worker it was handed.
**Public functions:**

- `job_init`, `job_init_count`, `job_deinit`: a pair, the first with all of `MC_THREADS_WORKERS`, the second with as many of them as the `settings` worker count says. `job_deinit` first runs whatever is still queued.
- `job_group_init`, `job_group_deinit`: a pair, for a group of jobs one thread can wait on.
- `job_try_submit_at`, `job_submit_at`: queue a job from outside the workers, at a `job_priority_t`. One returns false when
  that priority's queue is full, the other waits for room. `job_try_submit` and `job_submit` are the same at
  `JOB_PRIORITY_NORMAL`.
- `job_priority_drop`, `job_dropped`: mark what is queued at a priority as no longer wanted, and ask from inside a job
  whether it is one of those.
- `job_spawn`: queue a job from inside a job. Cannot fail.
- `job_group_wait`: sleep until a group's jobs have all finished.
- `job_stats_read`, `job_sleeper_count`, `job_priority_queued`, `job_priority_running`, `job_priority_cap`: counters for
  tests and the debug overlay.
- `job_profiler_set`, `job_worker_lane`: each job a scope on its worker's lane in `run/src/profiler/`, and that lane for a job's own scopes.

**May be called by:** `game`, `terrain`, `world/light`, `render/encode`, `save`, `mob`: anything above this layer.
**Depends on:** `thread` for pinned threads, `heap`, `arena`, `time` for the hold, `log`, `assert`, futex through
`syscall`, and generated `machine/index.h`.

## How work moves

| Queue | Who adds | Who takes | Kind |
|---|---|---|---|
| Each worker's own | That worker, through `job_spawn` | That worker from the newest end, the others from the oldest | Chase-Lev deque, fixed size |
| A shared injector for each priority | Any other thread, through `job_submit_at`; a worker whose own queue is full | Any worker | Vyukov bounded queue |

A worker looks in the injectors of a higher priority than its own queue's newest job, then its own queue, then the other
injectors, highest priority first, then steals, starting from a random worker. There are no locks. A job that spawns stays
cheap: a push is a few plain stores, plus one fence to find out whether anybody needs waking.

## Priorities

Plan 005 phase 14 found the far field filling every worker for minutes at render distance 64, at about a second a column,
while the ground underfoot and the re-encode of a mined block waited behind it in one queue. So each job has a priority,
highest first:

| Priority | For | Who |
|---|---|---|
| `JOB_PRIORITY_EDIT` | Encoding sections, a block edit's among them: a view of them holds up the next | `render/upload` |
| `JOB_PRIORITY_NEAR` | Columns within `GAME_NEAR_CHUNKS` of the player | `game` |
| `JOB_PRIORITY_COLUMN` | The other full-detail columns, nearest first as the map hands them out | `game` |
| `JOB_PRIORITY_NORMAL` | Everything not given one: mob paths, autosave, the map, tests, benches | the rest |
| `JOB_PRIORITY_CLIMATE` | Climate tiles and pages, and the census | `climate`, `game/census` |
| `JOB_PRIORITY_FAR` | Far-field columns | `render/lod` |

- **Order.** A worker never takes a lower priority's job while a higher priority has one it may take. Within one priority
  it is first come, first served. A running job is never stopped, so what arrives waits for a worker to finish one.
- **The cap.** While any job of a higher priority is queued or running, at most a quarter of the workers, rounded up,
  run `JOB_PRIORITY_FAR` jobs at once (`JOB_FAR_WORKERS_DIVISOR`), so the rest are free for the near work. With nothing
  above it, the far field may have every worker: a fixed cap made `render_feature_lod`, where the far field is all there
  is, take 481 s against its 590 s timeout. A job already running is never stopped, so near work that arrives while the far
  field has every worker waits for one far-field job to end. Workers the cap keeps out go to sleep, and any job from a
  shared queue that finishes wakes one while a capped queue has jobs.
- **The hold.** The cap also holds for `JOB_CAP_HOLD_NS`, 100 ms, after a job above it was last queued or finished. The
  game's columns come in bursts: the sim thread takes each finished one in, at 1 to 2 ms a column, and so is what sets the
  pace, and in the gaps nothing near is queued or running. Without the hold each gap lifted the cap, the far field started
  a second-long job on every worker, and those workers, on the same cores as the sim thread and the near columns by
  hyperthreading, made both about 2.2 times slower: after render distance 16 to 64 the near rings took 11.6 s with the
  cap at one worker as with it at a quarter, and 4.65 s with the hold. A worker kept out by the hold alone sleeps with an
  alarm for when it ends, as then no job finishing comes to wake it.
- **Dropping.** `job_priority_drop` moves a priority's epoch. A job queued under an older one reads `job_dropped` as true,
  and so does what it spawns. It still runs, so that its group finishes and its owner hears of it: it looks first and
  returns. `render/lod` drops its queued columns when the eye moves to another column or the far field changes or closes,
  and asks again for what is still wanted, nearest the new place first.
- **Spawned jobs** keep the spawning job's priority and its epoch, but the cap counts only what is taken from an injector,
  and stealing takes no account of priority. It only happens once every injector is empty or capped.

With nothing to do, a worker keeps looking for 64 rounds, then sleeps on a futex. It costs no CPU while asleep, and a
submit makes a syscall only if some worker is asleep. See the comments on `job_wake_one` and `job_worker_sleep` for why no
wakeup is lost.

## Rules for a job

- **Everything from `worker->arena_job` dies when the job returns.** A job finds the arena empty. Results go through its
  argument, which belongs to whoever waits on the group.
- **A job cannot wait** for other jobs. `job_group_wait` aborts on a worker, since workers all waiting would leave nobody
  to run anything. Spawn the remaining work as a job instead, or let the thread outside that waits on the group do it.
  This also means a Job arena never has to hold two unrelated jobs' memory at once, apart from the next rule.
- **An inlined job shares the arena.** With this worker's queue and the injector both full, `job_spawn` runs the new job on
  the spot. It finds the arena in use rather than empty, its allocations last until the outermost job ends, and past 32 such
  jobs on one stack the program aborts, like an arena running out: that budget is a bug to fix, not a state to carry on from.
- **No job waits on a lock another job holds**, for the same reason as waiting.

## Sizes

`JOB_QUEUE_CAPACITY`, `JOB_INJECTOR_CAPACITY` and `JOB_ARENA_BYTES` are in the header, not `config/machine.yaml`. They are
how this module works rather than a fact about the machine. Each priority has its own injector of
`JOB_INJECTOR_CAPACITY`. The whole region is about 518 MiB of address space. It counts
against `memory.process_budget_mib`, while only the pages touched use real memory.

`main` holds the `job_system_t`, in `app_t`, and calls `job_init` and `job_deinit`. `game` borrows it.
