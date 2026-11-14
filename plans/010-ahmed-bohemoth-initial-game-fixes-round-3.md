# 010-ahmed-bohemoth-initial-game-fixes-round-3 — master plan

**Status:** draft for approval (2026-11-14). Plans only: on approval, `plans/README.md` applies. This file is the master, kept verbatim from here on; the phase files in section 9 carry the work, and **no code is written until Ahmed says go.** Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-14

- "For tree feller I should never just cut on block and the entire tree falls. I have to cut a layer. Like in Hytale ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground in Hytale I really really love that feature."
- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please! There are actually loads of game logic that Hytale has for trees but we will not be doing that right now! Only be doing the tree feller stuff I think."
- "Trees must also have an underground root and not just start on top of dirt ... Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."
- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

So this plan is the tree feller and the stump. The farmer's workbench is **plan 011** (`plans/011-ahmed-bohemoth-farmers-workbench.md`), written now and done later.

### What the pictures show

- **`~/Pictures/10.png`**: the player stands close to a thick trunk, two logs by two, and looks at its bottom. Part of the layer just above the ground has been cut away: a notch, with the ring-marked tops of the logs under it showing. Everything above the notch still stands. **The tree falls only when every log of a layer that carries the trunk is cut.** One log, or three of four, is not enough.
- **`~/Pictures/11.png`**: after the fall, looking down. What is left is the stump: the bottom log layer, **level with the grass**, its log tops showing their rings. Felled wood and a few by-products lie on the grass round it, and the pickup list at the right counts the logs taken. Ahmed adds that **part of the stump goes underground**: the stump is not a log standing on the dirt, it sits in it.

The idea is the generic one: a trunk is cut through a whole layer, the part above comes down, and a rooted stump stays in the ground. The names, art and items are ours. Background reading on how such a game handles felling and regrowth (allthings.how, "Hytale tree regrowth explained") told us only the mechanics: in that game a stump does nothing more after the fall, and regrowth comes from saplings made at a separate farming bench. **Regrowth is out of scope** (section 8).

### What the game does today (read from the code, 2026-11-14)

1. **One cut fells the whole tree.** `player_felling_cut` (`run/src/player/felling/index.c`) is called by `run/src/player/interaction/index.c:419` for every log broken. It walks every touching log of the same wood **at or above the cut**, corners included, and queues them all if any touches leaves by a face. `player_felling_tick` breaks 8 a tick, nearest first, and drops them into the hands' ring. Then `world_tick_leaves_check` sweeps the box for leaves no log holds within 6 steps (`TICK_LEAF_REACH`).
2. **A stump exists only by accident.** A cut part way up leaves the logs under it (`a_cut_part_way_up_leaves_the_stump_and_takes_the_rest`). A cut at the lowest log takes everything, so nothing is left in the ground.
3. **Placed logs are told apart only by leaves.** "One of them must touch leaves, by a face. A log wall has none, and stays" (`run/src/player/felling/README.md`). A log wall with leaves against it is felled.
4. **Every tree stands on top of the ground.** `feature_row_chunk` sets `site.z = top + 1` (`run/src/terrain/feature/index.c:1105`). The lowest log is the first block over the column's top, and `feature_put(..., FEATURE_PUT_LOG)` never writes over solid ground. A wide trunk (jungle 3 in 4, every dark oak) has each of its four columns down to its own ground (`feature_wide_trunk`, `foot[0..2]`, up to 2 layers apart). A mangrove's roots arc from the trunk down to the ground beside it (`feature_tree_mangrove`).
5. **The land is version 2.** `TERRAIN_GENERATOR_VERSION = 2u` (`run/include/terrain/index.h:35`). `FEA_DIGEST_OF_GENERATOR[2] = 0xBF67826538D85EA0` (`tests/run/terrain/feature/index.c:76`). `save_world_open` refuses another generator (`run/src/save/world/index.c:550`).
6. **Tests that find trees in the land:**
   - `run_playthrough`'s finder (`tests/run/playthrough/index.c:309-450`) takes a tree's **lowest log** as its root, the block under it as the ground, and stands the body on that ground.
   - `fell.script` (`tests/run/player/felling/fixtures/`) makes one cut at that root and expects the feller to fell at least 2 more.
   - The day, bench and stairs scripts do the same through `@tree`.

