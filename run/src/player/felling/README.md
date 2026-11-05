# run/src/player/felling

**Purpose:** the tree feller: an axe that cuts a log brings down the rest of the tree, so that no wood is left floating.
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_felling_t`.
**Owns:** nothing.
**Lifetime:** World. The struct is over 5 KiB and `app_t` is on main's stack, so `run/src/game/` takes it from its world arena, keeps only the pointer, and starts it with `player_felling_start`. `player_t` holds nothing of it: `app_t` has no byte to spare, its frame being exactly at the limit in the release build. It is not saved: a queue lasts about a second, and to quit in that second leaves the rest of that tree standing. The saved `player_interaction_t` does not hold it, so this changes no save.
**Threads:** the one holding the map, which is `sim` in a real run.
**Public functions** (`run/include/player/felling/index.h`):

- `player_felling_start`: nothing under way.
- `player_felling_cut`: a log has just been cut: walks the tree and queues it.
- `player_felling_tick`: one tick of logs going.
- `player_felling_pending`: logs waiting.

**May be called by:** `run/src/game/` for the start and the tick, which says how many uses the axe has left and reads `felled_count` and `last_block`, and `run/src/player/interaction/` for the cut.
**Depends on:** `player/interaction` for the hands' ring, their counts and what stood on a log, `world/edit`, `world/map`, `world/tick` for the leaves, and the generated `tables/blocks`.

## What counts as the tree

From the log just cut, the walk goes to every touching log, including at a corner, because acacia and cherry branch sideways and jungle and dark oak trunks are 2 wide.

| Rule | Why |
|---|---|
| The same wood only | Oak breaks oak |
| At or above the cut only | A cut part way up leaves the stump on the ground, and nothing floats |
| One of them must touch leaves, by a face | A log wall has none, and stays. It is what protects builds, and it is not perfect: a log wall with a tree growing against it can still be taken |
| An axe, and not sneaking | `run/src/player/interaction/` looks at both. Sneaking chops one log |
| At most `PLAYER_FELLING_LOGS_MAX` waiting | ctest `run_player_felling` measures the biggest tree the generator draws, and holds this at more than twice it. Past it, the rest stay |
| A column not loaded stops the walk | So a very wide tree at the edge of the world can leave some behind |

The queue is the walk's own list, so a log's place in it is its distance from the cut, and they go nearest first. A second cut while a tree is coming down queues behind what is there, and its walk is undone by itself if it finds no leaves.

## Why it is not all in one tick

The hands' ring holds 32 drops, and a full ring loses the oldest. A giant jungle tree has more logs than that. So at most `PLAYER_FELLING_PER_TICK`, 8, go in a tick, which at 20 ticks a second is 160 a second and reads as the tree coming down fast. If the ring has no room for a log's rows and one over, the queue holds back, so that nothing is lost whoever is slow to take them. Something standing on a log goes with it, as with a hand, by `player_interaction_stack_break`: only a stack of plants on a log, more than the ring holds, could push a drop out, and `drops_lost_count` would say so.

## What it costs

`mined_count` counts each log, so that it tires the body as chopping does, and `felled_count` those past the cut. Each log is one use, `uses_left` being what the axe has in it: when it runs out the queue is dropped, and the rest stay up. A wooden axe has 59. Changing to anything but an axe in that second does the same, since no use could be charged. `run/src/game/` does the wearing: it is the one that knows the inventory.

## The leaves

When the queue ends, however it ends, the box of what went is handed to `world_tick_leaves_check`, which is what makes them go over about a second and a half through the rule of a random tick, with its drops, instead of waiting for random ticks to find them.
