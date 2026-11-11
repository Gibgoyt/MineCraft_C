# run/src/world/light/column

**Purpose:** the light of one column as if nothing stood beside it, worked out from a flat array of its states, so that a worker can do it while the column is made. `world/light` then takes it in on the sim thread, and works out only what the columns beside it add: plan 005 phase 14.
**Tier:** Borrow (depth 3): works only in the memory its caller gives: the states, the bytes it writes, and a scratch.
**Owns:** nothing.
**Lifetime:** one call.
**Threads:** any. It touches no section, no pool and no map, and keeps nothing between calls.
**Public functions** (`run/include/world/light/column/index.h`):

- `world_light_column_alone`: a column's light, alone.
- `world_light_into`, `world_light_def`, `world_light_is_opaque`, `world_light_keeps_own`: the rules, inline, shared with `world/light` so that both ways of working light out read them from one place.

**May be called by:** `run/src/world/light/` (the rules), and `game`'s column job (the column's light).
**Depends on:** `world/section`, `world/column`, `assert`, `types/block_state` and the generated `tables/blocks/`.

## What it works out

The rules are `world/light`'s (see its README): light is the least set of numbers that keeps them, so there is one answer. Here the column has nothing past its four sides, as `world/light` treats a column whose neighbours are not lit. With neighbours, light can only be more, so this is a lower bound of the column's light in the world, and exact everywhere nothing beside it reaches. `world_light_column_added_from` gives the column these bytes and then looks only at the voxels on either side of its four edges: past those, nothing is owed more than it holds. The answer is the same bytes `world_light_column_added` gives, which `run_world_light` holds against both it and a separate recompute.

## How

Per channel, on flat arrays: `out_packed` is the light itself, one `section_light_pack` byte per voxel, and the scratch is two lists through the voxels by level.

1. **Sky falls**, one section at a time from the top, as in `world/light`: a section of one block that costs nothing under open sky is 15 everywhere (**open**), and from the first section with no sky left, everything below is 0 (**dark**). Past the top of storage counts as 15 of sky for an opaque block's faces.
2. **Seeds.** A voxel the fall lit is a seed if it could give a neighbour beside or below more than the fall gave it; never above, which got at least as much from the fall. An open section is looked at only in its bottom layer, and a dark one not at all. The faces of an opaque block see what the fall gave beside them here too. For block light, the seeds are the blocks that give it off.
3. **Spread**, brightest first, as `world/light`'s bright step: an opaque neighbour's faces see the light and it goes no further; anything else that would get more gets it and goes in the list for its new level.

**Why one next per voxel is enough.** A voxel goes in the seed list at most once, before the spread. In the spread, what a voxel is offered is the level being spread less what its own opacity costs (at least 1), and levels are spread from 15 down, so the first offer that raises a voxel is the most it is ever offered: it goes in the raise list at most once. The one move that costs nothing, sky at 15 falling into a block that costs nothing, never raises a voxel, as the fall already gave every such voxel its 15. A count of voxels taken stops the program if that were ever wrong, rather than run round a broken list.

## Cost

It looks at each voxel of a section that is neither open nor dark, and at the voxels light reaches by spreading. Measured in plan 005 phase 14's Issues found.
