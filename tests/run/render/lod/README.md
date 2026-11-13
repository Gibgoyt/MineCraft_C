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
- **The pyramid's bands and plan** (plan 009 phase 03), on paper: at E = 2, 70° and 1080 rows level 1 starts at 385.6 blocks and level 2 at 771.2, E = 8 and 16 give 96.4 and 48.2, E = 64 is held to the 16-block floor; a field of view of 30° moves level 1 out by tan 35° / tan 15°, half the rows bring it in by half; at render distance 64 the plan has levels 1 and 2 (27 and 18 columns each way), at 128 levels 1 to 3, at 32 level 1 alone, and the cap holds.
- **The turn cache's addressing** (plan 008 phase 06), on paper: a million random unit directions at the game's side (1024) and at 7 each fall in a texel of the cube, whose middle is within one texel's angle (2 / side radians) of them; the six axes go to the middle texel of their own face, and stay on it a hair off.
- **Assembled is direct**: 48 random cubes of 32 x 32 x 32 blocks (slopes with water, torches and flowers, all air, buried stone given as one value), each a level 1 section made two ways, through `render/octant`'s store from its 8 sections' octants and straight from the blocks: the same encoded bytes and the same top.
