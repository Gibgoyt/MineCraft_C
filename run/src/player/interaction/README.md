# run/src/player/interaction

**Purpose:** mining, what a broken block drops, and placing: what the two mouse buttons do to the world.
**Tier:** Arena. Takes nothing, as a tier may allocate less than it is allowed: all it needs is in `player_interaction_t`.
**Owns:** nothing.
**Lifetime:** Process for the struct, inside `player_t`. `player_interaction_start` starts it for a world.
**Threads:** the one holding the map, which is `sim` in a real run.
**Public functions** (`run/include/player/interaction/index.h`):

- `player_interaction_start`, `player_interaction_hold_tool`, `player_interaction_hold_speed`: empty hands and a seed; what it mines with from now on, and what it mines at if its tier does not say.
- `player_interaction_harvests`, `player_interaction_speed_milli`, `player_interaction_ticks`: the rules below, for any block and tool.
- `player_interaction_tick`: one tick of both buttons. The right button on a door that opens by hand opens or shuts it, once a press: see "Doors" below.
- `player_interaction_progress`: how far along the block being mined is, for Stage 09.5's breaking overlay.
- `player_sounds_post`, `player_sounds_next`: what can be heard, into a ring and out of it. The hands post a hit every 4 ticks of mining, a break and a place, if the input has somewhere to put them. Nothing in the world or the save depends on them, and they take no random number.
- `player_interaction_drops_spawn`, `player_interaction_drop_next`: rolls a block's table into the ring; takes the oldest out of it.
- `player_interaction_stack_break`: what stood on a block just broken, and what hung on its sides, goes with it, for whatever else breaks one: `run/src/player/felling/`.
- `player_interaction_hand_give`, `player_interaction_hand_set`, `player_interaction_hand_take`: the one slot placing takes from.
- `player_interaction_place_plan`: what the right button would put down and where, with its facing and whether it fits, without changing anything. Placing is this and then the edits; a preview (plan 005 phase 10) draws it. See "Placing" below.
- `player_interaction_turn`: R. What is in the hand turns a quarter clockwise before it is placed, until the hand holds something else.

**May be called by:** `run/src/player/`. `run/src/game/` takes the drops from `player_interaction_drop_next` and sets the hand to the hotbar slot in hand with `player_interaction_hand_set`, or in a run with no inventory gives what is picked up to `player_interaction_hand_take`, and `player_interaction_hold_tool` and `player_interaction_hold_speed` with what `run/src/item/equipment/` says of that slot.
**Depends on:** `world/edit`, `world/map`, `world/wall_torch`, `world/bench`, `world/door`, `world/block_state`, `physics/collision`, `physics/raycast`, `player/camera` and `player/movement` for their types, `player/felling` for what a log cut sets going, and the generated `tables/blocks`.

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
- A log (`log: true`) cut with any tool or none, sneaking or not, sets going what `run/src/player/felling/` does with the rest of its tree, if `borrowed_felling` is given.
- A torch goes by the face aimed at: on a side it hangs from that block, a `wall_torch` facing it, on a top it stands, and on an underside it does not go at all. Aimed at the side of what is not solid, it stands on the floor if there is one. What is held and counted stays `torch`, whichever it became.
- What stands on the ground (`cross crop torch`) goes with what it stood on, up to 32 of them, and gives what it would to a hand. So does a torch hanging from one of the block's four sides. No other module does this: `world/tick` only takes crops off what is no longer farmland, with no drops.

## Drops

Shears on their own block roll nothing: `run/src/game/` brings out the block itself, as only it knows items. Otherwise each row of a block's table is rolled on its own, its chance and then its count, from splitmix64 with the state in the struct, so one run's buttons always give the same drops. They go in a ring of 32 as `player_drop_t`: an item number from `block_drop_items`, a count, and the middle of the cell. If no one takes them, the oldest are pushed out, and `drops_lost_count` says how many.

## Placing

The right button, when the left is not held, puts the hand's block on the face the ray came in through, or in the aimed-at block itself if that is `replaceable`, like tall grass. Held, that repeats every 4 ticks.

