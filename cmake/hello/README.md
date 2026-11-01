# cmake/hello

The program `minecraft_c` is until `run/src/main/` exists (Stage 03.1). It proves that C, NASM and CUDA compile with
the real flags and link the way the coding standard requires, by adding up the same numbers on the CPU and the GPU.

It lives here and not in `run/`, `asm/` and `cuda/` because it is part of the build's own proof, not of the game.
`cmake/targets.cmake` drops `main/` as soon as `run/src/main/index.c` exists, and drops `simd/` and `gpu/` after that
as soon as their language has a real module. Nothing in the game may call it.

`gpu/` calls `cudaMalloc`, which the standard otherwise allows only in `cuda/src/device_memory/`. That module does not
exist yet (Stage 08.1). `standards_lint` (Stage 02.4) should leave `cmake/` out of that check.
