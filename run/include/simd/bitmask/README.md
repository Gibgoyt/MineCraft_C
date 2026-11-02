# run/include/simd/bitmask

**Purpose:** C prototypes of the NASM functions in `asm/src/bitmask/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `bitmask_popcount_batch`, `bitmask_and_batch`, `bitmask_or_batch`, `bitmask_andnot_batch`,
`bitmask_from_nonzero_u8_batch`, `bitmask_from_nonzero_u16_batch`, `bitmask_from_equal_u16_batch`.
**May be called by:** `world`, `render`, `terrain`, `run/src/simd/`, tests and benchmarks.
**Depends on:** `asm/src/bitmask/`.
