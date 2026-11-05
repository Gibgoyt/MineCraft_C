# run/src/player/interaction

**Purpose:** mining, what a broken block drops, and placing: what the two mouse buttons do to the world.
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_interaction_t`.
**Owns:** nothing.
**Lifetime:** Process for the struct, inside `player_t`. `player_interaction_start` starts it for a world.
**Threads:** the one holding the map, which is `sim` in a real run.
**Public functions** (`run/include/player/interaction/index.h`):

- `player_interaction_start`, `player_interaction_hold_tool`, `player_interaction_hold_speed`: empty hands and a seed; what it mines with from now on, and what it mines at if its tier does not say.
- `player_interaction_harvests`, `player_interaction_speed_milli`, `player_interaction_ticks`: the rules below, for any block and tool.
- `player_interaction_tick`: one tick of both buttons.
- `player_interaction_progress`: how far along the block being mined is, for Stage 09.5's breaking overlay.
- `player_interaction_drops_spawn`, `player_interaction_drop_next`: rolls a block's table into the ring; takes the oldest out of it.
- `player_interaction_stack_break`: what stood on a block just broken, and what hung on its sides, goes with it, for whatever else breaks one: `run/src/player/felling/`.
- `player_interaction_hand_give`, `player_interaction_hand_set`, `player_interaction_hand_take`: the one slot placing takes from.

**May be called by:** `run/src/player/`. `run/src/game/` takes the drops from `player_interaction_drop_next` and sets the hand to the hotbar slot in hand with `player_interaction_hand_set`, or in a run with no inventory gives what is picked up to `player_interaction_hand_take`, and `player_interaction_hold_tool` and `player_interaction_hold_speed` with what `run/src/item/equipment/` says of that slot.
**Depends on:** `world/edit`, `world/map`, `world/wall_torch`, `world/bench`, `physics/collision`, `physics/raycast`, `player/camera` and `player/movement` for their types, `player/felling` for what a log cut with an axe sets going, and the generated `tables/blocks`.

## Mining

Everything comes from `config/blocks/<block>/index.yaml`. These are Minecraft's published rules, taken from its documentation and written here from scratch:

| What | Rule |
|---|---|
| Harvest | A block with no `tier` always gives its drops. One with a `tier` gives them only to its own `tool` of that tier or better. Anything else breaks it for nothing |
| Time | `hardness * 1.5` seconds if it can be harvested, `hardness * 5` if not, divided by the speed, so `hardness * 30` or `* 100` ticks over the speed. Rounded up, and at least one tick |
| Speed | 1 for a hand, or for any tool that is not the block's. On the block's own tool: wood 2, stone 4, iron 6, diamond 8; gold 12, by `player_interaction_hold_speed`, as it has no tier of its own; a sword 1.5; shears 2, and 15 on what decays |
| Penalties | 5 times as long with the feet off the ground, and 5 times again with the eyes under water |
| Never | `hardness: unbreakable` |

So dirt by hand is 15 ticks, stone by hand is 150 with nothing to show for it, and stone with a wooden pickaxe is 23.

- Work is counted in whole numbers, so that `0.6` being a little more than that in a `float32_t` cannot turn 18 ticks into 19.
- Letting go, aiming at something else, or the block changing starts the count again.
- After a break that took more than a tick, nothing is mined for 5 ticks. What breaks at a touch can be swept through, one a tick.
- A log (`log: true`) cut with an axe, not sneaking, sets going what `run/src/player/felling/` does with the rest of its tree, if `borrowed_felling` is given.
- A torch goes by the face aimed at: on a side it hangs from that block, one of the four `torch_wall_*`, on a top it stands, and on an underside it does not go at all. Aimed at the side of what is not solid, it stands on the floor if there is one. What is held and counted stays `torch`, whichever it became.
- What stands on the ground (`cross crop torch`) goes with what it stood on, up to 32 of them, and gives what it would to a hand. So does a torch hanging from one of the block's four sides. No other module does this: `world/tick` only takes crops off what is no longer farmland, with no drops.

## Drops

Shears on their own block roll nothing: `run/src/game/` brings out the block itself, as only it knows items. Otherwise each row of a block's table is rolled on its own, its chance and then its count, from splitmix64 with the state in the struct, so one run's buttons always give the same drops. They go in a ring of 32 as `player_drop_t`: an item number from `block_drop_items`, a count, and the middle of the cell. If no one takes them, the oldest are pushed out, and `drops_lost_count` says how many.

## Placing

The right button, when the left is not held, puts the hand's block on the face the ray came in through, or in the aimed-at block itself if that is `replaceable`, like tall grass. Held, that repeats every 4 ticks.

It does nothing, and costs nothing, when:

- the hand is empty, or the eye is inside what it aims at, so that no face was hit;
- the cell is not `replaceable`, is outside gameplay's heights, or is in a column not loaded;
- the block would overlap the body at its present height. Touching it is fine, so a block can go under the feet;
- it is not solid and the cell holds a fluid;
- it stands on the ground, and the cell below is not solid.

## The aim is a tick old, on purpose

`player_interact_tick` runs before `player_tick`, so it acts on the block the last tick aimed at: the one the player sees outlined. The body then moves in the world as the hands left it, so it cannot end a tick inside a block just placed, and the next aim and outline are of the block just uncovered. A block that is no longer what was aimed at is left alone for that tick.

## The hand is a mirror

Placing takes from one slot of up to 64 blocks, and knows nothing of an inventory. With `game_item_set`, as in `--walk`, `run/src/game/` sets it to the hotbar slot in hand before each tick, and takes off that slot what the tick placed. An item that is no block, like coal, leaves the hand empty, so nothing is placed.

With no inventory, as in tests of this module alone, what `run/src/entity/item_drop/` picks up from the ground goes through `player_interaction_hand_take` to that slot. An item that is no block, or another block than the slot holds, is not taken, so it stays on the ground and the slot loses nothing. `player_interaction_hand_give` is the slot itself, and it does replace a different block.

## What is left for others

- The crack is drawn by `cuda/src/entity_rays/`: `run/src/game/` turns `player_interaction_progress` into its frame with `render_entities_crack_frame`.
- Enchantments have no part here yet. Durability is `run/src/item/equipment/`'s, and what mining tires is `run/src/player/survival/`'s, from `mined_count`.
