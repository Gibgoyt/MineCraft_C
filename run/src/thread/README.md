# run/src/thread

**Purpose:** starts the threads `config/machine.yaml` asks for, each pinned to its own CPU, physical cores first.
**Tier:** Owner by depth. It reserves nothing. The stacks are `pthread`'s, outside our control like other memory inside
libc, and fixed at `THREAD_STACK_BYTES`.
**Owns:** nothing on the heap. A `thread_t` holds one running thread, and whoever started it joins it. `ring/` below this
directory takes its storage from the caller.
**Lifetime:** Process. `thread_topology_t` is read once at startup; a `thread_t` lasts from `thread_start` to `thread_join`.
**Threads:** `thread_topology_read` once, on the main thread, before anything is pinned. The rest may be called by any
thread, but one `thread_t` by one thread at a time.
**Public functions:**

- `thread_topology_read`: this machine's CPUs in placement order, from `/sys` and the process's affinity.
- `thread_topology_read_from`: the same from a directory and mask given by the caller, which is how tests see other
  machines.
- `thread_cpu_for`: the CPU of a role's n-th thread.
- `thread_pin_self`: pins the calling thread, for the main thread, which nothing here started.
- `thread_start`, `thread_join`: a pair; the new thread is on its CPU before its first instruction.

## Placement

The order is the first CPU of each physical core, in rising order, then every other CPU, in rising order. Roles take
places in it: main, sim, gpu, then workers, using the counts in `threads:`. `threads.reserved_os` is whatever remains at
the end. On the i9-10900K, whose CPU N and CPU N+10 share a core:

| CPUs | Who |
|---|---|
| 0, 1, 2 | main, sim, gpu |
| 3 to 9 | workers 0 to 6, each with a core to itself until the next row starts |
| 10 to 18 | workers 7 to 15, on the second hyperthread of cores 0 to 8 |
| 19 | left for the OS |

With `pin_physical_first: false`, the order is simply rising. With fewer CPUs than threads, for instance under `taskset`,
places wrap round and threads share, after a warning, so that the game still runs.

**May be called by:** `job` for workers and `game` for sim and gpu; `ring` by anything from this layer up.
**Depends on:** `file` for reading `/sys`, `log`, `assert`, glibc's pthreads, and generated `machine/index.h`.