### What is in the way

| Obstacle | Where | What this plan does |
|---|---|---|
| A tree is found by walking only up from the cut, so the stump and roots are never seen | `player_felling_cut` | The walk finds the whole tree, up and down, before deciding anything (2.3) |
| Placed logs with leaves on them fall like a tree | the leaves rule | A tree must also have **a log set in the ground**, which only generation makes in practice (2.3) |
| Logs never go over solid ground, so no root can be drawn | `feature_put`, `FEATURE_PUT_LOG` | A new put mode for roots, over ground only (2.2) |
| `run/src/terrain/feature/index.c` is at 1473 lines; the split mark is 1500 (`plans/CODING_STANDARD.md` section 5) | | The tree shapes move to `run/src/terrain/feature/tree/` first, with no byte changed (phase 01 step 1) |
| `tests/run/terrain/feature/index.c` is at 1630 lines | | The root tests go in a new `tests/run/terrain/feature/roots/` |
| The land changes, and saves hold edits over the land | `save/world/README.md` | `TERRAIN_GENERATOR_VERSION` 2 → 3, and worlds made by 2 are refused (section 7) |
| Ahmed's world `saves/test_8-3` (58 MB, 2026-11-14) is **generator 2**, so version 3 refuses it | `blocks/manifest` offset 52 reads 2 | Asked first, as Q1. The default follows the repo's rule (section 7) |
| `run_playthrough` stands the body on the block under the lowest log, which will be underground | `pt_box_tree` | The finder takes the first log over the ground as the cut, and the layer as its list of cuts (phases 01 and 02) |
| `bench_terrain`'s slowest place (jungle) is at 1005 columns a second against a floor of 1000 | plan 006 phase 04 Issues found 6 | Roots are two voxel writes per trunk column, measured before and after (section 4) |

## 1. What changes, in one paragraph

