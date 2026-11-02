# asm/src/bitmask

**Purpose:** counts the set bits of bit arrays, combines them, and builds them from arrays of bytes and block states.
**Tier:** Borrow in spirit: it uses the pointers and counts it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. Encoding jobs call it from workers.
**Public functions** (`run/include/simd/bitmask/index.h`):

- `bitmask_popcount_batch`: the set bits of a whole array.
- `bitmask_and_batch`, `bitmask_or_batch`, `bitmask_andnot_batch`: word by word.
- `bitmask_from_nonzero_u8_batch`, `bitmask_from_nonzero_u16_batch`: a bit for each element that is not 0.
- `bitmask_from_equal_u16_batch`: a bit for each element equal to a value.

**May be called by:** `world`, `render`, `terrain`.
**Depends on:** `asm/include/conventions/`, `asm/include/bitmask/`.
