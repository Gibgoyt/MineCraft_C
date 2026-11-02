# run/src/gpu_bridge/entity_rays

**Purpose:** names the C API to entity boxes and the outline, in `run/include/gpu_bridge/entity_rays/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/entity_rays/index.cu` does, and that README has the rules.
**Tier:** Arena, and it allocates nothing on the host or the card: its caller passes in every block.
**Owns:** nothing here.
**Lifetime:** Process.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions:** those of that header, listed in `cuda/src/entity_rays/README.md`.
**May be called by:** `render`, `cuda/src/frame/` and `cuda/src/shade/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status` and `gpu_bridge/traverse`, whose types are in its signatures, and `types`.
