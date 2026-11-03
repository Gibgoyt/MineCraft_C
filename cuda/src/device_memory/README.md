# cuda/src/device_memory

**Purpose:** the device allocator, and the only code that calls `cudaMalloc` and `cudaFree`. Also where every CUDA result becomes a `gpu_status_t`.
**Tier:** the device side's `heap`. On the host it allocates nothing, and borrows its records from `run/src/gpu_bridge/`.
**Owns:** every block on the card, through a record per block, until a `device_block_t` holder frees it, or deinit does.
**Lifetime:** Process.
**Threads:** one at a time. Main starts it, and the `gpu` thread uses it once that exists. There are no atomics. CUDA's context is shared by every thread in the process, and device 0 is every thread's default, so no other thread has to say which device it wants: `tests/run/gpu_bridge/` runs a kernel from a second thread.
**Public functions** (`run/include/gpu_bridge/device_memory/index.h`, for C and CUDA):

- `device_memory_init`, `device_memory_deinit`: find the card and start CUDA; free what is left and stop.
- `device_memory_alloc`, `device_memory_free`: take and give back a block, from one of the three pools in `gpu.budget`.
- `device_memory_write`, `device_memory_read`, `device_memory_zero`: bounds-checked, waiting copies, for small and start-up work.
- `device_memory_self_check`: runs a real kernel over a real block and checks its answer.
- `device_memory_status_from_cuda`: the table from CUDA's errors to ours.
- `device_memory_lost_set`: the card has gone. From then on every block counts as given back without asking, in `device_memory_free` and `device_memory_deinit`, which also find this out for themselves.
- `device_memory_fault_now`: a real fault, by a kernel writing where nothing is mapped, for `--pretend-gpu-lost` and `tests/run/failure/`. Only this process's context goes, and the kernel's log gets a line.

Only for CUDA code (`cuda/include/device_memory/index.hu`):

- `device_memory_check`: every CUDA call in every area goes through this.
- `device_memory_pointer`: the pointer a kernel takes, from a block.

**May be called by:** `run/src/gpu_bridge/`, the other `cuda/` areas, and `render`, for blocks it owns.
**Depends on:** `gpu_bridge/status`, the CUDA runtime.

## The rules

- A failure is a value. Nothing here logs, or aborts, or throws: it fills `last_error`, returns the status, and the C side reports it with `gpu_error_log`.
- A pool's budget is checked before CUDA is asked, so a budget that is too small fails the same way on any card. A real `cudaMalloc` failure is then the card itself being out of room, usually because something else on the machine has taken it.
- Blocks come straight from `cudaMalloc`, one per call. The slab pool is a block that `cuda/src/world_buffers/` divides up. Nothing here is for a hot path.
- A stale handle can never free somebody else's block, because a record's generation changes with each block it holds, and `cudaMalloc` is free to return the same address again. Such a handle is refused with `GPU_STATUS_BAD_HANDLE`.
- Device memory has no untouched-pages trick: everything allocated is really taken. The desktop shares this card, and `free_bytes_at_init` is only what was free at that moment.
