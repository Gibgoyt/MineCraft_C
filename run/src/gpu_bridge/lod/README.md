# run/src/gpu_bridge/lod

**Purpose:** the C API of the far field's rays. This directory holds only this README: `cuda/src/lod/` implements `run/include/gpu_bridge/lod/index.h`.
**Tier:** Arena by depth, and a boundary between C and CUDA. Nothing here holds memory.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** the `gpu` thread.
**Public functions:** `lod_frame`, `lod_rays`, `lod_hits_bytes`.
**May be called by:** `run/src/render/frame/` through `cuda/src/frame/`, `run/src/render/lod/` for what the scene holds, `gpu_bridge/frame` and `gpu_bridge/shade` for its types, and tests.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, `gpu_bridge/world_buffers`.
