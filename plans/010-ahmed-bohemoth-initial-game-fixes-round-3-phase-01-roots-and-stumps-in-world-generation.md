# 010-ahmed-bohemoth-initial-game-fixes-round-3 — phase 1: Roots and stumps in world generation

**Status:** in progress: the tree code moved to `feature/tree/`, every terrain digest held   (updated 2026-11-14)

Read first: `plans/010-ahmed-bohemoth-initial-game-fixes-round-3-phase-00-vision-and-plan-files.md`, then the master `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md` (sections 0, 2.1, 2.2, 2.4, 3 to 7 and 9). Every number here comes from the master. Where this file adds one, it says so and why.

## Goal

Every generated tree's trunk goes **2 blocks into the ground** (`TERRAIN_FEATURE_ROOT_DEPTH`):
- the first root log is level with the ground's top, where the grass or dirt the tree stood on was;
- the second is under it;
- this holds for a thin trunk, each of a wide trunk's four columns from its own foot, and each mangrove prop root.

A root replaces only solid ground (stone included), never air, water, plants, leaves, logs, ores, bedrock or an unbreakable block, so no root hangs in a cave and no ore is eaten. Nothing else in the land changes: tree shapes above the ground, sites, counts, reach and every other stage keep their bytes.

The tree code lives in `run/src/terrain/feature/tree/`, moved there first with no byte changed. `TERRAIN_GENERATOR_VERSION` is 3:
- `FEA_DIGEST_OF_GENERATOR` has row 3;
- each terrain pin that moved is re-pinned with "plan 010 phase 01: trees root two blocks into the ground";
- `run_save_world` refuses generators 1 and 2.

`run_playthrough`'s finder aims at the trunk's first log over the ground, and the day, bench, stairs and felling scripts still fell their tree with the **old** feller. `bench_terrain` stays at or above 1000 columns a second at its slowest place, and `bench_lod` at or under 40 ms a column. `saves/` is listed with sizes and generators in this file, and Ahmed has been asked about Q1.

## Where this fits

**The vision.** Ahmed: "Trees must also have an underground root and not just start on top of dirt", "a little of that stump goes underground". This is the world generation half: the stump exists in the land before anyone cuts. Phase 02 makes felling leave it.

**What later phases read from it:**

| Reader | What it reads |
|---|---|
| Phase 02 | A **root log** (master 2.1) exists in every generated tree, so "the tree" can require one, and placed logs on the ground never have one |
| Phase 02's playthrough | The finder's cut point, the first log over the ground, and the flush stump under it |
| Phase 03 | The version 3 pins, and the `saves/` list with Ahmed's answer |

