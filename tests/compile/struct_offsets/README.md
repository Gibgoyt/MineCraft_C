# tests/compile/struct_offsets

**Purpose:** ctest `compile_struct_offsets`: proves `struct_offsets` lays structs out as the compiler does, that NASM and gcc
accept what it writes, and that it refuses what it cannot lay out, on the right line and writing nothing.
**Tier:** none; tests are not tiered.
**Owns:** nothing. It writes only under `build/<preset>/test_work/struct_offsets/`.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_struct_offsets`, on `tests/harness/`, with the paths from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the `struct_offsets` tool, the generated `struct_offsets/` output, and `gcc` and `nasm`.
It starts no server and touches no display.