**What and where** is `player_interaction_place_plan`, which changes nothing and is the whole of the decision: `player_interaction_tick` places exactly what it says when it says `PLAYER_PLACE_OK`. It gives the cells (two for a bench, side by side; two for a door, the lower half where aimed and the upper above it), the voxel for each with its state, the facing, what the first cell held, and a status: `NONE` when there is nothing to show (an empty hand, no face, a torch aimed at an underside), `OK`, or `BLOCKED` with the cells it would have taken. A preview draws `OK` in blue and `BLOCKED` in red: plan 005 phase 10's, through `player_place_preview` in `run/src/player/`, which builds the input from the same function as the tick, then `run/src/game/` and `run/src/render/entities/` to `cuda/src/entity_rays/`. It draws whatever cells the plan gives, up to `ENTITY_RAYS_PREVIEW_CELLS` (4), so a door's two cells or a cell ahead for bridging need nothing more there.

**Which way it faces,** for a block whose file says `facing: horizontal` (stairs, ladders, vines, the woodworking bench, and the stoneworking bench and smith table when they come): the way the player looks, the nearest of the four, in `player_interaction_input_t.look_facing`, which `run/src/player/` works out from the yaw main last sent. Then R's quarter turns, clockwise seen from above. So stairs rise away from you, and a bench lies across your view, its first half where you aim and its second to your right. A ladder or a vine put on the side of a block faces that side, so that it lies on the wall aimed at, and R turns it from there. A torch on a wall faces its wall, by the face aimed at, and R does nothing to it. `run/include/types/block_state/README.md` says what each facing means.

**R** is `player_turn_send` on main, a press in `hands_latch` taken by the next tick, and `player_interaction_turn` there. The turns are in `player_interaction_t.turns`, saved with the rest of it, and go back to 0 when `player_interaction_hand_give` or `player_interaction_hand_set` is given a different block: changing what is held. A hand that runs out and is filled with the same block keeps them.

It does nothing, and costs nothing, when:

- the hand is empty, or the eye is inside what it aims at, so that no face was hit;
- the cell is not `replaceable`, is outside gameplay's heights, or is in a column not loaded;
- the block would overlap the body at its present height. Touching it is fine, so a block can go under the feet;
- it is not solid and the cell holds a fluid;
- it stands on the ground, and the cell below is not solid.

## Doors

Plan 005 phase 2; `run/src/world/door/README.md` has the whole of it.

- **Placing** goes through `player_interaction_place_plan` like anything else, with a door's two halves as its two cells (`count` 2, `cells[1]` straight above `cells[0]`, `voxels[1]` the upper half), so a preview draws the whole door, and both cells must take it or neither does. The facing is the player's turned by R, then turned round if the hinge goes on the right, by `door_hinged_facing` from what is beside the lower cell. That is the only change to the plan for doors.
- **Opening:** the right button on either half of a door that opens by hand opens or shuts both halves, as one edit each, with a place sound. Only a new press does it: `player_interaction_t.place_held` remembers that the button was down last tick (in what was padding, so an old state file reads 0), so holding the button after placing a door, or after opening it, does nothing more. The hand never places on such a door. An iron door does not open by hand, and the button places against it as against any block.
- **Mining** either half takes both, through the same path as a bench's two halves, and the half mined drops the one door.

## The aim is a tick old, on purpose

`player_interact_tick` runs before `player_tick`, so it acts on the block the last tick aimed at: the one the player sees outlined. The body then moves in the world as the hands left it, so it cannot end a tick inside a block just placed, and the next aim and outline are of the block just uncovered. A block that is no longer what was aimed at is left alone for that tick.

## The hand is a mirror

Placing takes from one slot of up to 64 blocks, and knows nothing of an inventory. With `game_item_set`, as in `--walk`, `run/src/game/` sets it to the hotbar slot in hand before each tick, and takes off that slot what the tick placed. An item that is no block, like coal, leaves the hand empty, so nothing is placed.

With no inventory, as in tests of this module alone, what `run/src/entity/item_drop/` picks up from the ground goes through `player_interaction_hand_take` to that slot. An item that is no block, or another block than the slot holds, is not taken, so it stays on the ground and the slot loses nothing. `player_interaction_hand_give` is the slot itself, and it does replace a different block.

## What is left for others

- The crack is drawn by `cuda/src/entity_rays/`: `run/src/game/` turns `player_interaction_progress` into its frame with `render_entities_crack_frame`.
- Enchantments have no part here yet. Durability is `run/src/item/equipment/`'s, and what mining tires is `run/src/player/survival/`'s, from `mined_count`.
