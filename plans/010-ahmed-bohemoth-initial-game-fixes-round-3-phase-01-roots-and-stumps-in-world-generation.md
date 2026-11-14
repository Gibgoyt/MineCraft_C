# 010-ahmed-bohemoth-initial-game-fixes-round-3 — phase 1: Roots and stumps in world generation

**Status:** done, waiting on Ahmed's gate: roots drawn and tested, `TERRAIN_GENERATOR_VERSION` 3 (its own last commit), benches recorded   (updated 2026-11-14)

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
- [x] `TERRAIN_FEATURE_ROOT_DEPTH` 2, `FEATURE_PUT_ROOT` and the ground class; roots under the trunk, each wide column and each mangrove prop root
- [x] `tests/run/terrain/feature/roots/`: the root tests of section 5
- [x] `TERRAIN_GENERATOR_VERSION` 3, `FEA_DIGEST_OF_GENERATOR[3]`, every terrain pin that moved re-pinned with its reason; `run_save_world` refuses 2
- [x] `run_playthrough`'s finder aims at the first log over the ground; the scripts still fell their tree
- [x] `tests/run/player/felling/` paints its trees on solid ground; the limit test measures with roots
- [x] Benches: `bench_terrain`, `bench_lod`, before and after
- [x] READMEs: `run/src/terrain/feature/`, its new `tree/`, `plans/REPO_STRUCTURE.md`
- [x] List `saves/` with sizes and generators in the phase file, and ask Ahmed (Q1)

Expanded:

