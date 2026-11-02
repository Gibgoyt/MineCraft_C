# compile/src/texture_vector/palette

**Purpose:** the palette of one texture, and each pixel's index into it.
**Tier:** Arena (depth 2). Its scratch memory comes from the `tp_arena_t` it is given, and goes back before it returns.
**Owns:** nothing.
**Lifetime:** the result lives in the caller's `tv_palette_t` and index buffer.
**Threads:** any single thread. There is no shared state.
**Public functions** (`compile/include/texture_vector/palette/index.h`):

- `tv_palette_build`: fills a `tv_palette_t` and one index byte per pixel of every frame.

**May be called by:** `compile/src/texture_vector/analyze/`, and its test.
**Depends on:** glibc's `qsort` and `bsearch`, `compile/src/texture_pack/support/`.

## How

A pixel with alpha 0 counts as `#00000000`, since its colour can never show. With 256 distinct colours or fewer the palette is those colours, and `exact` is 1: palette and indices rebuild every visible pixel exactly. With more, a median cut: repeatedly split the box with the widest channel, at the point where half its pixels lie on each side, and use each box's weighted mean. Pixels take the nearest entry, by squared distance across all four channels.

Entries run from most used to least, ties by colour value. Every comparison is a total order, so `qsort`, which is not stable, cannot change the result.
