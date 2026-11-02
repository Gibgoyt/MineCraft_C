# run/src/gpu_bridge/frame

**Purpose:** names the C API to one frame's kernels, in `run/include/gpu_bridge/frame/index.h`. No C implements it, so this directory holds no `index.c`: `cuda/src/frame/index.cu` does, and that README has the rules.
**Tier:** Arena, and it allocates nothing on the host or the card: its caller passes in every block.
**Owns:** nothing here.
**Lifetime:** Process.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions:** those of that header, listed in `cuda/src/frame/README.md`.
**May be called by:** `render`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, `gpu_bridge/shade` and `gpu_bridge/post`, whose types are in its signatures, and `types`.
