# 010-ahmed-bohemoth-initial-game-fixes-round-3 — phase 2: Layer felling

**Status:** done, rebased onto phase 01 (`58a8411e`); the full debug suite below   (updated 2026-11-14)

Read first: `plans/010-ahmed-bohemoth-initial-game-fixes-round-3-phase-00-vision-and-plan-files.md`, then the master `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md` (sections 0, 1, 2.1, 2.3, 2.4, 4 to 6 and 9), then phase 01's file for what the roots are. Every number here comes from the master. Where this file adds one, it says so and why.

## Goal

**A tree comes down only when every log of one layer of its trunk is cut.** That is 1 log on a thin trunk, 4 on a 2 by 2, and the trunk plus any prop root at that height on a mangrove.

- **What falls:** when the last log of layer h goes, everything of that tree above h falls. That is the crown and every branch above the layer, even a branch whose first log beside the trunk at height h would join stump to crown corner to corner. The logs break into drops 8 a tick, nearest the cut first, as today.
- **What stays:** every log at and under h that still joins a root log is the stump.
- **A cut that leaves the layer incomplete** removes that one log, and the tree stands.
- **A cut branch** brings down only what no longer joins a root log; nothing floats.
- **What is never felled:** a log that is not part of a tree, meaning one with leaves touching it somewhere and a root log (master 2.1). So a player's wall, tower or house of logs is never felled, leaves or not, and neither is a stump.
- **When the walk is unsure** (a column not loaded, more than `PLAYER_FELLING_FIND_MAX` logs), nothing falls.

Leaves follow today's leaf rule. A cut at or under eye height leaves no leaf; nothing floats. The find takes under 1 ms on the biggest tree of every kind.

`run_playthrough` fells its tree by cutting its whole layer, and `fell.script` looks down at the flush stump (`stump.png`).

## Where this fits

**The vision.** Ahmed: "I should never just cut on block and the entire tree falls. I have to cut a layer ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground". This is that.

**What it reads from phase 01:** every generated tree has a root log in each trunk column (a log level with the ground and one under it), so "a tree must hold a root log" is true of every generated tree and false of a log put on the ground.

**What later phases read from it:** phase 03's gate, and the felling report line the playthrough prints.

