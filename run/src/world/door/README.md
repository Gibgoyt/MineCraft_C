# run/src/world/door

**Purpose:** which block is a door, where its other half is, which doors the hand opens, and which way a door faces for its hinge. Placing, opening, mining and blasts must agree on it.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/door/index.h`):

- `door_is`: whether a block id is a door, `shape: door` in its file.
- `door_opens_by_hand`: whether the right button opens and shuts it. Every door but the iron one.
- `door_half_other`: whether a voxel is half a door, the step up or down to its other half, and what that half must be.
- `door_hinged_facing`: the facing a door goes down with, from the player's and what is beside it.

**May be called by:** `run/src/player/interaction/`, `run/src/entity/explosion/`, `run/src/mob/path/`.
**Depends on:** the generated `tables/blocks/`, and `types/block_state` for the bits.

## One block, two voxels

`wood_door`, `iron_door` and `glass_door` (plan 005 phase 2) are each one block, with `facing: horizontal`, `half: upper_lower` and `open: true` in `config/blocks/`. Both halves are the same block with the same facing and the same open bit; the upper half has `BLOCK_STATE_UPPER` (`run/include/types/block_state/`).

| State | Shape (drawn, aimed at, collided with) |
|---|---|
| Shut, facing north | A plate three sixteenths thick against the `+y` side, its hinge at the west end |
| Open, facing north | The same plate turned a quarter anticlockwise about the hinge: against the `-x` side. `block_state_drawn_facing` |

**The hinge is always at the door's left end as it faces,** because a voxel has no bit left for it. A door hinged on the right is a door facing the other way: placed looking north with its hinge to the east, it faces south, with its plate on the near side of its cell instead of the far side, three sixteenths of a block from where Minecraft's would be.

**Pictures:** the lower half shows its `side` picture on every face. The upper half shows its `top` and `bottom` pictures on its two wide faces, and `side` on its edges: `block_state_picture_face`. So a door's file names its upper picture as `top` and `bottom`, and its lower as `side`.

## What happens

| What | Who sees to it |
|---|---|
| Placed: the lower half where aimed, the upper one above. Both cells must take it, the body must not meet either plate, and a column not loaded refuses. Facing the way the player looks, turned by R, then turned round for a hinge on the right | `player_interaction_place_plan` in `run/src/player/interaction/`, whose plan holds both cells, so a preview draws the whole door |
| The hinge side: another door beside the lower half, at the left or the right, puts this one's hinge on the other side, so two make a pair. Otherwise the side with more wall (solid blocks beside the lower and upper halves) has the hinge, and with neither, the left | `door_hinged_facing`, with the interaction reading the neighbours |
| The right button on either half, on a new press: both halves open or shut together. Holding the button does it once | `player_interaction_tick` |
| Mined at either half: both go, and one door drops | `run/src/player/interaction/` |
| Blown up at either half: both go, and the door always drops, as for the bench | `run/src/entity/explosion/` |
| Bodies: a shut door blocks, an open one does not | `run/src/physics/collision/` |
| Mobs' paths: a shut door is a wall, which no body goes through or stands on; an open one is open | `run/src/mob/path/` |

**An iron door stays shut by hand.** In Minecraft only redstone opens one, and there is no redstone here yet, so the right button does nothing to it, and is free to place a block against it. It can still be mined with a pickaxe and placed. When redstone comes, it opens by `block_state_with_open` on both halves, as a hand opens a wooden one.

Not done: a door does not break when the block under it goes, and it may be placed with nothing under it. A half with no other half beside it can only come from a file written by hand; it is mined as any block, dropping one door.
