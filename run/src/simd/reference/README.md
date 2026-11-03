# run/src/simd/reference

**Purpose:** scalar C versions of every kernel in `asm/src/copy/`, `asm/src/blend/`, `asm/src/bitmask/`, `asm/src/noise/`
and `asm/src/vec_batch/`: the readable definition of what each must do, and what each must beat.
**Tier:** Arena by depth, and it allocates nothing: it uses what the caller gives it.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions** (`run/include/simd/reference/index.h`), one per kernel, with its arguments: `simd_reference_copy_stream`, `simd_reference_copy_stream_rows`, `simd_reference_blend_over`, `simd_reference_blend_premul`, `simd_reference_bitmask_popcount`, `simd_reference_bitmask_and`, `simd_reference_bitmask_or`, `simd_reference_bitmask_andnot`, `simd_reference_bitmask_from_nonzero_u8`, `simd_reference_bitmask_from_nonzero_u16`, `simd_reference_bitmask_from_equal_u16`, `simd_reference_noise3`, `simd_reference_vec_batch_transform_points`, `simd_reference_vec_batch_transform_directions`, `simd_reference_vec_batch_mul_vec4`.
**May be called by:** `run/src/simd/` for the self-check at startup, `tests/asm/`, `bench/simd/`. Nothing else: the game
calls the kernels.
**Depends on:** glibc's `<stdbit.h>` and `<math.h>`, `math/mat4`, `math/vec3`, `math/vec4`.
