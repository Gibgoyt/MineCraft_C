# asm/src/blend

**Purpose:** alpha blending of 32-bit pixels, for composing the HUD over a finished frame.
**Tier:** Borrow in spirit: it uses the pointers and counts it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. The HUD path calls it from `main`.
**Public functions** (`run/include/simd/blend/index.h`):

- `blend_over_batch`: straight-alpha source over a destination whose top byte is kept.
- `blend_premul_batch`: premultiplied source over a destination, on all four bytes, so layers can be stacked.

**May be called by:** `ui`, `render`.
**Depends on:** `asm/include/conventions/`, `asm/include/blend/`.
