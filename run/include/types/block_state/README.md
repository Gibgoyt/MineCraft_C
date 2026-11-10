# run/include/types/block_state

**Purpose:** the one definition of what a voxel's `uint16_t` holds: a block id, and the block's state beside it (which way it faces, which half it is, whether it is open), with the arithmetic of turning a block, its boxes, its faces and the place a texture is read at. The world, the saves, the card and the table tool all mean the same bits by it.
**Tier:** Borrow in spirit: a header of types and arithmetic, with no memory of its own and no source file.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any.
**Public functions** (`run/include/types/block_state/index.h`), each `static inline`, and `__host__ __device__` under nvcc:

- `block_state_id`, `block_state_bits`, `block_state_facing`, `block_state_upper`, `block_state_open`: the parts of a voxel.
- `block_state_make`, `block_state_with_facing`, `block_state_with_upper`, `block_state_with_open`: a voxel from its parts, or with one changed.
- `block_facing_turn`: quarter turns clockwise, seen from above.
- `block_facing_step_x`, `block_facing_step_y`, `block_facing_of_step`: a facing as a step on the grid and back.
- `block_facing_of_look`: the nearest of the four to a look, which is how a player's view becomes a block's facing.
- `block_facing_face_to_world`, `block_facing_face_to_local`: a face of a turned block, in the table's order.
- `block_facing_box_turn`: a box inside a voxel, turned with it.
- `block_facing_point_to_local`: a point on a turned block, back to where it is on the block facing north, so that its pictures turn with it.

**May be called by:** everything, including `compile/` and `cuda/`. The generated `tables/blocks/index.h` includes it, so whatever has the block table has this.
**Depends on:** `types/index.h`.

## The bits

| Bits | What | Values |
|---|---|---|
| 0-11 | The block id, into `block_defs` | 0 to 4095: `compile/src/game_tables/blocks/` refuses a 4097th block |
| 12-13 | Facing, a `block_facing_t` | `NORTH` 0 (+y), `EAST` 1 (+x), `SOUTH` 2 (-y), `WEST` 3 (-x): quarter turns clockwise from north |
| 14 | `BLOCK_STATE_UPPER` | The upper half of a block two tall: a door |
| 15 | `BLOCK_STATE_OPEN` | A door or a gate that is open |

**A block holds only the bits its file declares** (`facing: horizontal`, `half: upper_lower`, `open: true` in `config/blocks/`), and the rest are 0. So a block with no state is its id, and so is every voxel of every world from before this. `run/include/world/block_state/` knows which bits each block may hold, and cleans what a save gives it.

**Facing 0 is north, the way every block faced before facings,** so a block placed before, or by the terrain, looks as it did.

## What a facing means

A block's facing is **the way the player looked when placing it**, the nearest of the four, turned by R a quarter at a time:

| Block | Facing north means | So placed looking |
|---|---|---|
| Stairs | The high step on the north side: they rise away from you | north, you walk up them north |
| Ladder, vine | The plate against the north side of its voxel: on the wall you look at | at a wall to the north |
| Torch on a wall | Its wall to the north, so it leans out south | at the south face of a block |
| Woodworking bench | Lying along x, the first half where you aim and the second to its east: across your view | north |
| A door (phase 2) | Its hinge side and swing are the door's to say; the bits are here | |

A shape in `cuda/include/traverse/` is drawn as it faces north, and turned about the middle of its voxel by `block_facing_box_turn`: one quarter takes `(x, y)` to `(y, 1 - x)`. `run/src/physics/` turns its boxes the same way, so a block collides and is aimed at as it is drawn. Its faces turn with it: `block_facing_face_to_local` says which of its own faces the world sees on a side, and `block_facing_point_to_local` where on that face's picture, so a bench's front stays its front.

## For later phases

- **Placing:** `block_facing_of_look(dx, dy)` of the view, then `block_facing_turn` by the player's quarter turns, then `block_state_placed(id, facing)` from `world/block_state`. `player_interaction_place_plan` in `run/src/player/interaction/` does exactly this and says what and where a hand would place, without placing it, for a preview.
- **A door:** declare `facing: horizontal`, `half: upper_lower` and `open: true`; the two voxels are `block_state_make(id, facing, false, open)` and `block_state_make(id, facing, true, open)`; opening is `block_state_with_open`, which is an edit like any other and is logged and drawn.
- **Reading:** `world_map_block_get` and `column_block_get` give the id alone, which is what nearly everything wants. `world_map_voxel_get` and `column_voxel_get` give it with its state. A set stores whatever it is given: an id alone is that block facing north.
