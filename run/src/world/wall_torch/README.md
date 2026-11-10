# run/src/world/wall_torch

**Purpose:** which block a torch on a wall hangs from, and which torch leans which way. Three modules break these torches, and they must agree.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/wall_torch/index.h`):

- `wall_torch_out`: whether a voxel is a torch on a wall, and the way it leans out, from its facing.
- `wall_torch_of`: the wall torch that leans a given way, as a voxel facing its wall.
- `wall_torch_hangs_from`: whether the torch in a voxel beside a block hangs from that block.
- `WALL_TORCH_SIDES`: the four sides it can hang from, one for each facing.

**May be called by:** `run/src/world/tick/`, `run/src/player/interaction/`, `run/src/player/felling/`, `run/src/entity/explosion/`.
**Depends on:** the generated `tables/blocks/`, and `types/block_state` for facings.

## One block that faces its wall

A torch on a wall is one block, `wall_torch`, with `facing: horizontal` (`run/include/types/block_state/`). **It faces its wall:** facing north, its wall is the block to its north, and it leans out to the south. Before plan 005 phase 12 a voxel held only a block number, so it was four blocks named for the way each leaned: `torch_wall_east` is now `wall_torch` facing west, `torch_wall_west` facing east, `torch_wall_north` facing south, and `torch_wall_south` facing north. A world saved with them reads each that way: `save_names_renamed_state` in `run/src/save/names/`.

| What it needs | Who sees to it |
|---|---|
| A solid block behind it to go there | `run/src/player/interaction/` |
| That block mined, felled or blown up: it goes then and drops a torch | `run/src/player/interaction/`, `run/src/entity/explosion/` |
| That block gone by any other way: sand falling, leaves decaying | `run/src/world/tick/`, which reads the voxel for its facing |

A torch hanging from a column that is not loaded stays, as the simulation takes what it cannot see for a wall. R does not turn a torch: its wall says which way it faces.

## How it is drawn

The boxes are in `cuda/include/traverse/index.hu`, as it faces north: a stick that steps out from the wall, five boxes along the axes, as that table holds nothing else. Its facing turns them about the middle of the voxel, on the card, in collision and in the raycast alike. Its faces are `blocks/torch_lean_side` and `blocks/torch_lean_cap`, ours by recipe, whose colour on a side goes by height alone, so a turned torch looks as each of the four did. `blocks/torch_wall` from the pack is a picture of a leaning torch, drawn for a mesh Minetest has and we do not. A texture here is placed by the voxel, so the picture would be right from one side of the stick and back to front from the other. See issue 1 of `plans/003-ahmed-bohemoth-initial-game-fixes-phase-02-wall-torches.md`.
