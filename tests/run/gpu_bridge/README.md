# tests/run/gpu_bridge

**Purpose:** ctest `run_gpu_bridge`. Tests `run/src/gpu_bridge/` with `status`, and `cuda/src/device_memory/`, on the real card: real allocations, a real kernel, and the real errors a card gives on request.
**Tier:** test.
**Owns:** nothing outside the test arena, and blocks on the card for the length of a test, never more than 16 MiB.
**Lifetime:** One run.
**Threads:** main, and a short-lived thread it joins, in the place of `gpu`.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/gpu_bridge/`, `cuda/src/device_memory/`, the built `minecraft_c`, and a card. No card fails this test, on purpose: `gpu.enabled` is true, and a skip would read as a pass.

Compute only. It opens no display, and the only program it runs goes under `--headless`.

What it can make the runtime do for real:

- `CUDA_VISIBLE_DEVICES=` in a fresh process, which is the runtime with no device: this program runs itself again for that, and for what a leak logs. A context cannot be undone in a process that has one.
- `cudaErrorMemoryAllocation`, by asking for a pebibyte, which the card refuses without taking anything.

What it will not make it do: fault a kernel on a card the desktop shares. So the table from those errors to `GPU_STATUS_DEVICE_LOST` is read as a table, against the numbers in `driver_types.h`.
