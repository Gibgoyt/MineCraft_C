# cmake/hello/gpu

**Purpose:** proves CUDA links and runs, with a compute kernel built for `sm_75`.
**Tier:** Owner of two small device buffers, for the length of one call.
**Owns:** nothing after it returns.
**Lifetime:** One call.
**Threads:** main.
**Public functions:** `mc_hello_gpu_sum`, adds up an array of `uint32_t` on the GPU; false on any CUDA error.
**May be called by:** nothing since Stage 03.1. It compiles and links, so that `mc_cuda` has a source until Stage 08.1.
**Depends on:** the CUDA runtime. Compute only; uses a few KiB of VRAM and never touches the display.