**What this phase must NOT build:**
- No change to the land (phase 01 is the land's).
- No falling animation: logs still break into drops (master section 8).
- No regrowth, sap, new items, or saplings from stumps.
- No change to the leaf rule (`run/src/world/tick/`), to drops, to wear (only the cut log wears what is in hand), or to the hands' ring.
- No setting to turn layer felling off: Ahmed asked for the feller always on (plan 005 phase 01), and this is how it now works.

## Ahmed's words

- "For tree feller I should never just cut on block and the entire tree falls. I have to cut a layer. Like in Hytale ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground in Hytale I really really love that feature."
- "Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer!"

`10.png`: a notch cut in the bottom layer of a 2 by 2 trunk, and the tree still standing.

---

## Background, explained

### The feller today

Read from the code, 2026-11-14 (`run/src/player/felling/index.c`, `run/include/player/felling/index.h`, README):

- `player_felling_cut` pushes the cut, then walks the queue as its own list: every 26-neighbour of the same wood **at or above the cut's z** is pushed, and `leafy` is set if any of them touches a `BLOCK_FLAG_DECAYS` block by a face. With no leaves, `felling_walk_undo` takes this walk out.
- The queue `cells[PLAYER_FELLING_LOGS_MAX = 256]` with `seen[1024]`, `head` and `tail`, is drained by `player_felling_tick`, at most `PLAYER_FELLING_PER_TICK` (8) a tick, holding back while the hands' ring lacks room. Each log goes by `world_edit_block_set`, drops if `player_interaction_harvests`, takes what stood on it (`player_interaction_stack_break`), torches included.
- When the queue empties, `world_tick_leaves_check(box)` sweeps the leaves.
- `player_felling_t` is 5184 bytes, in the world arena (`run/src/game/index.c:697`), not saved.

### The layer, worked through

**A thin oak** (the trunk at x, y from z 71 to 74, roots at 70 (flush) and 69, leaves from 72):

| Cut | Layer | What falls | What stays |
|---|---|---|---|
| z 71 (first over the ground) | {71}: empty after the cut | 72 to 74 and the leaves | 70, 69: the stump, flush |
| z 73 | {73}: empty | 74, and leaves no log holds | 69 to 72, and the leaves 72 holds |
| z 70 (the flush root, digging) | {70}: empty | 71 to 74 | 69 |

**A 2 by 2 jungle tree** (columns A, B, C, D, cut at z 81, every column's first log over the ground):

| Cuts so far | Layer 81 left | Falls |
|---|---|---|
| A | B, C, D | nothing |
| A, B | C, D | nothing |
| A, B, C | D | nothing |
| A, B, C, D | none | everything above 81 |

The order does not matter (24 orders, one test).

**A branch at the layer.** A wide jungle branch starts at `from` with its first log at `from + 1`, diagonal to the trunk (`feature_tree_jungle`). That log touches the trunk at `from`, `from + 1` and `from + 2` by corners. Cut the 4 trunk logs at `from + 1`:
- by joining alone, the branch log still holds the crown up through the trunk at `from`;
- by the layer rule, the tree falls: everything above `from + 1` reachable from the trunk's logs above through logs above `from + 1` goes;
- the branch log at `from + 1` stays, joined to the stump, and is part of it.

**An acacia** leans 1 or 2 logs from the top of its straight trunk:
- Cutting a lean log is a branch cut. The lean above and the crown (which only it holds) no longer join a root log, so they fall, and the trunk stands.
- Cutting the straight trunk at any height is a layer cut (the layer is 1 log), so all above falls.

**A mangrove** with prop roots on 2 sides, each from its own foot up to the water's top plus 1:
- Its trunk is 3 columns.
- At a height the prop roots reach, the layer is 3 logs.
- Above them, 1.

### Why "a root log" and not just "leaves"

Today a log wall with leaves on it is felled (README: "it is not perfect"). A root log needs ground under it and ground or the same wood on 3 of its 4 sides. A log placed on the ground has air on all 4 sides, and a log placed on another log has a log under it, not ground. So no build of logs on the ground is a tree, whatever leaves touch it. The one way to make one is to set a log into a hole and hang leaves on its wood: recorded in the README as the rule's edge.

### What the find costs

At most `PLAYER_FELLING_FIND_MAX` (1024) logs, each looking at 26 neighbours, by `world_map_block_get`. The biggest generated tree (a wide jungle with branches, about 100 logs with roots by phase 01's measure) is about 2600 reads. The bound is 26 624. The master's budget is under 1 ms on the biggest tree; the test times 100 cuts.

---

## Design

### `player_felling_t`

New fields (sizes are this file's estimate; the asserts take the real ones):

| Field | Size | What |
|---|---|---|
| `found[PLAYER_FELLING_FIND_MAX]` | 1024 × 12 | The tree, as `player_felling_cell_t`, in walk order |
| `found_seen[4 × PLAYER_FELLING_FIND_MAX]` | 4096 × 2 | Its own hash, a power of two (`static_assert`) |
| `found_flags[PLAYER_FELLING_FIND_MAX]` | 1024 | Per log: root, trunk column, falls, joined |
| `find_overflow_count`, `find_unloaded_count`, `not_a_tree_count`, `stood_count` | 4 × 8 | Counts, for tests and a report |

The fall queue (`cells`, `seen`, `head`, `tail`) stays as it is. What falls is pushed there nearest the cut first: the walk order from the cut is already distance order. `PLAYER_FELLING_LOGS_MAX` stays 256, held above twice the biggest tree by the existing test.

### `player_felling_cut`, in three steps (master 2.3)

1. **Find.**
   - Walk the 26 neighbours of the same wood from the cut, up and down, into `found`.
   - Mark each log that touches leaves by a face (`leafy`) and each **root log**: ground under it, and ground or the same wood on 3 of its 4 sides. Ground is `BLOCK_FLAG_SOLID`, not `BLOCK_FLAG_LOG`, not `BLOCK_FLAG_DECAYS`.
   - A neighbour whose column is not loaded ends the cut with nothing felled (`find_unloaded_count`). So does a full `found` (`find_overflow_count`).
   - No leaves or no root log: nothing felled (`not_a_tree_count`).
2. **Layer.**
   - Trunk columns are the columns of root logs.
   - If the cut's (x, y) is one, **the trunk** is the trunk columns joined to it through trunk columns by a side or corner (a small flood over at most `found`'s columns).
   - **Layer h** is `found`'s logs at the cut's z in the trunk's columns.
   - If it is empty and some log of the trunk's columns is above h, the layer has gone. Flood from the trunk's logs above h through `found`'s logs with z > h, never entering a trunk column that is not the trunk's. Every log reached falls.
3. **Nothing floats.**
   - Flood from every root log through `found`'s logs that do not fall; a log not reached falls too.
   - This alone handles a branch cut (the cut not in a trunk column), and anything a layer cut left hanging.
   - If nothing falls, the tree stands (`stood_count`).

Then push what falls onto the queue in `found`'s order, and set the box as today. The cut itself is in the box, so its leaves are looked at even when nothing else falls.

**Returns** how many were queued, the cut among them: 1 when nothing falls. The interaction caller ignores it, as today.

### What stays as it is

`player_felling_tick`, `player_felling_pending`, `player_felling_start`, the interaction's call site (`run/src/player/interaction/index.c:419`), `run/src/game/`'s use of `felled_count` and `last_block`, drops, wear, torches, the ring, the leaf sweep.

### The playthrough (master 2.4)

`pt_tree_t` gains `cut_z` and a short list of the cut layer's logs (up to 4 kept, more refused, so a mangrove with 2 prop roots at the cut height is fine).
- `pt_box_tree` works out the layer at the cut point (phase 01's first log over the ground) by the same words as the feller.
- It prefers a tree whose layer is 1 log, then the nearest, among trees with at least `PT_TREE_LOGS_MIN` (12) logs **above** the layer, since the stump no longer drops.
- `pt_tree_lines` types one aimed cut per log of the layer, nearest the stand first, each held long enough for the hand, and each aim worked out from the stand to that log's middle.
- The back logs of a 2 by 2 are seen through the gap the front ones leave. If a back log is hidden from every stand, the finder refuses that tree.
- After the last cut, `fell.script` looks down at the stump (`+8 move 0 <down>` then `screenshot stump.png`).
- The run's report check stays: the cut was a log, and `the tree feller felled N` with N ≥ 2. A new line names the layer's size and the stump's logs.

## Owned paths

This phase may edit these, plus this file:
- `run/src/player/felling/` (`index.c`, `README.md`), `run/include/player/felling/index.h`
- `tests/run/player/felling/` (`index.c`, `README.md`, `fixtures/fell.script`)
- `tests/run/playthrough/` (`index.c`, `README.md`, the fixtures' `@tree` handling only)

**Wiring outside them:** `run/src/game/` only if a count is reported (a log line).

**Not touched:** `run/src/terrain/`, `run/src/world/tick/`, `run/src/player/interaction/` (its call stays), `config/`.

## Steps

1. **The struct and its asserts**, with `found`, its hash and flags. *Why: the find needs its own list; the queue keeps its meaning.*
2. **The find**, up and down, with root logs, leaves, unloaded and overflow ending it with nothing felled. *Why: the stump and roots are below the cut; an unsure walk must fell nothing.*
3. **The layer rule.** *Why: Ahmed's "I have to cut a layer".*
4. **The nothing-floats rule.** *Why: branch cuts (acacia, cherry) and anything a layer cut left hanging (master Q6).*
5. **Tests**, below. Rewrite the tests that assumed one cut fells all, recorded in Issues found with the old and new expectation:
   - `a_giant_cut_at_the_level_of_its_middle_leaves_only_what_is_below_that` now cuts all four;
   - `every_kind_of_tree_comes_down_whole_with_a_drop_for_every_log` cuts each kind's whole bottom layer, and every log above it drops;
   - `past_the_limit_the_rest_stay` becomes "past the find's limit, nothing falls";
   - `a_column_not_loaded_stops_the_walk_and_nothing_goes_wrong` becomes "... and nothing falls".
6. **`run/src/player/felling/README.md`**, rewritten: what counts as the tree (leaves and a root log), the trunk, the layer, the stump, branches, the edge of the rule (a log set in a hole), and why an unsure walk fells nothing. *Why: the README's table is how the next reader learns the rule.*
7. **The playthrough**: the layer as cuts, `stump.png`, the report line. Run it, record the tree it found (kind, logs, layer size, distance), look at `before.png`, `cut.png`, `later.png` and `stump.png`, and describe them here. *Why: the proof by eye that Ahmed will repeat at the gate.*
8. **Benches: the find's time** over 100 cuts of the biggest tree, recorded. Tick as each lands.

## Todo

From the master (section 9, phase 02):

- [x] `player_felling_cut`: the find (up and down, `found`, roots, leaves, overflow), the layer rule and the nothing-floats rule, as 2.3
- [x] `player_felling_t` grows; its asserts and README
- [x] `tests/run/player/felling/`: the tests of section 5, rewriting those that assumed one cut fells a wide trunk
- [x] `run_playthrough`: the cut layer as a list of cuts; `fell.script`'s `stump.png`
- [x] `run/src/player/felling/README.md` rewritten for layers, roots and the stump

Expanded:

- [x] A column not loaded, or a find past `PLAYER_FELLING_FIND_MAX`, fells nothing and is counted
- [x] A log tower on the ground with leaves on it is never felled (root log rule)
- [x] The find is under 1 ms on the biggest tree of every kind (100 cuts timed)
- [x] No heap, the same bytes for the same cuts (existing `the_same_cut_gives_the_same_bytes_and_no_heap`)
- [x] The pictures from `run_playthrough` looked at and described here
- [x] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `a_thin_trunk_falls_when_its_one_log_layer_is_cut` | Oak, birch, spruce, small jungle, cherry; each trunk height | All logs above go; logs under, roots included, stay | The cut layer misread |
| `a_wide_trunk_stands_until_its_fourth_log_in_every_order` | Wide jungle and dark oak, 24 orders | 0 logs fall after cuts 1 to 3; every log above falls after the 4th | Joining alone used |
| `a_branch_beside_the_layer_does_not_hold_the_crown` | Wide jungle, cut at a branch's `from + 1` | Everything above the layer falls; the branch log at the layer stays | The layer rule missing |
| `an_acacia_cut_at_its_lean_drops_the_crown_and_keeps_the_trunk` | 32 acacia seeds | Lean and crown fall; the straight trunk stays | Nothing-floats missing |
| `a_cherry_bough_cut_drops_only_that_bough` | 32 cherry seeds | The bough and its blob's wood go; the trunk and other bough stay | |
| `a_mangrove_layer_holds_its_prop_roots` | Mangroves with 2+ prop roots | Cutting the trunk only, at a prop root's height, fells nothing; the trunk and prop roots there fell it | Prop roots not trunk columns |
| `the_stump_is_flush_and_rooted` | Every kind, cut at the first layer over the ground | The 2 roots per trunk column stay; the top one level with the ground | The stump taken |
| `a_log_tower_with_leaves_is_never_felled` | 6 logs placed on dirt, leaves round the top | Only the cut log goes | Root log rule missing |
| `a_log_wall_with_no_leaves_is_left_alone` (existing) | Any tool | Only the cut log goes | |
| `a_stump_loses_only_the_log_cut` | A felled tree's stump | 1 log goes | A stump counted as a tree |
| `an_unloaded_column_or_a_huge_find_fells_nothing` | A column missing; a find of 1025 | Only the cut; counts 1 | Partial felling |
| `wherever_a_tree_is_cut_no_wood_is_left_floating` (existing, now through full layers) | Every kind, seed, height | 0 floating logs, 0 floating vines; 0 leaves at or under eye height | |
| `no_drop_is_lost_on_the_biggest_tree_...` (existing) | | `drops_lost_count` 0 | |
| `the_same_cut_gives_the_same_bytes_and_no_heap` (existing) | | Same bytes, 0 heap | |
| `the_find_takes_under_a_millisecond` | Biggest tree of every kind, 100 cuts | Mean and worst under 1 ms (release); recorded in debug | Walk too wide |
| `run_playthrough` | The found tree's layer cut by the script | `felled N ≥ 2`; the hand holds what the day needs | A layer log missed |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_felling|run_player_interaction.*)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_playthrough$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_player_felling$' --output-on-failure
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed, in a new world:
1. Cuts one log of a 2 by 2 jungle tree's bottom layer and sees it stand.
2. Cuts the other three and sees it fall, with a stump left level with the ground.
3. Cuts a thin oak through its first log over the ground and sees it fall.
4. Cuts a log out of a house wall with leaves against it and sees nothing else go.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Two trunks of one wood touching are one trunk | Both layers must be cut; recorded if the playthrough's survey or Ahmed meets it |
| A branch resting on a hillside becomes a trunk column | Its log joins the layer at its height; rare; recorded if seen |
| The playthrough cannot see a back log of a 2 by 2 from any stand | The finder prefers 1-log layers and refuses a tree with a hidden layer log |
| Seed 36 has no fit tree with 12 logs above a 1-log layer within `PT_RINGS` | Accept a wide tree (4 cuts); if none, raise `PT_RINGS` and record the distance |
| A placed log set into a hole with leaves on it is felled | The rule's edge, written in the README |
| Q5: every leaf above the layer goes even if the stump holds it? | No: the leaf rule decides |
| Q6: a branch cut drops only that branch? | Yes, so nothing floats |

## Commits

Appended as they land:

- `fb28c504` Fell a tree only when a whole layer of its trunk is cut (the feller, its tests and README)
- `02f4460c` WIP: run_playthrough cuts the found tree's whole trunk layer, nearest log first, each aimed from the stand
- `050f2c71` Make run_playthrough fell seed 36's tree layer by layer (after phase 01's roots)
- this commit: `run_item_equipment`'s trees rooted in the floor; the full suite recorded

## Issues found

- **The struct.** `player_felling_t` is 30384 bytes (the plan guessed about 25 KiB): beside `found`, `found_seen` and `found_flags` it has `found_queue` (2 KiB, the floods' own list), `found_column` (1 KiB, each log's trunk column), the trunk columns (`trunk_x`, `trunk_y`, `trunk_in`, at most `PLAYER_FELLING_TRUNK_COLUMNS` = 64, more being a build set into the ground and so unsure), the four counts, and `last_layer` / `last_stump` for a report. The world arena's budget in `run/src/game/index.c` now adds `sizeof(player_felling_t)` (it was inside the 8 KiB of slack at 5 KiB; at 30 KiB it is not). That is the one line outside the owned paths.
- **The queue is all or nothing.** The plan kept "past the limit, the rest stay" for the queue. That would leave half a tree floating, against "nothing floats", so when what falls does not fit in the queue's room nothing of that cut falls, and `turned_away_count` counts the logs. The find (1024) and the queue (256) both stay far over the biggest tree (107 logs with roots).
- **Return value.** 0 when the find says no tree or is unsure (no change to the queue, as before for a log wall), 1 when the tree stands (the cut is queued so its leaves are looked at), 1 + the falling logs otherwise. `a_second_tree_cut_while_the_first_comes_down_goes_too` keeps its 0 and 11.
- **A branch at the layer keeps its stub.** Cutting a wide jungle's 4 trunk logs at a branch's `from + 1`: the branch's first log stays with the stump (as the plan says), and so do the branch's 1 or 2 logs above it, because only that first log joins them to anything; master 2.3 has what falls as "what a path of logs above h joins to the trunk's logs above h", and they are joined by no such path. Nothing floats. The plan's test table said "everything above the layer falls"; the test checks 2.3's words: no log left in the trunk's columns above, the branch log at the layer still there, and at most 2 logs per branch starting at that height left above.
- **The giant on falling ground** (`a_giant_cut_at_the_level_of_its_middle_leaves_only_what_is_below_that`): with the ground 2 lower beside it, the trunk's own column stands on a pillar of dirt with air on 2 sides, so its roots are not root logs and its column is no trunk column; its cut is a branch's, and the other 3 columns are the layer. The test cuts all four, the middle first; the expectation (nothing left from `PF_Z` up, the stump below) is unchanged.
- **Rewritten tests**, old expectation then new:
  - `every_kind_of_tree_comes_down_whole_with_a_drop_for_every_log`: one cut at the lowest log fells every log, `felled = logs - 1`; now every log of the first layer over the ground is cut, every log above drops (`felled = above`, drops `above + layer`), and the stump (2 roots per trunk column) stays.
  - `a_giant_cut_at_the_level_of_its_middle_...`: one cut; now all four (above).
  - `past_the_limit_the_rest_stay` (255 fell, the rest stood) became `past_the_finds_limit_nothing_falls` (1095 logs: only the cut, `find_overflow_count` 1) and `past_the_queues_limit_nothing_falls` (365 to fall: none, `turned_away_count` 365).
  - `a_column_not_loaded_stops_the_walk_and_nothing_goes_wrong` (the logs went, the leaf against the unloaded column stayed) became `a_column_not_loaded_fells_nothing` (only the cut, every log and both leaves stay, `find_unloaded_count` 1).
  - `wherever_a_tree_is_cut_no_wood_is_left_floating`: now cuts the trunk's whole layer at each height, or one branch log where the trunk has ended: 111 cuts, none left wood floating.
  - `no_drop_is_lost_on_the_biggest_tree_...`: cuts the 4 of the layer; the drops are the layer and every log above.
- **Ground in the tests.** Every test stands on 6 blocks of dirt under `PF_Z` over every loaded column (a disc, not the square), and `pf_trunk` from `PF_Z` draws 2 logs into it. `pf_paint` paints on phase 01's dirt, so its painter draws the roots. Before phase 01 landed a test helper drew them; it went in the rebase onto `58a8411e`, which keeps phase 01's ground and adds the layer helpers. There is one root-log rule in code, `felling_is_root` in `run/src/player/felling/index.c` (phase 01 added none; the tests' `pf_painted_root` and the playthrough's `pt_root_log` read pictures by the same words, to check the feller rather than share it).
- **The playthrough** (debug, after the rebase onto phase 01): 6 of 6, 252 checks. Seed 36's tree is phase 01's mangrove, 14 logs over the ground, cut point (80, 145, 94), 155 blocks from the start, stood at (78, 144); its layer there is **4 logs** (the trunk and 3 prop roots), 10 above. The report: the player mined 15 blocks (mud, 4 by hand, 10 felled), `the tree feller felled 10`, `the last tree cut had 0 logs left in its trunk's layer, and 8 logs of it stayed joined to a root` (2 roots in each of the 4 columns). The pictures, looked at: `before.png` the trunk close up; `notch.png` (new) the first log of the layer gone, the trunk standing over the gap (Ahmed's `10.png`); `cut.png` after the fourth; `later.png` open sky where the mangrove stood; `stump.png` (new) four ringed log tops flush with the ground in a cross, drops on them, as in `11.png`. Changes to the finder from the plan, each forced by a run:
  - **What counts is the layer and above, not above alone.** The layer's logs are cut by hand and drop too, so the finder needs `layer + above >= PT_TREE_LOGS_MIN` (phase 01's "logs at and over the cut point"). With "above >= 12" no tree within 1150 blocks fit: seed 36's land near the start has acacias of 4 to 7 logs and that mangrove, and thin trees never have 12 over a 1-log layer.
  - **One trunk only.** A clump (two trees grown into one) is refused: an acacia pair 1075 blocks off had "12 above" but felled 8, the other trunk keeping its own.
  - **Aim low on the near side.** A back log's middle is hidden by the trunk over the gap the front cut leaves, which stands until the last cut, so the mangrove was refused as unseen. The scripts now aim `PT_AIM_LOW` (0.2) over the log's floor, `PT_AIM_IN` (0.4) in from its middle toward the stand, under the standing trunk; the order is nearest first where every log is seen, else the first order found that is.
  - **The walk in starts at once** (4 ticks after the last cut, not 40): with 4 hand cuts, their drops merged in those 40 ticks and the walk picked up a mangrove propagule first, into hotbar slot 2 where the scripts look for logs. Walking in at once takes the layer's logs off the stump first; the felled logs land round the trunk while it stands there.
  - `pt_tree_t` gained no `cut_z`: it is `ground_z + 1`, the cut point's height.
- **`run_item_equipment`'s real-game trees** were logs stood on the floor, with no root, so the new feller took them for no tree (`in_a_real_game_an_axe_fells_a_tree_taller_than_its_uses_...` and `..._an_axe_that_breaks_on_the_cut_...` failed: nothing felled). `ie_tree` now draws the trunk 2 logs into the floor, as a generated tree's; every expectation stands as it was (the stump is under `ie_logs_in`'s count). The one edit outside the owned paths besides the arena line, the test's own fixture.
- **The whole debug suite, -j 2**, after the rebase onto phase 01 (`58a8411e`): 176 of 178 at first; `run_item_equipment` (above, fixed, then passing alone) and `run_climate_warning`'s 20 ms (passing alone, the load flake the master notes). Release: built whole, `run_player_felling` 37 of 37 (`the_find_takes_under_a_millisecond` worst 41 µs); the release build then deleted. `pgrep -af "$PWD/build"` prints nothing.
- **The find's time, release** (`-O3`, CPU only, 2026-11-14): worst 40 µs (jungle, 101 logs, mean 26 µs); every kind under 1 ms.
- **The find's time, debug** (`-O0`, CPU only, 2026-11-14), 100 cuts at the first log over the ground of the biggest tree of each kind: oak 8 logs found 12 µs mean / 15 µs worst, birch 9: 15 / 58, spruce 12: 19 / 62, jungle 101: 170 / 257, acacia 9: 15 / 57, dark oak 44: 73 / 87, mangrove 51: 85 / 99, cherry 14: 22 / 25. Release to come.
