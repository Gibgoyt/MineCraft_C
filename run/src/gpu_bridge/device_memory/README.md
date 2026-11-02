# run/src/gpu_bridge/device_memory

**Purpose:** names the C API to the device allocator, in `run/include/gpu_bridge/device_memory/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/device_memory/index.cu` does, and that README has the rules.
**Tier:** Arena, and it allocates nothing on the host: the Owner passes in the records.
**Owns:** nothing here.
**Lifetime:** Process.
**Threads:** one at a time: main, then `gpu`.
**Public functions:** those of that header, listed in `cuda/src/device_memory/README.md`.
**May be called by:** `gpu_bridge`, and later `render`, for blocks it owns.
**Depends on:** `gpu_bridge/status`, whose types are in its signatures.
