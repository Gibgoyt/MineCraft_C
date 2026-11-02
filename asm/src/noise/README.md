# asm/src/noise

**Purpose:** 8-wide 3D gradient noise, the batch that terrain generation spends most of its arithmetic on.
**Tier:** Borrow in spirit: it uses the pointers and count it is given, and its own stack frame.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It has no state.
**Public functions** (`run/include/simd/noise/index.h`):

- `noise3_batch8`: gradient noise at each of `count` points, for one seed.

**May be called by:** `terrain`, `run/src/simd/` for its self-check, tests and benchmarks.
**Depends on:** `asm/include/conventions/`, `asm/include/noise/`.

The lattice hash takes no table, so no two threads share anything and no gather is needed. Its result is defined by
`simd_reference_noise3` in `run/src/simd/reference/`, which this must match bit for bit. Changing either changes every
world, which `tests/asm/noise/` pins with known values.
