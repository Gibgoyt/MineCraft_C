# run/include/types

**Purpose:** the only home of `float32_t`, `float64_t`, `libc_double_t` and the `F32()`/`F64()` literal macros.
**Tier:** none: a header with no source, and so no memory to manage.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** none. Types and macros, with the rules in `plans/CODING_STANDARD.md` section 2:

- `float32_t`, `float64_t`: `_Float32` and `_Float64` in C; `float` and `double` under `__CUDACC__`.
- `F32(1.0)`, `F64(1.0)`: literals for headers compiled on both sides. A C-only file writes `1.0f32`, a `.cu` file `1.0F`.
- `libc_double_t`: only for the cast at a varargs call, as in `printf("%.2f", (libc_double_t)value)`.

**May be called by:** everything, including `compile/` and `cuda/`.
**Depends on:** `<stdint.h>`.