- [x] `FEATURE_PUT_LOG` still never writes over ground (a test: a trunk painted into a hillside leaves the hill's blocks)
- [x] No new random draw: tree, plant and vine sites and counts unchanged (`FEA_FOREST_PIN`'s tree count and every non-log voxel above the ground the same as before; a test compares the two lands with logs under the ground masked out)
- [x] Height, density, hydrology, biome, cave and ore digests unchanged
- [x] `run_terrain_feature_own` (box against chunk) and 1 against 16 workers: same bytes
- [x] Goldens that moved made again and described here
- [x] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

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

- c1047426 Move the tree shapes from terrain/feature into terrain/feature/tree, with no byte of the land changed (plan 010 phase 01 step 1)
- a5e40cac Root every generated tree two logs into the ground (plan 010 phase 01): the trunk, each wide column from its own foot and each mangrove prop root, over ground only
- (this commit) Raise TERRAIN_GENERATOR_VERSION to 3 for plan 010 phase 01's roots: the version, row 3, run_save_world refusing generators 1 and 2

## Issues found

1. **Q1: Ahmed chose version 3, 2026-11-14; old worlds deleted by the orchestrator.** The nine generator 1 worlds are gone; `saves/test_8-3` (generator 2) is left for the orchestrator to delete once this lands. This phase touches nothing in `saves/`. The version change is still its own last commit, so it reads alone.
2. **The move (step 1).** `terrain_feature_put` and its modes (`terrain_feature_put_t`, `TERRAIN_FEATURE_PUT_*`, renamed from the file's static `feature_put` and `FEATURE_PUT_*`) are now declared in `run/include/terrain/feature/index.h`, and stay in `feature/index.c`, since lakes, plants and spikes put with them too. `FEATURE_CLASSES` stays private to `feature/`. `feature_row_reach` asks `terrain_feature_tree_reach` for a tree's reach, as the table moved with the trees. `feature/index.c` went from 1473 lines to 1193, `tree/index.c` is 276. Every `run_terrain_.*` test passed on the moved code (14 of 14, the digests unchanged), before any root went in.
3. **The ground a root takes is a narrowing of `FEATURE_CLASS_SOLID`, not a new class in `FEATURE_CLASSES`.** `feature_root_takes` (in `feature/index.c`) is: of `FEATURE_CLASS_SOLID`, `BLOCK_FLAG_SOLID`, not `BLOCK_FLAG_LOG`, `BLOCK_FLAG_DECAYS` or `BLOCK_FLAG_UNBREAKABLE`, and not one of the 16 ores, listed by hand in `FEATURE_ORES`. The ore table's rows were not used, as they hold the rocks (granite, diorite, andesite, tuff) too, which roots split. `run_terrain_feature_roots` holds every block whose name ends `_ore` and every unbreakable block to it, so an ore added later fails that test until it is listed. As `FEATURE_CLASSES` did not change, `TERRAIN_FEATURE_PUT_LOG`'s rule (`there != FEATURE_CLASS_SOLID`) did not either: no ordinary log goes into the ground by construction, and `ordinary_logs_still_never_go_into_the_ground` checks it on 1324 trees against a hill.
4. **Each root voxel is decided by itself.** Under an ore or air at the top's place the root is not drawn there and the lower one still is: the top of a site is always soil or snow (`feature_tree_stands`), so this does not happen in the land, and reading the voxel above would break the purity rule (a box may hold one and not the other). The plan's "the ore stays and the root is 1 log" holds for an ore or a cave **under** the top, which is what the test checks.
5. **No new draw, measured.** `nothing_but_trees_changes` digests 3 worlds (the default, 11107447384583428913, 11) by 16 whole chunks, every voxel at a column's pre-feature top or the one under it that is a log or ground read as one mark. Pinned on step 1's code, before roots: `0x526F23C145164874`. With roots it is the same value, with 1355 root logs in it. `run_terrain_feature`'s forest still counts 83 trunks on 18.9 chunks.
6. **Pins that moved (all with "plan 010 phase 01: trees root two blocks into the ground").** `FEA_DIGEST_OF_GENERATOR[2]` `0xBF67826538D85EA0` to `0x8A84C8086A5091A5` while the version stays 2 (the last commit moves it to row 3 and puts row 2 back); `FEA_FOREST_PIN` `0x0A2981C595D9ACA2` to `0xB97FC1D587B0154F`. `only_what_is_open_changes_and_no_voxel_outside_the_world` now lets ground become a root (never an ore) and checks there are some (500, 40 and 149 in its three places were the solid-to-solid changes it found). Held, unchanged: `run_terrain_biome` (`BIO_COLUMNS_PIN`), density, hydrology, cave, cave walk, ore, fields, landform, climate sample and seed; `run_terrain_feature_own`; and `run_terrain_feature`'s 1, 16 and scattered workers agree with each other.
7. **Goldens made again** with `MC_GOLDEN_WRITE=1` and looked at against the old ones side by side with a diff (the old ones kept in the scratchpad): `tests/cuda/lod/fixtures/real_near.png` (123 pixels past the tolerance), `tests/run/render/frame/fixtures/start.png` (425), `north_low.png` (25) and `down.png` (19). Every changed pixel is at the foot of a trunk on a slope: where the step of the ground showed dirt or grass beside a trunk, it now shows the log's bark of the root. Nothing else moved; `real_far.png`, `hills*.png` and the other frame goldens are byte-for-byte what they were.
8. **The finder.** `pt_cut_point` goes up the lowest log's column to the first log with a side open to air or a plant; the 1 or 2 logs under it are the roots, with no log of the wood under the lowest. `ground_z` is the flush root; `pt_clear_round` counts the flush root as the trunk's own; the 12 to 64 logs counted are those at and over the cut. `run_playthrough` passed (6 of 6, 256 checks) with the old feller: seed 36's tree is now a mangrove, 14 logs over the ground, cut point at (80, 145, 94), 155 blocks from the start, stood at (78, 144). The last figure written in a plan file was a mangrove of 14 logs 874 blocks off (plan 007's land); main's own figure just before this phase was not measured, so the move is not put down to the roots alone.
9. **The felling tests.** `pf_paint` fills dirt up to `PF_Z - 1` in the tree's own column and up to its foot elsewhere, so every painted tree has its roots. `pf_fell_one` and the drop test count the logs at and over `PF_Z`, which a cut at the foot fells, and check the tree has roots under that. `run_player_felling` passes with the old feller. The biggest of every kind with roots: oak 8, birch 9, spruce 12, jungle 107, acacia 9, dark oak 50, mangrove 75, cherry 14; 2 x 107 = 214 is under `PLAYER_FELLING_LOGS_MAX` 256.
10. **`saves/` (2026-11-14, read only):** `test_8-3`, generator 2 (`blocks/manifest` offset 52), 91879095 bytes (89M), newest file 2026-11-14 11:05. The generator 1 worlds are already gone (issue 1).
11. **Benches before roots** (release, on step 1's code, the land byte for byte main's; load average up to 43 from other agents' builds during the run): `bench_terrain` slowest place jungle 953 chunk columns/s on 16 workers (plains 1828, forest 2125, mangrove swamp 1100, jagged peaks 1216, plateau top 1322), so already under 1000 before any root went in. `bench_lod`: worst column 30.90 ms (shift 2), shift 1 22.79 ms.
12. **A tree for the gate, on Ahmed's seed 11107447384583428913** (found with the playthrough's finder on that seed's real climate): a jungle tree of 62 logs over the ground, its trunk at x -131, y -560, the first log over the ground at z 4, so the flush root is at z 3, level with the grass, and the second at z 2. `/tp -133 -561 4` stands 2 blocks off it, facing it; dig the block beside the trunk at z 3 and the one under it.
13. **The version (step 5), its own last commit.** `TERRAIN_GENERATOR_VERSION` 3. `FEA_DIGEST_OF_GENERATOR` row 3 is `0x8A84C8086A5091A5` (the land with roots), row 2 back to `0xBF67826538D85EA0`, row 1 unchanged. `run_save_world`: `static_assert(TERRAIN_GENERATOR_VERSION == 3u)`, a new manifest says 3, generators 0, 1, 2 and 4 refused with the list's words; its comment was kept to one line more than before, as the file is at the 2000-line limit (the first full run's `standards_lint` caught 2001). `run/src/terrain/README.md` names version 3.
14. **Benches after roots** (release, on the roots commit; the version number does not change a block). `bench_terrain`'s jungle place on 16 workers, the two builds run one after the other at a load average of 1 to 4, the "before" being the same code with the root loop's depth set to 0: before 988, 790, 965, 957; after 958, 956, 950, 949. The 790 is one stray run of the before build. So the roots cost about 1% (medians about 961 and 953), within the run-to-run spread, and the jungle place was already under 1000 before any root went in (953 at load 43 earlier in the day, issue 11). The master's fallback, folding the root writes into the trunk loop, would save part of 2 to 8 puts per tree and cannot win back the 40 to 50 a second the place is short; that shortfall is main's, not this phase's, and is left for the orchestrator. Other places after: plains 1800, forest 2180, mangrove swamp 1023 to 1129, jagged peaks 1156, plateau top 1319, deep ocean 2508. `bench_lod` after: worst column 33.32 ms (shift 2), shift 1 24.22 ms, under the 40 ms budget (at load 40, when the shifts with no features moved by as much: 11.68 to 12.45 ms at shift 6). `build/release` was deleted after.
15. **The whole debug suite at -j 2 on the version commit:** 176 of 178 passed in the full run. `run_climate_warning`'s 20 ms timing failed under load and passed alone (7.3 s run), and `standards_lint` failed on `tests/run/save/world/index.c` at 2001 lines, fixed, then `standards_lint`, `clang_format`, `run_save_world` and `run_climate_warning` passed together. On the roots commit before it, `run_ui_atlas`'s lock wait and `run_climate_warning` had failed under load in the full run and passed alone; the 4 goldens were made again and `render_feature_lod`, `run_render_frame`, `run_player_felling`, `run_save_world` and `run_terrain_feature*` passed. `pgrep -af "$PWD/build"` prints nothing.
