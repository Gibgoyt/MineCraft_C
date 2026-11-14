# run/src/terrain/feature/tree

**Purpose:** the shapes of the eight kinds of tree: how far each reaches, which are wide, the trunk, the leaves, the branches, the vines up the trunk. Moved here from `run/src/terrain/feature/index.c` in plan 010 phase 01, with no byte of the land changed, because that file was at 1473 lines and the roots would have pushed it past the 1500-line split (`plans/CODING_STANDARD.md` section 5).
**Tier:** Borrow: it writes into a canvas it is lent, and allocates nothing.
**Owns:** nothing.
**Lifetime:** nothing is kept here.
**Threads:** any. It touches only the canvas it is given.
**Public functions** (declared with the rest of `terrain/feature`, in `run/include/terrain/feature/index.h`): `terrain_feature_tree_reach`, `terrain_feature_tree_is_wide`, `terrain_feature_paint_tree`.
**May be called by:** `terrain/feature`, which finds the sites, and tests (`tests/run/terrain/feature/`, `tests/run/player/felling/`).
**Depends on:** `terrain/feature`'s header for the canvas, the site and `terrain_feature_put` (which stays in `terrain/feature`, as the rest of the stage puts with it too), `terrain/rng`, `assert`, and the generated `tables/blocks/`.

Each voxel goes on the canvas through `terrain_feature_put`, so what is drawn at a voxel depends only on the site and on what is at that voxel: the rules are `run/src/terrain/feature/README.md`'s "How a voxel stays a pure function" and its tree table.
