# run/src/player/felling

**Purpose:** the tree feller: when the last log of a layer of a tree's trunk is cut, everything above it comes down, with any tool or bare hands, leaving the stump in the ground and no wood floating. It is part of the game, always on, with no setting and no exception (Ahmed, plan 005 phase 1).
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_felling_t`.
**Owns:** nothing.
**Lifetime:** World. The struct is about 30 KiB, most of it the find's scratch, and `app_t` is on main's stack, so `run/src/game/` takes it from its world arena, keeps only the pointer, and starts it with `player_felling_start`. `player_t` holds nothing of it: `app_t` has no byte to spare, its frame being exactly at the limit in the release build. It is not saved: a queue lasts about a second, and to quit in that second leaves the rest of that tree standing. The saved `player_interaction_t` does not hold it, so this changes no save.
**Threads:** the one holding the map, which is `sim` in a real run.
**Public functions** (`run/include/player/felling/index.h`):

- `player_felling_start`: nothing under way.
- `player_felling_cut`: a log has just been cut: finds the tree, and queues what falls.
- `player_felling_tick`: one tick of logs going.
- `player_felling_pending`: logs waiting.

**May be called by:** `run/src/game/` for the start and the tick, which reads `felled_count` and `last_block` for the sound and the report, and `run/src/player/interaction/` for the cut.
**Depends on:** `player/interaction` for the hands' ring, their counts and what stood on a log, `world/edit`, `world/map`, `world/tick` for the leaves, `world/wall_torch`, as each of a log's sides may have a torch to drop, and the generated `tables/blocks`.

## What counts as the tree

Ahmed (plan 010): "I should never just cut on block and the entire tree falls. I have to cut a layer ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground". So a cut does three things, all on the sim, with no heap.

**1. The find.** From the log just cut, a walk goes to every touching log of the same wood, up and down, including at a corner (acacia and cherry branch sideways, and jungle and dark oak trunks are 2 wide), into `found`.

| Word | What it is here |
|---|---|
| Ground | A block that is solid and neither a log nor leaves |
| Root log | A log with ground under it, and ground or the same wood on at least 3 of its 4 sides: set in the ground |
| The tree | What the find found, when one log of it touches leaves by a face and one is a root log |
| Trunk column | An (x, y) column of the tree holding a root log |
| The trunk | The trunk columns joined to the cut's column by a side or corner, through trunk columns: 1 for a thin tree, 4 for a wide one, a mangrove's trunk and its prop roots |
| Layer h | The tree's logs at the cut's height in the trunk's columns. A branch is never in a layer: its column holds no root log |
| The stump | What stays joined to a root log after the cut |

Every generated tree's trunk columns go 2 logs into the ground (plan 010 phase 01): the top one level with the ground's top, the deeper one a root log. A log a player puts on the ground has air beside it, and one put on another log has no ground under it, so **no build of logs on the ground is a tree**, leaves or not, and is never felled. Neither is a stump, which has no leaves.

**2. The layer.** If the cut is in a trunk column and no log of layer h is left, every log above h that a path of logs above h joins to the trunk's logs above h falls; the path never enters another trunk's columns. So the crown and every branch above the layer fall, even where a branch's first log, beside the trunk at h, would join the stump to the crown by a corner. That first log stays with the stump, and so does the rest of its branch if only that log joins it to anything (ctest `a_branch_beside_the_layer_does_not_hold_the_crown`). A cut that leaves a log in the layer takes that one log, and the tree stands: a 2 by 2 trunk comes down on its fourth cut of a layer, in any order.

**3. Nothing floats.** Whatever no longer joins a root log through logs that stay falls too. That alone is a branch cut: an acacia's lean takes its crown with it and the trunk stands, a cherry bough takes only itself.

| Rule | Why |
|---|---|
| The same wood only | Oak breaks oak |
| Leaves by a face, and a root log | A log wall, tower or house on the ground has no root log and stays. **The rule's edge:** a log a player sets into a hole in the ground, with leaves hung on its wood, is a tree |
| Any tool or none, sneaking or not | `run/src/player/interaction/` starts it for every log it cuts. Ahmed asked for it always on, so there is no setting |
| Unsure is nothing | A column not loaded next to any log found (`find_unloaded_count`), more than `PLAYER_FELLING_FIND_MAX` (1024) logs or more than `PLAYER_FELLING_TRUNK_COLUMNS` (64) trunk columns (`find_overflow_count`), or more to fall than the queue has room for (`turned_away_count`): only the cut goes. Better a tree left standing than a wrong one felled, or half of one left floating |
| At most `PLAYER_FELLING_LOGS_MAX` waiting | ctest `run_player_felling` measures the biggest tree the generator draws, and holds this at more than twice it |

`not_a_tree_count` and `stood_count` count the cuts that were no tree and the trees that stood; `last_layer` and `last_stump` say what the last cut left in its layer (`UINT32_MAX` for a branch) and how many logs stayed joined to a root. `player_felling_cut` returns 0 for no tree or an unsure find, 1 when the tree stands (the cut is queued, so the leaves round it are looked at), and otherwise 1 and the logs falling.

`found` is in walk order, which is distance from the cut, and what falls goes into the queue in that order, so the logs go nearest first. A second cut while a tree is coming down queues behind what is there. The find reads at most 1024 logs' 26 neighbours; ctest `the_find_takes_under_a_millisecond` times it on the biggest tree of every kind.

## Why it is not all in one tick

The hands' ring holds 32 drops, and a full ring loses the oldest. A giant jungle tree has more logs than that. So at most `PLAYER_FELLING_PER_TICK`, 8, go in a tick, which at 20 ticks a second is 160 a second and reads as the tree coming down fast. If the ring has no room for a log's rows and one over, the queue holds back, so that nothing is lost whoever is slow to take them. Something standing on a log goes with it, as with a hand, by `player_interaction_stack_break`: only a stack of plants on a log, more than the ring holds, could push a drop out, and `drops_lost_count` would say so.

## What it costs

`mined_count` counts each log, so that it tires the body as chopping does, and `felled_count` those past the cut. Only the log the hands cut wears what is in hand, one use as any block, which `run/src/game/` charges when the hands report it. What falls with it costs nothing (Ahmed: "my axe gets worn as if I have cut every block that fell down!!"), so a wooden axe, with 59 uses, brings down a tree of any size, and an axe that breaks on the cut still brings its tree down. What is in hand while it comes down makes no difference to what goes, only to what drops, by the same rule as a hand.

## The leaves

When the queue ends, however it ends, the box of what went is handed to `world_tick_leaves_check`, which is what makes them go over about a second and a half through the rule of a random tick, with its drops, instead of waiting for random ticks to find them.

Leaves that touch what stays are held by it, by the same rule. A cut through the first layer over the ground, or where the eye is, takes every leaf, as a stump that low touches none. A cut up in the crown leaves the leaves that reach what is left of the trunk, on it and joined to it, so they do not float. ctest `run_player_felling` measures both, in `wherever_a_tree_is_cut_no_wood_is_left_floating`.
