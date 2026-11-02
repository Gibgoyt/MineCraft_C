# run/src/gpu_bridge/traverse

**Purpose:** names the C API to ray traversal, in `run/include/gpu_bridge/traverse/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/traverse/index.cu` does, and that README has the rules.
**Tier:** Arena, and it allocates nothing on the host or the card: its caller passes in every block.
**Owns:** nothing here.
**Lifetime:** Process.
**Threads:** the `gpu` thread, once there is one.
**Public functions:** those of that header, listed in `cuda/src/traverse/README.md`.
**May be called by:** `render`.
**Depends on:** `gpu_bridge/status`, `gpu_bridge/device_memory` and `gpu_bridge/world_buffers`, whose types are in its signatures and whose format it walks, and `types`.
