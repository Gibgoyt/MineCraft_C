# run/src/gpu_bridge/shade

**Purpose:** names the C API to shading, in `run/include/gpu_bridge/shade/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/shade/index.cu` does, and that README has the rules.
**Tier:** Arena, and it allocates nothing on the host or the card: its caller passes in every block.
**Owns:** nothing here.
**Lifetime:** Process.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions:** those of that header, listed in `cuda/src/shade/README.md`.
**May be called by:** `render`, and `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/post`, `gpu_bridge/status` and `gpu_bridge/traverse`, whose types are in its signatures, and `types`.
