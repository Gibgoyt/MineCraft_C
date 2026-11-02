# run/src/math

**Purpose:** our vector library: the Owner of `vec2 vec3 vec4 ivec3 mat4 quat aabb ray`, and the check that the floating-point
environment suits a deterministic game.
**Tier:** Owner.
**Owns:** nothing. The tier allows regions and arenas, but every part is plain arithmetic on values the caller holds, so there
is nothing for one to hold. `plans/CODING_STANDARD.md` section 4: any tier may allocate less than it is allowed.
**Lifetime:** Process.
**Threads:** `math_init` and `math_deinit` on main, before the first thread starts and after the last one stops. The other
functions touch no shared state, so any thread may call them.
**Public functions:**

- `math_init`, `math_deinit`: a pair. Init fails if rounding is not to nearest or if denormals are flushed.
- `math_radians`, `math_clamp`, `math_lerp`: the scalar helpers the parts and their callers share.

**May be called by:** `main` for the pair; anything from this layer up for the rest.
**Depends on:** `assert`, `log`, `types`, glibc's `fenv.h`.

The whole library is scalar C, and every definition lives in an `index.c`, not in a header. In release and profile `-flto`
inlines them. `plans/CODING_STANDARD.md` section 7 says why there is no NASM here: a call to assembly for one `vec3_add` costs
more than the add. Batch work belongs to `asm/src/vec_batch/`.

Conventions shared by every part: axes are x east, y north, z up, right-handed. Angles are radians. Functions take and return
structs of 32 bytes or less by value, and `mat4_t`, at 64 bytes, by pointer. Where the math has no answer, the header says what
comes back, and it is never a NaN made here: a zero vector normalizes to zero, for example. `-std=c23` keeps GCC from fusing
`a * b + c` into an FMA, which would change results; `run_math_vec3` checks that.
