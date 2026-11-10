# tests/run/render/lod

**Purpose:** ctest `run_render_lod`. Tests what a far-field column is made from in `run/src/render/lod/`, on the host: that a sample with the chunks of what grows kept from one line to the next is the sample without, at every level that grows anything and on two seeds, and that `terrain_feature_fill_box_cached` gives `terrain_feature_fill_box`'s bytes for any small boxes.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena, and two terrains, each released at its test's end.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/render/lod/`, `run/src/terrain/`, `run/src/time/`, `run/src/world/column/` and the generated `tables/blocks/`. No card: nothing here starts CUDA. What the card does with a column is `tests/cuda/lod/`'s.

What it holds to:

- **A column's lines:** at six places on Ahmed's seed and on the default one, at voxels of 2, 4 and 8, 32 lines a column in a job's order: all 1536 blocks of each the same as with no cache, which is what the far field did before plan 005 phase 14.
- **The cache:** 400 small boxes, mostly near each other, through both, byte for byte, with plants and leaves among them, and fewer chunks loaded than boxes asked for them.
- **The cost**, printed and not held to: a row of a column both ways. `bench/lod/` has the release numbers.
