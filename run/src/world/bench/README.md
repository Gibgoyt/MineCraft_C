# run/src/world/bench

**Purpose:** which block is half of a bench two voxels long, and where its other half is. Placing and two kinds of breaking must agree on it.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/bench/index.h`):

- `bench_is_half`: whether a block id is either half.
- `bench_half_other`: whether a voxel is half a bench, the step to its other half by its facing, and what that half must be: the other block, facing the same way.

**May be called by:** `run/src/player/interaction/`, `run/src/entity/explosion/`.
**Depends on:** the generated `tables/blocks/`, and `types/block_state` for facings.

## Two blocks that are one

A bench is two blocks, `woodworking_bench_west`, the first half, and `woodworking_bench_east`, the second, each with the same facing (`facing: horizontal`, `run/include/types/block_state/`). It lies across the view of whoever placed it: the first half where they aimed, and the second to their right.

| Facing | Second half from the first | Lies along |
|---|---|---|
| North, as every bench before facings | `x + 1` | x |
| East | `y - 1` | y |
| South | `x - 1` | x |
| West | `y + 1` | y |

Each half's pictures turn with it on the card, so the front the player saw when placing it stays towards where they stood.

| What happens | Who sees to it |
|---|---|
| Placed: both cells must take it, the player must be in neither, and a column not loaded refuses. The first half goes where aimed, the second as the table above says | `run/src/player/interaction/`, through `player_interaction_place_plan` |
| Mined at either half: both go, what stood on or hung from each goes too, and one bench drops | `run/src/player/interaction/` |
| Blown up at either half: the same, and the bench always drops, as it is two blocks and the luck of a blast is for one | `run/src/entity/explosion/` |
| The right button on either half: the bench's screen | `ui_screen_kind_of_block` in `run/src/ui/` |

Nothing else takes a block that is solid and not replaceable: water, sand and sim do not. A half with no other half beside it can only come from a file written by hand or a column made anew, and it breaks as any block does, with one bench dropped. **If the other half's column is not loaded, the half aimed at or reached by a blast stays,** so that no half is left to be found later.

Each half's file says it drops a bench, so that either can be the one that does, and the other goes quietly with `world_edit_block_set`.
