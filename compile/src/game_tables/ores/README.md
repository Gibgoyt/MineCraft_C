# compile/src/game_tables/ores

**Purpose:** reads `config/ores/<ore>/index.yaml`, checks each file, the blocks and the item it names, and generates the ore table `run/src/terrain/ore/` lays veins from.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/ores/index.h`):

- `game_tables_ores_build`: the whole job, from a config directory to `<out>/ores/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none. The fields are in `config/ores/README.md`.

- A field it does not know, at any level, and a `name` that is not its directory's.
- A block with no `config/blocks/<block>/index.yaml`, a `drops` with no `config/items/<item>/index.yaml`, and a `drops` that the block's or the deep block's own file does not list among its drops.
- A number outside what its field allows, and a `z_min` not below its `z_max`.
- A row's fields at the top of a file that has `veins`, and a `veins` that is not a list of mappings.
- Two rows with one name, since the name is the row's stream, and two rows with one `order`.

## The table

`tables/ores/index.h`, from the generated root:

| Name | What |
|---|---|
| `ore_rows[ORE_ROW_COUNT]` | `ore_row_def_t`, by `order`, lowest first: the order the rows are laid down, each replacing what the last left |
| `ore_row_def_t` | One row: its stream's name `"ore/<row>"`, veins tried per chunk in 256ths, `z_low` and `z_high`, the column height it needs, its two blocks, the vein's size and its `ore_spread_t` |
| `ORE_ANY_COLUMN` | `min_column_height` for a row with none |

`run/include/terrain/ore/index.h` names `ore_row_def_t` as `terrain_ore_row_t`. The rows keep their old names and order, so the move of the table into yaml (plan 005 phase 11) changed no world: the ore and terrain digests held.
