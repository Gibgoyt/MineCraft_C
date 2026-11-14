# tests/run/terrain/feature/roots

**Purpose:** ctest `run_terrain_feature_roots`. Plan 010 phase 01's roots: every generated tree's trunk columns go `TERRAIN_FEATURE_ROOT_DEPTH` (2) logs into the ground, and nothing else in the land moves. In its own directory as `tests/run/terrain/feature/index.c` is at 1630 lines.
**Tier:** test.
**Owns:** the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/terrain/` (`terrain_feature_paint_tree`, `terrain_generate_box_fallback`, `terrain_feature_chunk_columns`), `run/src/world/column/`'s layers, and the generated block table.

Trees are painted on a canvas of ground, as `terrain_feature_fill_box` would find it:

- `every_trunk_column_roots_two_blocks_into_the_ground`: every kind, 64 seeds, on flat dirt. The trunk, each of a wide trunk's 4 columns and each mangrove prop root drawn has a log at the ground's top and one under it, dirt under those, and no other log is in the ground (no flare).
- `the_top_root_is_level_with_the_ground`: on grass, the top root is where the grass was, level with the grass round it, and the log over it is the first in the air.
- `a_wide_trunk_roots_from_each_columns_own_foot`: wide jungle and dark oak trees with their other 3 columns' ground 2 under to 2 over the site's; each column roots from its own foot.
- `mangrove_prop_roots_root_too`: 64 mangroves in 1 to 5 layers of water on mud, the sides' ground a layer lower here and there. Each prop root drawn goes 2 into its own ground, and a side not drawn keeps its mud.
- `roots_never_hang_or_eat_ores`: under the top, air, water, lava, bedrock, a log, leaves, a plant, every block named `_ore` (16 today; one added later is held to it by its name) and every unbreakable block stay, and the root is the 1 log at the top.
- `roots_split_stone`: 1 grass over stone, deepslate, granite, tuff or sandstone: 2 roots, the lower in the rock's place.
- `ordinary_logs_still_never_go_into_the_ground`: every thin tree against a hill 12 high on each of its 4 sides: no hill block over the ground is replaced, whatever leans or branches into it, and the trunk still roots.
- `nothing_but_trees_changes`: 3 worlds (the default, Ahmed's 11107447384583428913 and 11), 16 whole chunks each, generated. Every voxel at a column's top (as `terrain_feature_chunk_columns` gives it, before anything grows) or under it that is a log or ground is read as one mark, so a root and the ground it took read the same. The digest of that is pinned to its value measured before roots were drawn (plan 010 phase 01 step 1), so a moved draw, a moved site or a changed voxel anywhere else fails it. It also counts the root logs it masked (1355), so that it cannot pass on land with none.

What it does not say: the box against the chunk and 1 against 16 workers are `run_terrain_feature_own`'s and `run_terrain_feature`'s, which hold the land with roots to the same bytes.
