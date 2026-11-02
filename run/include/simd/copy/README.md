# run/include/simd/copy

**Purpose:** C prototypes of the NASM functions in `asm/src/copy/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `copy_stream_batch`, `copy_stream_rows_batch`.
**May be called by:** `display`, `run/src/simd/`, tests and benchmarks.
**Depends on:** `asm/src/copy/`.
