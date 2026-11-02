# compile/src/game_tables/recipes

**Purpose:** reads `config/recipes/<recipe>/index.yaml`, checks each file and what the files say about one another, and generates the recipe tables.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/recipes/index.h`):

- `game_tables_recipes_build`: the whole job, from a config directory to `<out>/recipes/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none. The fields are in `config/recipes/README.md`.

- A field the file's `kind` does not have, an unknown `kind`, and a `name` that is not its directory's.
- A name that is no item and no group, a `gives` or `leaves` that is not an item, and a group inside a group.
- A group not named `group_*`, and a `group_*` that is not a group.
- A pattern with more than 3 rows or columns, rows of different lengths, or an empty row or column at its edge: a grid is matched by what it holds, so those would mean nothing.
- A letter in the pattern that is not in `keys`, and a key that is not in the pattern.
- A `count` over what one slot of the item holds, which it reads from the item's own file.
- **Two recipes one grid could match:** the same shape with a choice in common in every cell, straight or mirrored, or for a shapeless one any way of laying the same things out. Otherwise what comes out would depend on which one is first.
- An item that smelts two ways or burns two ways.
- A directory of recipes with no crafting recipe or no smelting one, since a table of no rows does not compile.

A tree with no `recipes` directory at all writes nothing and is no problem. That is for the small trees of `tests/compile/game_tables/`; without the real directory, the real build fails on the outputs it wants.

## The tables

`tables/recipes/index.h`, from the generated root:

| Name | What |
|---|---|
| `recipe_defs[RECIPE_COUNT]` | Every `shaped` and `shapeless` file, sorted by name |
| `recipe_choices`, `recipe_choice_items` | What a cell may hold. A cell in `recipe_def_t.cells` is a row of `recipe_choices` plus one, and `RECIPE_CELL_EMPTY` is 0 |
| `recipe_smelts[RECIPE_SMELT_COUNT]`, `recipe_smelt_rows[ITEM_COUNT]` | What a furnace makes; by input item, the row plus one, or `RECIPE_NO_SMELT` |
| `recipe_fuels[ITEM_COUNT]` | By item: how long it burns and what it leaves, or all zero |

- **A shaped recipe's cells are `width` by `height`,** by row, in the first `width * height` of `cells`.
- **A shapeless one's are the first `cell_count`.**
- `recipe_fuels` and `recipe_smelt_rows` are direct by item id, so a furnace's tick looks nothing up.