A generated tree's trunk now goes **two blocks into the ground**: the first log is level with the ground's top (it replaces the grass or dirt the tree stood on) and the second is under that. A tree comes down **only when every log of one horizontal layer of its trunk has been cut**. One log on a thin trunk, four on a 2 by 2. Then everything of that tree above the layer falls, as the feller drops logs today. Every log at and below the layer stays: that is the stump, rooted in the ground. Cutting a log that does not complete a layer removes just that log. A log that is not part of a tree with roots and leaves (a player's wall, tower or house) is never felled.

## 2. Design

### 2.1 Words used in this plan

| Word | Meaning |
|---|---|
| **Ground** | A block that is solid and is neither a log nor leaves |
| **Root log** (set in the ground) | A log with ground directly under it **and** ground or a log of the same wood on at least three of its four sides. A generated tree's deeper root always is one where its ground is whole. A log a player puts on the ground has air beside it and is not one |
| **The tree** | The logs of one wood joined to the cut by a face, edge or corner (the 26 neighbours, as today), found up **and** down from the cut, which (a) touch leaves by a face somewhere and (b) include at least one root log. Anything else is not a tree, and a cut removes just its one log |
| **Trunk column** | An (x, y) column of the tree holding a root log |
| **The trunk** | The trunk columns joined by a side or corner to the cut's column. A thin tree has 1 column, a wide one 4, and a mangrove its trunk plus each prop root. Two trunks touching by a side or corner are one trunk, which is a risk (section 6) |
| **Layer h of the trunk** | The tree's logs at height z = h in the trunk's columns. **Branches are never part of a layer:** a branch log stands in a column with no root log |
| **The stump** | What stays after a fall: the logs at or under the cut layer that are still joined to a root log |

### 2.2 Roots in world generation (phase 01)

**What is drawn.** Every column of a tree that today starts on the ground goes on down **`TERRAIN_FEATURE_ROOT_DEPTH` = 2** blocks:
- the trunk (`site.z - 1` and `site.z - 2`, which is the column's `top` and `top - 1`);
- each column of a wide trunk, from its own foot (`foot[i]` and `foot[i] - 1`);
- each mangrove prop root, from its own foot.

So the first root log is **level with the ground**, its ringed top showing where the grass was, exactly as in `11.png` once the tree above is gone. The second is wholly underground ("a little of that stump goes underground").

**What a root may replace: `FEATURE_PUT_ROOT`.** A new put mode, over solid ground only. It never writes over:
- air, water, plants, leaves or logs, so a root never hangs in a cave or a lake;
- bedrock, any unbreakable block, or an ore, so roots never eat ores.

It does go over stone, because roots split rock, and a root that stopped at thin soil would leave a stump with one log and an edge case for felling.

Each root voxel is decided only by what is at that voxel, so the feature stage's rule holds: "What a step writes at a voxel depends only on what is at that voxel" (`run/src/terrain/feature/README.md`, "How a voxel stays a pure function"). Two voxels under a column's top are well inside `FEATURE_BELOW` (lake depth + 4), so no reach or margin changes. The wide trunk's margin and the mangrove's are unchanged too.

**`FEATURE_CLASSES` gets a ground class** for the root mode. `FEATURE_PUT_LOG` keeps "everything but solid ground and logs", so ordinary logs still never go into the ground.

**Logs, not a new block.** The stump is ordinary logs of the tree's wood. Why:
- `11.png` shows log tops with their rings, and every log already has a ringed top picture (`config/blocks/oak_log/index.yaml`: `top: blocks/oak_log_top`).
- A dug-out stump gives the wood it is, with no new item, drop or recipe.
- A stump or root block would be 8 new blocks (one per wood), with pictures, save names and drops, and the felling, the finder and the leaf rule would all need to know them.
- What makes a root a root is **where it is** (in the ground, 2.1), not what block it is.

A separate look for a stump (bark with soil on its sides) is left as Q3.

**No root flare.** The trunk goes straight down; no extra logs spread out at the ground. `11.png`'s stump is a cross of log tops round a thick trunk; a flare could come later (Q4).

### 2.3 Layer felling (phase 02)

`player_felling_cut(felling, map, block, x, y, z)` keeps its signature and caller. The cut log is already air when it is called. It now works in three steps, all on the sim with no heap.

**1. Find the tree.** A walk over the same wood from the cut's 26 neighbours, up and down, into a new list `found` of up to `PLAYER_FELLING_FIND_MAX` = 1024 logs, with its own `seen` hash. While it walks it notes:
- whether a log touches leaves by a face;
- which logs are root logs;
- each log's column.

It stops, and **nothing falls** (just the cut log goes, as by a hand), if:
- a column it needs is not loaded;
- it overflows the list (counted in `find_overflow_count`);
- the logs have no leaves or no root log.

Today a column not loaded only stops the walk early and the rest is felled. **Nothing falls when the walk is unsure.**

**2. Did a layer go?** Only if the cut log was in a trunk column:
- Work out **the trunk**: the trunk columns joined to the cut's column.
- The cut is at height h. If **no log of layer h remains** in the trunk, and the tree has logs above h in the trunk's columns, the layer has gone.
- Then **what falls** is every log of the tree with z > h that a path of logs with z > h joins to the trunk's logs above h. The path may not enter another trunk's columns.
- So the crown and every branch above the layer fall, even where a branch's first log, beside the trunk at height h, would join the stump to the crown corner to corner.

**3. Nothing floats.** After step 2, or straight away when the cut was a branch log, every log of the tree that no longer joins a root log through logs falls too. So:
- cutting an acacia's lean brings down the lean and its crown, and the trunk stands;
- cutting a cherry bough brings down that bough;
- cutting one log of a 2 by 2 layer brings down nothing.

What falls goes into today's queue (`cells`, nearest the cut first, 8 a tick, waiting for room in the hands' ring). It drops what a hand would and wears nothing but the cut log, all as today. When the queue ends, `world_tick_leaves_check` sweeps the box as today.

**The leaves** follow today's rule: a leaf goes when no log is within 6 steps of it through leaves.
- A cut at the bottom layer, or up to eye height, leaves a stump that touches no leaf, so **every leaf goes**. This is today's `wherever_a_tree_is_cut_no_wood_is_left_floating` claim, kept as a test.
- A cut high inside the crown keeps the leaves the stump still holds, so nothing floats (Q5).

**What stays the same:**
- any tool or none, sneaking or not;
- a second cut while a tree comes down queues behind it;
- torches and plants on a falling log go with it;
- `felled_count` and `last_block` for the sound and the report;
- the hands' ring rules.

**What the struct costs.** `found` (1024 × 12 bytes) and its hash (4096 × 2 bytes) add about 20 KiB to `player_felling_t`. It already lives in the world arena, not on `app_t`, so the frame limit is untouched. Its `static_assert`s and field offsets are updated. The walk reads at most 1024 × 26 voxels: **under 1 ms on the biggest tree**, measured by a test.

### 2.4 Tests that find trees in the land

`run_playthrough`'s finder learns the new shape:

- **Phase 01** (roots): the "root" the scripts aim at becomes **the trunk's first log over the ground**, the lowest log with air beside it on the stand's side, not the lowest log. `ground_z` is the block under that log, which is now the flush root log, and the stand is on the ground beside it. The finder's "no log under the root" check changes to match. With roots only, one cut there still fells the tree, as the old feller does.
- **Phase 02** (layers): the finder also records the **cut layer's logs** (1 to 4, or more for a mangrove with prop roots at that height). `@tree` and `@fell` type one aimed cut per log, nearest the stand first, so the back logs are seen through the gap the front ones leave.
  - The finder prefers a tree whose layer is one log, nearest first, among trees with at least `PT_TREE_LOGS_MIN` (12) logs **above** the cut. The stump's logs no longer drop, so they no longer count.
  - The report check stays: the cut was a log, and the feller felled at least 2 more. `fell.script` gains a look down at the stump after the fall, `stump.png`.

## 3. What does not change

- Height, density, hydrology, biomes, surface, caves, ores, lakes, plants and snow. Only trees gain roots.
- Tree shapes above the ground, tree reach, site margins and tree counts. Sites and rates are drawn exactly as before.
- Leaf decay (`run/src/world/tick/`) and its 6-step reach.
- Drops, wear, the hands' ring, the felling queue's 8 a tick.
- Recipes and stations (plan 011 does the farmer's workbench).
- The save format. Only the generator number moves.

## 4. Costs and limits

| What | Budget | How it's held |
|---|---|---|
| Terrain throughput | `bench_terrain` stays at or above 1000 columns a second at its slowest place (jungle, 1005 at plan 006 phase 04) | Measured before and after in phase 01. Roots are 2 voxel writes per trunk column (8 for a wide tree) |
| The far field | `bench_lod` at or under 40 ms a column | Measured in phase 01 |
| The find | Under 1 ms for the biggest tree of every kind, on the sim | A test in phase 02 times it over 100 cuts |
| `player_felling_t` | About 26 KiB, from the world arena | `static_assert` |
| Determinism | 1 and 16 workers give the same bytes; a box and a chunk give the same bytes (`tests/run/terrain/feature/own/`) | Tests in phase 01 |

## 5. Tests the plan relies on

- **Roots:** for every tree kind over 64 seeds, painted on solid ground, every trunk column (each of a wide trunk's four, each mangrove prop root) has exactly 2 logs at and under its ground top. The top one is level with the ground.
- **Roots never hang:** with air, water, an ore or bedrock under the top, the root stops there: 0 logs over air or water, 0 ores replaced.
- **Pure function:** `own/`'s box-against-chunk test and 1 against 16 workers give the same bytes.
- **Version:** `TERRAIN_GENERATOR_VERSION` 3, `FEA_DIGEST_OF_GENERATOR[3]` pinned, rows 1 and 2 unchanged. `run_save_world` refuses generators 1 and 2 and opens 3.
- **A thin trunk:** one cut at any trunk height fells everything above. The stump is every log under the cut, roots included.
- **A wide trunk:** in every one of the 24 orders of cutting its four logs at one height, nothing falls after cuts 1 to 3, and everything above falls on the 4th.
- **A branch at the layer:** a wide jungle tree cut through all four logs at the height of a branch's first log falls whole above the layer.
- **Branches:** cutting an acacia's lean fells the lean and crown and leaves the trunk; a cherry bough the same.
- **Not a tree:** a log tower on the ground with leaves on it (no root log) is never felled; a log wall with no leaves is never felled; a stump (roots, no leaves) loses only the log cut.
- **Nothing floats:** every kind, every seed, every trunk height cut through a full layer: 0 logs and 0 vines left floating, and 0 leaves left for a cut at or under eye height.
- **No drop lost, no heap, same bytes for the same cuts:** as today's tests.
- **The playthrough:** the found tree is felled by cutting its layer; the report says so; `stump.png` shows a flush stump.

## 6. Risks

| Risk | What's done about it |
|---|---|
| Ahmed loses `saves/test_8-3`, a generator 2 world he is playing (58 MB, 2026-11-14) | Asked first (Q1). Nothing is deleted without his word. The default is the repo's rule: the version goes to 3 and the world is refused, not migrated |
| A kept generator 2 world would have trees with no roots in its saved sections, which the new rule would never fell | Another reason not to open generator 2 worlds on version 3; recorded |
| `bench_terrain`'s jungle place drops under 1000 | 2 writes per column is tiny next to the walk, so a fall is unlikely. If it falls, the root loop is folded into the trunk loop and measured again |
| Two trunks of one wood touching (dense dark oak) are one trunk, so the player must cut both layers | Rare: sites are random and logs never go over logs. Recorded, and counted in the playthrough's survey of found trees |
| A branch resting on a hillside is a root log if three of its sides are ground | Its column becomes a trunk column, and its log joins the layer at that height. Rare, physically fair ("it is propped up"); tested on the generator's trees, and recorded if it shows |
| A log a player sets into a hole in the ground, with leaves touching, counts as a tree | That is the rule as written (2.1). Recorded in the felling README as the one way to build something it fells |
| Felling now needs roots, so it silently stops working on a tree drawn without them (a test canvas of air, an old world) | The felling tests paint every tree on solid ground. The playthrough's report fails if nothing is felled |
| The walk now goes down as well as up, and is bigger | Bounded by `PLAYER_FELLING_FIND_MAX`, measured under 1 ms |
| The 2000-line hard limit | The tree code moves to `feature/tree/` before roots are added; root tests get their own directory |

## 7. Old worlds

The land changes (every tree gains roots), so, by the save's rule ("on another generator's land a saved edit would meet a seam"):
- `TERRAIN_GENERATOR_VERSION` goes from 2 to 3;
- `FEA_DIGEST_OF_GENERATOR` gains row 3, with its reason, and rows 1 and 2 stay;
- worlds of generators 1 and 2 are refused, with the existing message, and still show on the list as made by an older generator.

**What is in `saves/` (listed 2026-11-14, `blocks/manifest` offset 52):**
- Nine generator 1 worlds, `test_1` to `test_8` and `test_8-2`, already refused by version 2. They are waiting on Ahmed's word to delete them, with the command written in plan 006 phase 05 Issues found 3.
- **`test_8-3`: generator 2**, 58 MB, last written 2026-11-14. Version 3 would refuse it too.

Phase 01 lists them all again with sizes and **asks before anything is deleted**. Phase 03 deletes only what Ahmed names (Q1).

## 8. Out of scope

Ahmed: "There are actually loads of game logic that Hytale has for trees but we will not be doing that right now! Only be doing the tree feller stuff I think." So, not in this plan:
- regrowth of trees, and saplings from stumps or from a bench;
- tree sap, resin or any new by-product of felling, and any new item at all;
- an "essence" or any crafted growth item;
- the farmer's workbench: **plan 011** (`plans/011-ahmed-bohemoth-farmers-workbench.md`), later;
- stumps rotting, being dug out by a tool of their own, or growing back;
- a falling-tree animation: logs still break into drops, as today;
- root flare and a stump block of its own (Q3, Q4);
- leaves of a felled tree going regardless of the stump (Q5).

## 9. Phases

Each phase ends somewhere worth stopping, and becomes one phase file with the template's Goal, Todo, Verification, Commits and Issues found. Every phase file also gets the richer parts plan 006 uses:
- read first;
- must NOT build;
- owned paths;
- steps with a *Why*;
- tests with numbers;
- the gate;
- risks and open questions.

Every heavy command goes through `tools/test-or-build.sh`.

**Order.** Roots come before layer felling. The new feller's idea of a tree needs a root log, so felling first would leave every generated tree unfellable until the roots landed. Felling first would also mean the playthrough's finder changed twice for nothing.

### Phase 00: Vision and plan files

- [ ] This master in `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md`, committed alone
- [ ] Phase files 01 to 03 from the template, every box unticked, each linking back to phase 00, one commit each
- [ ] Phase 00's file restates the pictures, the words of 2.1, the order, and what is out of scope

### Phase 01: Roots and stumps in world generation

- [ ] Move the tree shapes from `run/src/terrain/feature/index.c` to `run/src/terrain/feature/tree/`, no byte of output changed (every digest holds)
- [ ] `TERRAIN_FEATURE_ROOT_DEPTH` 2, `FEATURE_PUT_ROOT` and the ground class; roots under the trunk, each wide column and each mangrove prop root
- [ ] `tests/run/terrain/feature/roots/`: the root tests of section 5
- [ ] `TERRAIN_GENERATOR_VERSION` 3, `FEA_DIGEST_OF_GENERATOR[3]`, every terrain pin that moved re-pinned with its reason; `run_save_world` refuses 2
- [ ] `run_playthrough`'s finder aims at the first log over the ground; the scripts still fell their tree
- [ ] `tests/run/player/felling/` paints its trees on solid ground; the limit test measures with roots
- [ ] Benches: `bench_terrain`, `bench_lod`, before and after
- [ ] READMEs: `run/src/terrain/feature/`, its new `tree/`, `plans/REPO_STRUCTURE.md`
- [ ] List `saves/` with sizes and generators in the phase file, and ask Ahmed (Q1)

**Gate:** Ahmed opens a new world, digs beside a tree, and sees its trunk go two blocks into the dirt, the top one level with the grass.

### Phase 02: Layer felling

- [ ] `player_felling_cut`: the find (up and down, `found`, roots, leaves, overflow), the layer rule and the nothing-floats rule, as 2.3
- [ ] `player_felling_t` grows; its asserts and README
- [ ] `tests/run/player/felling/`: the tests of section 5, rewriting those that assumed one cut fells a wide trunk
- [ ] `run_playthrough`: the cut layer as a list of cuts; `fell.script`'s `stump.png`
- [ ] `run/src/player/felling/README.md` rewritten for layers, roots and the stump

**Gate:** Ahmed cuts one log of a jungle giant's bottom layer, sees it stand, cuts the other three, sees it fall and its stump stay level with the ground.

### Phase 03: The final check

- [ ] Full debug and release suites; a release build; 1 against 16 workers on the new pins
- [ ] `bench_terrain`, `bench_lod`, the find's time
- [ ] The old worlds: delete exactly what Ahmed names, nothing else
- [ ] Ahmed's gate notes, in his words

**Gate (Ahmed):**
1. Cut a thin oak through its bottom layer (one log): it falls, and a flush stump with a root under it stays.
2. Cut a 2 by 2 jungle tree's bottom layer: it stands after 1, 2 and 3 logs and falls on the 4th.
3. Look down at the stump: level with the grass, rings showing; dig beside it and see it go underground.
4. Cut one log out of the middle of a trunk's side on a wide tree: it stays standing.

## 10. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | `saves/test_8-3` is a generator 2 world (58 MB, played 2026-11-14), and version 3 refuses it. Keep it refused on the disk, delete it, or keep the version at 2 and accept seams and unfellable rootless trees in it? And which of the nine generator 1 worlds to delete? | Version 3; `test_8-3` stays on the disk, refused, until Ahmed says delete; the generator 1 worlds are deleted only on his word |
| Q2 | Root depth | 2: one log level with the ground and one under it |
| Q3 | A stump look of its own (a "rooted log" block per wood)? | No: ordinary logs, which show their rings |
| Q4 | Root flare (logs spread at the ground round wide trunks, like the cross in `11.png`)? | No, for now |
| Q5 | Should every leaf above the cut layer go even when the stump still holds it (cuts high in the crown)? | No: the leaf rule decides, so nothing floats and nothing held is taken |
| Q6 | Should a branch cut bring down only that branch (nothing floats), or nothing at all? | Only that branch, so no wood floats |

## Verification of this plan

    ls plans/010-*
    grep -L "## Issues found" plans/010-*phase-*.md   # prints nothing
    git status --short                               # nothing uncommitted
