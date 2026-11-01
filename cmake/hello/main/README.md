# cmake/hello/main

**Purpose:** entry point of the hello program; runs the NASM and CUDA parts and checks their answers.
**Tier:** Root. Allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** main.
**Public functions:** `main`.
**May be called by:** the OS.
**Depends on:** `cmake/hello/simd`, `cmake/hello/gpu`.
