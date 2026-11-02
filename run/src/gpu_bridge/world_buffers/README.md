# run/src/gpu_bridge/world_buffers

**Purpose:** names the C API to the section pool and the column table, in `run/include/gpu_bridge/world_buffers/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/world_buffers/index.cu` does, and that README has the rules. It exists so that the header may include `gpu_bridge/device_memory` and `gpu_bridge/status`, as with `device_memory` (Stage 08.1 issue 3).
**Tier:** Arena, and it allocates nothing on the host: its owner passes in what it uses.
**Owns:** nothing here.
**Lifetime:** World.
**Threads:** one at a time: `gpu`.
**Public functions:** those of that header, listed in `cuda/src/world_buffers/README.md`.
**May be called by:** `render` and its parts.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, whose types are in its signatures.
