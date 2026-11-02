# cmake/hello

What is left of the program that was `minecraft_c` until `run/src/main/` arrived in Stage 03.1. Its `main/` and `simd/`
parts went then, since `run/src/main/` and `asm/src/cpuid/` took their place.

`gpu/` stays because an object library must have a source, and CUDA has no module until Stage 08.1.
`cmake/targets.cmake` drops it as soon as `cuda/src/` holds one, and this directory should be deleted then. Nothing in
the game calls it.

`gpu/` calls `cudaMalloc`, which the standard otherwise allows only in `cuda/src/device_memory/`, and `standards_lint`
allows this one file for that reason.
