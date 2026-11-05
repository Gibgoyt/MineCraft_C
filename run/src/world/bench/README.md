# run/src/world/bench

**Purpose:** which block is half of a bench two voxels long, and where its other half is. Placing and two kinds of breaking must agree on it.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/bench/index.h`):

- `bench_half_other`: whether a block is half a bench, the way to its other half, and what that half is.

**May be called by:** `run/src/player/interaction/`, `run/src/entity/explosion/`.
**Depends on:** the generated `tables/blocks/`.

## Two blocks that are one

A voxel holds a block number and nothing else, so a bench is two blocks: `woodworking_bench_west` at the lower x, and `woodworking_bench_east` beside it. Always along x, as blocks have no facing.

| What happens | Who sees to it |
|---|---|
| Placed: both cells must take it, the player must be in neither, and a column not loaded refuses. West goes where aimed, east at `x + 1` | `run/src/player/interaction/` |
| Mined at either half: both go, what stood on or hung from each goes too, and one bench drops | `run/src/player/interaction/` |
| Blown up at either half: the same, and the bench always drops, as it is two blocks and the luck of a blast is for one | `run/src/entity/explosion/` |
| The right button on either half: the bench's screen | `ui_screen_kind_of_block` in `run/src/ui/` |

Nothing else takes a block that is solid and not replaceable: water, sand and sim do not. A half with no other half beside it can only come from a file written by hand or a column made anew, and it breaks as any block does, with one bench dropped. **If the other half's column is not loaded, the half aimed at or reached by a blast stays,** so that no half is left to be found later.

Each half's file says it drops a bench, so that either can be the one that does, and the other goes quietly with `world_edit_block_set`.
