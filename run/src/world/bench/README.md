# run/src/world/bench

**Purpose:** which blocks are parts of a station more than one voxel big, and where its other parts are. Placing, two kinds of breaking and opening its screen must agree on it.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/bench/index.h`):

- `bench_is_half`: whether a block id is a part of such a station. The name is from when there was one, a bench of two halves.
- `bench_parts`: whether a voxel is a part, and if so every part's step from it by its facing, and what each part must be: its block, facing the same way. The first part's cell keeps what the station holds, a tanning rack's slots.
- `bench_half_other`: for a station of two parts, the step to the other part and what it must be.

**May be called by:** `run/src/player/interaction/`, `run/src/entity/explosion/`, `run/src/game/` (a screen opens at the first part).
**Depends on:** the generated `tables/blocks/`, and `types/block_state` for facings.

## Blocks that are one

A station is several blocks, each with the same facing (`facing: horizontal`, `run/include/types/block_state/`). The first part goes where the placer aimed, the second to their right, and for the smith table the other two beyond those, away from the placer. Its shapes are the table `BENCH_SHAPES` in `index.c`, in the placer's frame: a new station is a row there.

| Station | Parts, in order | Size |
|---|---|---|
| Woodworking bench | `woodworking_bench_west`, `woodworking_bench_east` | 2 by 1 by 1 |
| Stoneworking bench | `stoneworking_bench_left`, `stoneworking_bench_right` | 2 by 1 by 1 |
| Tanning rack | `tanning_rack_left`, `tanning_rack_right` | 2 by 1 by 1 |
| Leather workbench | `leather_workbench_left`, `leather_workbench_right` | 2 by 1 by 1 |
| Smith table | `smith_table_front_left`, `_front_right`, `_back_left`, `_back_right` | 2 by 2 by 1 |

Where the second part goes from the first, by the facing, which is the way the placer looked; "ahead", for the smith table's back two, is the facing itself:

| Facing | Right of the first | Ahead of it |
|---|---|---|
| North, as every bench before facings | `x + 1` | `y + 1` |
| East | `y - 1` | `x + 1` |
| South | `x - 1` | `y - 1` |
| West | `y + 1` | `x - 1` |

Each part's pictures turn with it on the card, so the front the player saw when placing it stays towards where they stood.

| What happens | Who sees to it |
|---|---|
| Placed: every cell must take it, the player must be in none, and a column not loaded refuses. The first part goes where aimed, the rest as above | `run/src/player/interaction/`, through `player_interaction_place_plan` |
| Mined at any part: all go, what stood on or hung from each goes too, and one station drops | `run/src/player/interaction/` |
| Blown up at any part: the same, and the station always drops, as it is several blocks and the luck of a blast is for one | `run/src/entity/explosion/` |
| The right button on any part: the station's screen, of the first part's cell | `ui_screen_kind_of_block` in `run/src/ui/`, and `run/src/game/` |

Nothing else takes a block that is solid and not replaceable: water, sand and sim do not. A part with its others missing can only come from a file written by hand or a column made anew, and it breaks as any block does, with one station dropped. **If another part's column is not loaded, the part aimed at or reached by a blast stays,** so that no part is left to be found later.

Each part's file says it drops the station, so that any can be the one that does, and the others go quietly with `world_edit_block_set`.
