# run/src/simd/reference

**Purpose:** scalar C versions of every kernel in `asm/src/copy/`, `asm/src/blend/`, `asm/src/bitmask/`, `asm/src/noise/`
and `asm/src/vec_batch/`: the readable definition of what each must do, and what each must beat.
**Tier:** Arena by depth, and it allocates nothing: it uses what the caller gives it.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions** (`run/include/simd/reference/index.h`): `simd_reference_<kernel>`, one per kernel, with its arguments.
**May be called by:** `run/src/simd/` for the self-check at startup, `tests/asm/`, `bench/simd/`. Nothing else: the game
calls the kernels.
**Depends on:** glibc's `<stdbit.h>` and `<math.h>`, `math/mat4`, `math/vec3`, `math/vec4`.
