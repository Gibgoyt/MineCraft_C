# asm/src/copy

**Purpose:** streaming copy to scanout memory, with non-temporal stores and one fence at the end.
**Tier:** Borrow in spirit: it uses the pointers and counts it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. The display path calls it from `main`.
**Public functions** (`run/include/simd/copy/index.h`):

- `copy_stream_batch`: one span of bytes.
- `copy_stream_rows_batch`: a rectangle, where either side's pitch may be wider than a row. One fence for all of it.

**May be called by:** `display`, and whatever else writes large buffers once and does not read them back. Not for small or
often-read data: a non-temporal store pushes the line out of the cache.
**Depends on:** `asm/include/conventions/`, `asm/include/copy/`.
