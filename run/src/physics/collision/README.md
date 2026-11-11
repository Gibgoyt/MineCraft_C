# run/src/physics/collision

**Purpose:** swept AABB collision against the blocks of the world: what a block collides with, the boxes near a move, and how far a box gets before something stops it.
**Tier:** Arena. `collision_gather` allocates from the arena `physics` gives it. The rest allocates nothing.
**Owns:** nothing.
**Lifetime:** What it returns lasts until its Owner resets the arena, which is the next move.
**Threads:** Whichever holds the map, normally `sim`.
**Public functions** (`run/include/physics/collision/index.h`):

- `collision_cell_read`, `collision_cell_read_voxel`: a cell's block id, or the block with its state (`run/include/types/block_state/`). Below the world is bedrock, above it air, and false if its column is not loaded.
- `collision_block_boxes`: a block's boxes, from `config/blocks/`, turned by the voxel's facing. Given an id alone, it faces north.
- `collision_part_box`: a box in 32nds of a block, as `cuda/include/traverse/`'s table holds them, turned by a facing exactly as the card turns it, in world coordinates. What collision and the raycast both build boxes with.
- `collision_gather`: every box a box could meet, with a column that is not loaded counting as a wall.
- `collision_sweep`: the sweep itself, on any list of boxes.
- `collision_overlaps_any`: whether a box overlaps any of a list.

**May be called by:** `run/src/physics/` for what takes an arena. `collision_cell_read` and the box types by anything that moves, through `physics`'s header.
**Depends on:** `arena`, `assert`, `world/map`, `world/column`, the generated `tables/blocks`, libm.

## How a sweep goes

Each round finds the earliest moment any box's faces meet on every axis at once: the largest of the three entry times, as long as it comes before the smallest exit time. The box goes there, is set exactly on that face, stops on that axis, and the next round goes on with what is left on the others. So at most three stops, and a move can never pass through anything, because time and not position is what is searched.

- **Touching is not overlapping.** Anything within `COLLISION_EPSILON` counts as touching, so a box resting on a floor or sliding along a wall is free to move along it and leave it.
- **Already inside does not stop it.** A box the moving one overlaps at the start is ignored, so that a player a block was placed on can walk out.
- **The floor wins a tie on one box.** Walking across a seam at the same instant as coming down on it would otherwise catch on the side of the next block, which is flush with the floor. Between two boxes a tie needs no rule: whichever stops it first, the other stops it next, at the same moment.
- **The face is exact.** The result is the box's new corner, set from the face and not added up, so that no error builds up over ticks.

## Shapes

| In `config/blocks/` | Boxes |
|---|---|
| Not `solid`: air, fluids, plants, torches on a floor or a wall, ladders, vines, lily pads | None |
| `shape: slab` | The lower half |
| `shape: door`, shut | Its plate, three sixteenths thick, against the side it faces: +y facing north (plan 005 phase 2) |
| `shape: door`, open | None, so it can be walked through |
| `shape: stairs` | The lower half, and the upper half of the side they face: north as the table has them, turned by the voxel's facing as the card turns what it draws |
| Anything else solid | The whole cell |
