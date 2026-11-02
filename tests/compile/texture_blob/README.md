# tests/compile/texture_blob

**Purpose:** ctest `compile_texture_blob`: proves the mip rules by small images with hand-worked answers, and that `compile/src/texture_blob/` lays a blob out as the format says and refuses bad input.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_texture_blob`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/texture_blob/` (run as a process, and `mip` linked directly), `compile/src/texture_vector/` (run as a process), `compile/src/texture_pack/` parts `support` and `mctex`, and `rm` from the system. It needs no server, no display, no network, and no pack.

The committed `textures/` and the real blob are checked by ctest `run_texture`, in `tests/run/texture/`.
