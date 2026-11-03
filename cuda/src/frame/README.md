# cuda/src/frame

**Purpose:** one frame's kernels in order on one stream, with the card's clock around each, and the copy of the picture home.
**Tier:** a CUDA area. It holds no memory but its clock events: every block comes from its caller.
**Owns:** `owned_events`, CUDA events, in the `frame_gpu_t` it is given.
**Lifetime:** `frame_gpu_init` to `frame_gpu_deinit`; pins last from `frame_host_pin` to `frame_host_unpin`.
**Threads:** one at a time: the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/frame/index.h`, for C and CUDA):

- `frame_gpu_init`, `frame_gpu_deinit`: make and destroy the clock events. Deinit is safe on a zeroed `frame_gpu_t`.
- `frame_host_pin`, `frame_host_unpin`: pin host memory for the copy home. It stays whoever's it was: `cudaHostRegister` allocates nothing, so `run/src/heap/` stays the only allocator.
- `frame_gpu_run`: coarse pass, primary rays, shading, post, and the copy, and waits for all of it. Reports each kernel's time and each pass's on the card's clock. God rays and the temporal pass count as post, AO as shading, and the far field's rays as primary, as the rest of those same rays.
- `frame_kernel_name`: what a kernel is called in a profile.

**May be called by:** `render`.
**Depends on:** `gpu_bridge/*`, `cuda/src/traverse/`, `cuda/src/shade/`, `cuda/src/post/`, the CUDA runtime.

## Why it is the way it is

The kernels run in one stream's order and upload's copies run in that same stream, between frames: that order is what lets `world_buffers` reuse a slab at once. A second stream would break that.

The clock is `cudaEventRecord` between the kernels and not the host's, because a host clock around each would need a wait around each, and so would change what it measures. An event is a marker in the stream, costing the kernels nothing they would notice, so the timers are always on, and `run/src/profiler/` only reads what they say. A kernel here is one call this file makes: what a call does inside itself, as `shade_frame_entities` does with AO, is one number.

A wait comes once, at the end. A kernel that faults after it started is first reported there, so that is the check that matters: `frame_gpu_run` returns what `cudaDeviceSynchronize` says.