**What this phase must NOT build:**
- No change to felling (`run/src/player/felling/`): one cut still fells the tree above it. That is phase 02.
- No change to tree shapes above the ground, tree reach, site margins, rates, or how many trees stand.
- No root flare and no stump block (master Q3, Q4).
- No change to any other stage: height, density, hydrology, biomes, surface, caves, ores, lakes, plants, ice and snow.
- No deletion in `saves/` (Q1: phase 03, on Ahmed's word only).
- No regrowth, sap or any new item (master section 8).

## Ahmed's words

- "Trees must also have an underground root and not just start on top of dirt ... Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."
- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"

`11.png`: the bottom log layer left level with the grass, ringed tops showing, after the fall.

---

## Background, explained

### Where a tree stands today

Read from the code, 2026-11-14:

1. **The site.** `feature_row_chunk` (`run/src/terrain/feature/index.c:1062-1116`) makes `site.z = top + 1`, where `top` is the column's highest block that is not air or water. `feature_tree_stands` (`:1016`) needs that top to be soil, or snow for a spruce. A wide trunk also needs its 3 other columns dry and within 2 layers (their tops go in `foot[0..2]`). A mangrove fills `foot[0..3]` with the 4 side columns' tops.
2. **The trunk.**
   - `feature_logs(canvas, x, y, site->z, top - 1, log)` writes logs with `FEATURE_PUT_LOG`, which "goes over all but solid ground and other logs" (`:116-150`).
   - `feature_wide_trunk` (`:290`) writes each of the other 3 columns from `foot[i] + 1`.
   - `feature_tree_mangrove` (`:354`) writes each prop root from `foot[side] + 1` up to the water's top plus 1, each side skipped 1 time in 4.
3. **Below the ground.** The feature stage may write as far as `FEATURE_BELOW` (`TERRAIN_FEATURE_LAKE_DEPTH_MAX + 4`) under a column's top (`:40`); a lake's bed and an ice spike's foot use it. A root at `top` and `top - 1` is inside that, so no box bound or skip test changes (`feature_row_chunk`: `top - FEATURE_BELOW > canvas->z_high`).
4. **Purity.** "What a step writes at a voxel depends only on what is at that voxel, and on the chunks. No step reads what any other voxel has become" (`run/src/terrain/feature/README.md`). A site's ground comes from the walk's columns, never from the box. So a root can be decided per voxel ("replace if this voxel is ground of the right kind"), and nothing else needs to look.
5. **The file is at 1473 lines** (split at 1500, hard limit 2000, `plans/CODING_STANDARD.md` section 5). The tree code (`:169-441`, about 270 lines: reach, wideness, logs, the leaf blob and layer, the 8 kinds, vines, `terrain_feature_paint_tree`) is one clear part, and moves to `run/src/terrain/feature/tree/`.
   - That is depth 3, the Borrow tier: no allocation. The tree code allocates nothing; it writes into a canvas it is lent.
   - `FEATURE_CLASSES`, `feature_put` and `terrain_feature_canvas_of` are used by the rest of the stage too. They stay in `feature/` and are exposed to `tree/` through `run/include/terrain/feature/` (or move to `tree/` and are called back from `feature/`). The step decides and records which.

### The root, worked through

A thin oak on flat grass at top z = 70 (grass), dirt at 69 and 68, stone at 67:

| z | Before | After |
|---|---|---|
| 71 to 74 | oak log (trunk 4) | oak log |
| **70** | grass block | **oak log**: its ringed top is level with the grass round it |
| **69** | dirt | **oak log** |
| 68 | dirt | dirt |

A wide jungle tree on ground that falls away: the site column top 80, and `foot` = 79, 80, 78. Its columns get roots at (80, 79), (79, 78), (80, 79) and (78, 77). Each column's root starts at its own ground.

**When the ground is thin or open:**
- On 1 block of soil over stone, the root at `top - 1` replaces the stone (roots split rock).
- Over a cave, the root at `top - 1` is not written if that voxel is air; the root is then 1 log.
- On an ore at `top - 1`, the ore stays and the root is 1 log.

### Why ordinary logs (master 2.2)

| Logs | A new stump or root block |
|---|---|
| `11.png`'s ringed tops are what a log's top picture already is | 8 new blocks (one per wood), 8 sets of pictures |
| Digging one out gives the wood it is | New drops, or 8 new items |
| `save/names` already lists them | 8 new names in every manifest |
| Felling, the finder, leaf decay already know logs | Each must learn a new flag |
| A root is known by **where** it is (master 2.1's root log) | |

Q3 (a look of its own) stays open, default no.

### The version (master section 7)

Every tree's column changes, so the land changes, and a saved section beside an unsaved one would meet a seam: a felled tree in a saved section over an unsaved one that now has roots. The repo's rule is a version and no migration (`run/src/save/world/README.md`, "`manifest.generator`"), as plan 006 phase 05 did for 1 → 2.

`saves/` on 2026-11-14:
- `test_1` to `test_8` and `test_8-2` are generator 1, already refused, waiting on Ahmed's word to delete (plan 006 phase 05 Issues found 3).
- **`test_8-3` is generator 2** (58 MB, last written 2026-11-14 10:58) and would be refused by version 3.

So this phase **asks first** and deletes nothing.

---

## Design

### `run/src/terrain/feature/tree/` (step 1)

`terrain_feature_paint_tree`, `terrain_feature_tree_reach` and `terrain_feature_tree_is_wide` keep their names and their header (`run/include/terrain/feature/index.h`), so callers (`tests/run/player/felling/`, `tests/run/terrain/feature/`) do not change. The move is checked by every terrain digest holding, bit for bit, before roots are added. `tree/README.md` says what moved and why (the 1500-line split).

### `FEATURE_PUT_ROOT` and the ground class (step 2)

- `FEATURE_CLASSES` gains `FEATURE_CLASS_GROUND` for solid blocks a root may take. That is every block that is solid, not a log, not leaves, not an ore, not bedrock, not unbreakable. The table is made once from `block_defs` flags (`BLOCK_FLAG_SOLID`, `BLOCK_FLAG_LOG`, `BLOCK_FLAG_DECAYS`, `BLOCK_FLAG_UNBREAKABLE`, and the ore table's blocks), or listed by hand as today's table is. The step decides and records which.
- `FEATURE_PUT_LOG` keeps its meaning: it takes air, water, plants, vines and leaves, and **not** ground. Today it is written as `there != FEATURE_CLASS_SOLID`, so the step must make sure the new class does not let ordinary logs into the ground.
- `FEATURE_PUT_ROOT` takes `FEATURE_CLASS_GROUND` only.

### Roots (step 3)

```c
enum : int32_t {
    /* Plan 010 master 2.2: how far a tree's trunk columns go into the ground, the first level with the ground's top. */
    TERRAIN_FEATURE_ROOT_DEPTH = 2,
};
```

A helper `feature_roots(canvas, x, y, ground_top, log)` writes `FEATURE_PUT_ROOT` at `ground_top` down to `ground_top - TERRAIN_FEATURE_ROOT_DEPTH + 1`. It is called:
- for the trunk with `site->z - 1`;
- in `feature_wide_trunk` with each `foot[i]`;
- in `feature_tree_mangrove` for each prop root it draws, with `foot[side]`, after the same 1-in-4 draw, so no draw moves.

**No new random draws**, so every other tree, plant and vine lands where it did. The README's tree table gains a column "Roots", and its "What may go over what" table a row: "A root | Solid ground, stone included; never an ore, bedrock or an unbreakable block".

### The finder (step 6)

`pt_box_tree` (`tests/run/playthrough/index.c:311`) takes `root` as a tree's lowest log, rejects a tree whose root has a log under it, and sets `ground_z = root[2] - 1`. With roots, the lowest log is the deep root. The finder now takes:
- **the cut point**: the lowest log of the tree that has air (or a plant) beside it on at least one side and ground under it, or the root log under it. On flat ground that is the first log over the flush root;
- `ground_z` = the cut point's z - 1, which is the flush root's z, the height the feet stand on beside it;
- the check "no log under the root" becomes "the two logs under the cut point are this tree's roots" (or one, where the ground stopped it).

`pt_flood`, `pt_compact` and `pt_clear_round` keep working on the logs. `pt_clear_round` counts the flush root as part of the trunk, not as something on the clear ground. The scripts' `@tree` / `@fell` / `@feet` lines need no change in this phase: one cut at the cut point still fells everything above with the old feller. The stump left is the two roots, flush, so the day's walk in over the logs is not blocked.

### The felling tests' canvas (step 7)

`pf_paint` (`tests/run/player/felling/index.c:209`) paints on air. Roots go only over ground, so painted trees would have none. Phase 02 needs them, and the limit test should count them. So `pf_paint` first fills every voxel under `PF_Z - foot_gap` (the ground it says the tree stands on) with dirt, and `pf_place_tree` puts that ground into the world. Then:
- `the_limit_is_above_the_biggest_tree_of_every_kind_measured` counts roots, and must still hold `2 × biggest < PLAYER_FELLING_LOGS_MAX`;
- the old feller's tests cut at `PF_Z`, over the roots, so the roots stay and the tests that count "logs at or above `PF_Z`" hold unchanged.

## Owned paths

This phase may edit these, plus this file:
- `run/src/terrain/feature/` (`index.c`, `README.md`), new `run/src/terrain/feature/tree/` (`index.c`, `README.md`), `run/include/terrain/feature/index.h`
- `run/include/terrain/index.h` (`TERRAIN_GENERATOR_VERSION`)
- `tests/run/terrain/feature/` (`index.c`: `FEA_DIGEST_OF_GENERATOR`, `FEA_FOREST_PIN`), new `tests/run/terrain/feature/roots/`
- `tests/run/terrain/biome/` (`BIO_COLUMNS_PIN` if it digests features), and any other terrain or render pin that moves, each re-pinned with its reason
- `tests/run/save/world/` (the generator checks at `index.c:510`, `:948-950`), `tests/fuzz/save/` if it names the version
- `tests/run/playthrough/` (the finder), `tests/run/player/felling/` (the canvas)
- goldens that show a tree on a slope (made again with `MC_GOLDEN_WRITE=1` and looked at)
- `plans/REPO_STRUCTURE.md` (the new `tree/` directory)

**Not touched:** `run/src/player/felling/`, `run/src/world/tick/`, `config/`.

## Steps

1. **Move the tree code to `feature/tree/`.** Build, run `run_terrain_.*`: every digest holds. Commit alone. *Why: the file is at 1473 lines and roots would push it past the 1500 split; a move with no change is easy to review and to prove.*
2. **The ground class and `FEATURE_PUT_ROOT`.** *Why: no log may go into the ground today, and ordinary logs must keep that rule.*
3. **Roots** under the trunk, each wide column and each prop root, with no new draw. *Why: Ahmed's "underground root"; no draw keeps every other feature where it was.*
4. **`tests/run/terrain/feature/roots/`** (ctest `run_terrain_feature_roots`), with the tests below. *Why: the feature test file is at 1630 lines.*
5. **Version 3.** `TERRAIN_GENERATOR_VERSION = 3u`. `FEA_DIGEST_OF_GENERATOR` gains row 3, with a comment naming this phase; rows 1 and 2 stay. Every other moved pin is re-pinned with its reason. Height, density, hydrology, biome, cave and ore pins must **not** move; a move is an Issue found. In `run_save_world`, `static_assert(TERRAIN_GENERATOR_VERSION == 3u)` and generators 0, 1, 2 and 4 refused. *Why: the land changed (master section 7).*
6. **The playthrough's finder** aims at the first log over the ground. `run_playthrough` passes with the old feller; the tree it finds and its distance are recorded here. *Why: the lowest log is now underground, and the body would be stood in the dirt.*
7. **The felling tests paint on ground.** `run_player_felling` passes with the old feller; the biggest tree with roots is recorded. *Why: phase 02 needs painted trees to have roots.*
8. **Benches and pictures.**
   - `bench_terrain` and `bench_lod`, before (on this phase's base) and after, recorded.
   - Goldens that moved are made again with `MC_GOLDEN_WRITE=1`, looked at, and described here.
   - `bench_biome_map` is not needed; biomes do not move.
9. **List `saves/`** with `du -sb`, `du -sh`, the newest file's date and each manifest's generator (offset 52 of `blocks/manifest`), into Issues found. Ask the main thread to put Q1 in front of Ahmed. *Why: version 3 refuses `test_8-3`, a world he plays.*
10. **READMEs:** `run/src/terrain/feature/README.md` (roots in the trees table, the put table, the order), `tree/README.md`, `tests/run/terrain/feature/roots/README.md`, `tests/run/playthrough/README.md` (the cut point), `plans/REPO_STRUCTURE.md`. Tick each todo as it lands.

## Todo

From the master (section 9, phase 01):

- [x] Move the tree shapes from `run/src/terrain/feature/index.c` to `run/src/terrain/feature/tree/`, no byte of output changed (every digest holds)
- [ ] `TERRAIN_FEATURE_ROOT_DEPTH` 2, `FEATURE_PUT_ROOT` and the ground class; roots under the trunk, each wide column and each mangrove prop root
- [ ] `tests/run/terrain/feature/roots/`: the root tests of section 5
- [ ] `TERRAIN_GENERATOR_VERSION` 3, `FEA_DIGEST_OF_GENERATOR[3]`, every terrain pin that moved re-pinned with its reason; `run_save_world` refuses 2
- [ ] `run_playthrough`'s finder aims at the first log over the ground; the scripts still fell their tree
- [ ] `tests/run/player/felling/` paints its trees on solid ground; the limit test measures with roots
- [ ] Benches: `bench_terrain`, `bench_lod`, before and after
- [ ] READMEs: `run/src/terrain/feature/`, its new `tree/`, `plans/REPO_STRUCTURE.md`
- [ ] List `saves/` with sizes and generators in the phase file, and ask Ahmed (Q1)

Expanded:

- [ ] `FEATURE_PUT_LOG` still never writes over ground (a test: a trunk painted into a hillside leaves the hill's blocks)
- [ ] No new random draw: tree, plant and vine sites and counts unchanged (`FEA_FOREST_PIN`'s tree count and every non-log voxel above the ground the same as before; a test compares the two lands with logs under the ground masked out)
- [ ] Height, density, hydrology, biome, cave and ore digests unchanged
- [ ] `run_terrain_feature_own` (box against chunk) and 1 against 16 workers: same bytes
- [ ] Goldens that moved made again and described here
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `every_trunk_column_roots_two_blocks_into_the_ground` | Every kind, 64 seeds, painted on dirt | Each trunk column (1, 4, or trunk plus prop roots) has a log at its ground top and at top - 1 | A column is missed, or the depth is wrong |
| `the_top_root_is_level_with_the_ground` | Same | The top root's z equals the neighbouring flat ground's top; the log over it is the first above ground | Off by one |
| `a_wide_trunk_roots_from_each_columns_own_foot` | Wide jungle and dark oak, feet 0, -1, -2 and +1, +2 apart | Roots at each `foot[i]` and `foot[i] - 1` | One foot used for all |
| `mangrove_prop_roots_root_too` | 64 mangrove seeds on mud | Each drawn prop root has 2 logs at and under its foot | Prop roots missed |
| `roots_never_hang_or_eat_ores` | Air, water, an ore, bedrock under the top | 0 logs written there; the ore and bedrock unchanged; the root 1 log | The put mode is too wide |
| `roots_split_stone` | 1 soil over stone | 2 root logs, the lower in stone's place | Stone refused |
| `ordinary_logs_still_never_go_into_the_ground` | A trunk painted against a hillside | 0 hill blocks replaced above the root | `FEATURE_PUT_LOG` now takes ground |
| `nothing_but_trees_changes` | 3 seeds, 16 chunks each, the land against the land with roots masked | Every voxel but root voxels the same | A draw moved |
| `run_terrain_feature_own` (existing) | Box against chunk | Same bytes | A root depends on the box |
| `one_and_sixteen_workers_lay_the_same_blocks` (existing pattern) | 8 by 8 columns | Same bytes | Order dependence |
| `FEA_DIGEST_OF_GENERATOR[3]` | The land of version 3 | Pinned; rows 1 and 2 unchanged | |
| `run_save_world` generators | 0, 1, 2, 4 refused; 3 opens | | |
| `run_playthrough` | The finder's tree, the scripts | The day ends as before; the report names the tree and its distance | The body stands in the dirt |
| `run_player_felling` | With ground under painted trees | Passes with the old feller; limit still above twice the biggest | |
| Bench `bench_terrain` | Slowest place | ≥ 1000 columns/s | |
| Bench `bench_lod` | Per column | ≤ 40 ms | |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_save_world|run_player_felling)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_playthrough$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    pgrep -af "$PWD/build"    # must print nothing
    du -sb /home/ahmed/Projects/Games/MineCraft_C/saves/*    # the list for Q1; nothing is deleted here

## Gate

Ahmed opens a new world, finds a tree, digs the block beside its trunk at the ground and the one under it, and sees the trunk go two blocks into the dirt, the top one level with the grass. He also answers Q1 (which worlds to delete, and what to do with `test_8-3`).

## Risks and open questions

| Risk or question | Default |
|---|---|
| `bench_terrain`'s jungle place (1005) falls under 1000 | Fold the root writes into the trunk loop; measure again; record |
| The move to `tree/` changes a byte (an inlined order, a static moved) | The digests catch it; fix before roots go in |
| A render golden shows a log's side where a slope's dirt was | Expected only on slopes beside a trunk; made again, looked at, described |
| A cave right under the top makes a 1-log root | Fine: the root is 1 deep there; phase 02's root log rule needs 3 ground sides, which a flush root on flat ground has |
| Q1: `test_8-3` (generator 2) and the generator 1 worlds | Version 3; nothing deleted until Ahmed says |
| Q2: root depth | 2 |
| Q3: a stump block of its own | No |
| Q4: root flare | No |

## Commits

Appended as they land:

- (none yet)

## Issues found

1. **Q1: Ahmed chose version 3, 2026-11-14; old worlds deleted by the orchestrator.** The nine generator 1 worlds are gone; `saves/test_8-3` (generator 2) is left for the orchestrator to delete once this lands. This phase touches nothing in `saves/`. The version change is still its own last commit, so it reads alone.
2. **The move (step 1).** `terrain_feature_put` and its modes (`terrain_feature_put_t`, `TERRAIN_FEATURE_PUT_*`, renamed from the file's static `feature_put` and `FEATURE_PUT_*`) are now declared in `run/include/terrain/feature/index.h`, and stay in `feature/index.c`, since lakes, plants and spikes put with them too. `FEATURE_CLASSES` stays private to `feature/`. `feature_row_reach` asks `terrain_feature_tree_reach` for a tree's reach, as the table moved with the trees. `feature/index.c` went from 1473 lines to 1193, `tree/index.c` is 276. Every `run_terrain_.*` test passed on the moved code (14 of 14, the digests unchanged), before any root went in.
