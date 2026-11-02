# run/src/gpu_bridge

**Purpose:** the C side of the card: starts and stops CUDA, and holds what the areas of the C API borrow.
**Tier:** Owner.
**Owns:** `owned_host` with `arena_host` over it, which holds the device allocator's records, and through `device_memory` every block on the card.
**Lifetime:** Process.
**Threads:** `main` for init and deinit; `gpu` in between, once that thread exists.
**Public functions** (`run/include/gpu_bridge/index.h`):

- `gpu_bridge_limits_default`: `gpu.budget` from `config/machine.yaml`, and room for 1024 live blocks.
- `gpu_bridge_init`, `gpu_bridge_deinit`: start CUDA and run its self-check; free what is left and stop. Deinit is safe on a zeroed `gpu_bridge_t`.
- `gpu_bridge_exit_check`: names every block still live, for `main` to call before deinit.

**May be called by:** `run/src/main/`, which starts and stops it, and later `render` and `game`.
**Depends on:** `heap`, `arena`, `log`, `machine`, and its parts below.

## Parts

One per CUDA area, and each is the C API to it: `run/include/gpu_bridge/<area>/index.h`, wrapped in `extern "C"`, included from `cuda/include/<area>/index.hu`, implemented in `cuda/src/<area>/index.cu`. C never includes a `.hu`.

| Part | Job |
|---|---|
| `status` | `gpu_status_t`, `gpu_error_t`, and reporting a failure. The one part here with C behind it |
| `device_memory` | The device allocator, in `cuda/src/device_memory/` |

## What it costs

CUDA starts on this call and not before, so `--report` and `--test-pattern`, which never make it, pay nothing.

Once made, it holds a context on the card for the rest of the process, on top of the blocks. The desktop shares that card. This is compute only: no display code, and nothing here opens `/dev/dri`.
