# run/src/job

**Purpose:** runs small pieces of work on `MC_THREADS_WORKERS` pinned threads, with idle workers stealing from busy ones.
**Tier:** Owner.
**Owns:** one `heap_region_t`, holding the workers, every queue and every Job arena. The `thread_t` of each worker.
**Lifetime:** Process for the system; Job for each worker's `arena_job`, which is reset after every job.
**Threads:** `job_init` and `job_deinit` on one thread. Workers are started here. `job_try_submit`, `job_stats_read` and
`job_sleeper_count` from any thread; `job_submit` and `job_group_wait` from any thread but a worker; `job_spawn` only from
inside a job, on the worker it was handed.
**Public functions:**

- `job_init`, `job_init_count`, `job_deinit`: a pair, the first with all of `MC_THREADS_WORKERS`, the second with as many of them as the `settings` worker count says. `job_deinit` first runs whatever is still queued.
- `job_group_init`, `job_group_deinit`: a pair, for a group of jobs one thread can wait on.
- `job_try_submit`, `job_submit`: queue a job from outside the workers. One returns false when the queue is full, the other
  waits for room.
- `job_spawn`: queue a job from inside a job. Cannot fail.
- `job_group_wait`: sleep until a group's jobs have all finished.
- `job_stats_read`, `job_sleeper_count`: counters for tests and the debug overlay.
- `job_profiler_set`, `job_worker_lane`: each job a scope on its worker's lane in `run/src/profiler/`, and that lane for a job's own scopes.

**May be called by:** `game`, `terrain`, `world/light`, `render/encode`, `save`, `mob`: anything above this layer.
**Depends on:** `thread` for pinned threads, `heap`, `arena`, `log`, `assert`, futex through `syscall`, and generated
`machine/index.h`.

## How work moves

| Queue | Who adds | Who takes | Kind |
|---|---|---|---|
| Each worker's own | That worker, through `job_spawn` | That worker from the newest end, the others from the oldest | Chase-Lev deque, fixed size |
| The shared injector | Any other thread, through `job_submit`; a worker whose own queue is full | Any worker | Vyukov bounded queue |

A worker looks in its own queue, then the injector, then steals, starting from a random worker. There are no locks. A job
that spawns stays cheap: a push is a few plain stores, plus one fence to find out whether anybody needs waking.

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
how this module works rather than a fact about the machine. The whole region is 516 MiB of address space. It counts
against `memory.process_budget_mib`, while only the pages touched use real memory.

`main` holds the `job_system_t`, in `app_t`, and calls `job_init` and `job_deinit`. `game` borrows it.
