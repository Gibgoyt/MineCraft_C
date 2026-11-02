# run/src/gpu_bridge/status

**Purpose:** what became of a call across the C and CUDA boundary: the `gpu_status_t` every CUDA area returns, and the `gpu_error_t` with the runtime's own words.
**Tier:** Arena, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process. The strings are static.
**Threads:** any.
**Public functions** (`run/include/gpu_bridge/status/index.h`):

- `gpu_status_name`: a sentence fragment for a status, for any value.
- `gpu_error_log`: writes one line for a failure. CUDA code never logs. It fills a `gpu_error_t` and returns, and the C side decides.

**May be called by:** `gpu_bridge` and its parts, `cuda/` (the header is compiled on both sides), and `render` for the statuses it is handed.
**Depends on:** `log`.

`GPU_STATUS_DEVICE_LOST` is the one a loop must stop for: CUDA's errors from a faulted kernel are sticky, so nothing after it can work. `cuda/src/device_memory/` holds the table from CUDA's errors to these.
