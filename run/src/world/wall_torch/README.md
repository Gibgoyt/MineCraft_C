# run/src/world/wall_torch

**Purpose:** which block a torch on a wall hangs from, and which torch leans which way. Three modules break these torches, and they must agree.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/wall_torch/index.h`):

- `wall_torch_out`: whether a block is a torch on a wall, and the way it leans out.
- `wall_torch_of`: the torch that leans a given way.
- `WALL_TORCH_BLOCKS`: all four.

**May be called by:** `run/src/world/tick/`, `run/src/player/interaction/`, `run/src/player/felling/`, `run/src/entity/explosion/`.
**Depends on:** the generated `tables/blocks/`.

## The four

A voxel holds a block number and nothing else, so a facing is four blocks: `torch_wall_east`, `torch_wall_west`, `torch_wall_north` and `torch_wall_south`, each its own shape in `config/blocks/`. Each is named for the way it leans. `torch_wall_east` is on the east side of a block, with that block to its west.

| What it needs | Who sees to it |
|---|---|
| A solid block behind it to go there | `run/src/player/interaction/` |
| That block mined, felled or blown up: it goes then and drops a torch | `run/src/player/interaction/`, `run/src/entity/explosion/` |
| That block gone by any other way: sand falling, leaves decaying | `run/src/world/tick/` |

A saved world keeps each by its name, so these ids can move like any other's. A torch hanging from a column that is not loaded stays, as the simulation takes what it cannot see for a wall.

## How they are drawn

The boxes are in `cuda/include/traverse/index.hu`: a stick that steps out from the wall, five boxes along the axes, as that table holds nothing else. Their faces are `blocks/torch_lean_side` and `blocks/torch_lean_cap`, ours by recipe, whose colour on a side goes by height alone. `blocks/torch_wall` from the pack is a picture of a leaning torch, drawn for a mesh Minetest has and we do not. A texture here is placed by the voxel, so the picture would be right from one side of the stick and back to front from the other. See issue 1 of `plans/003-ahmed-bohemoth-initial-game-fixes-phase-02-wall-torches.md`.
