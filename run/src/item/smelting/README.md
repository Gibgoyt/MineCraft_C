# run/src/item/smelting

**Purpose:** furnaces: three slots at a cell, what they burn and what they make, one tick at a time. A furnace has a process (plan 005 phase 6), from its block: the furnace, the blast furnace or the tanning rack, and does only that process's smelts. The rack burns nothing: a hide tans by time alone, and its fuel slot takes nothing.
**Tier:** Arena. `smelting_start` takes its array from the arena `run/src/item/` gives it, once per world. A tick allocates nothing.
**Owns:** nothing. `smelting_store_t` is a field of `item_t`, and `borrowed_furnaces` is in its world arena.
**Lifetime:** World.
**Threads:** the sim's alone.
**Public functions** (`run/include/item/smelting/index.h`):

- `smelting_start`: an empty store.
- `smelting_at`, `smelting_open`, `smelting_open_process`, `smelting_remove`: the furnace at a cell; the same making one, a furnace or of a process; ending one and giving back what it held.
- `smelting_burns`: whether its process burns fuel.
- `smelting_tick`: one tick of one furnace.
- `smelting_is_lit`, `smelting_cook_fraction`, `smelting_burn_fraction`: what a block and a screen show.
- `smelting_recipe_of`, `smelting_burn_ticks`: what an item is to a furnace.
- `smelting_click`, `smelting_quick_move_in`, `smelting_quick_move_out`: what a screen does.

**May be called by:** `run/src/item/`, and the screens of Stage 14.3.
**Depends on:** `arena`, `item/stack`, `item/inventory`, the generated `tables/recipes/`.

## What a tick does

The numbers are Minecraft's published ones and live in `config/recipes/`, at 20 ticks a second.

| What | How |
|---|---|
| Fuel | Goes on only when something can cook now. It burns out even with nothing left to do: a burning piece is never put back |
| Cooking | 200 ticks by default, and only while burning, with an input and room for the whole output |
| One piece of coal | 1600 ticks, so exactly 8 items of 200 |
| Out of fuel | Cooks backward at 2 a tick, never below 0 |
| Burning, with nothing it can do | What had been cooked is gone at once |
| Another item in the input | Starts from nothing |
| A lava bucket | Leaves a bucket in the fuel slot, which itself does not burn, and so the slot stops there |
| The fuel slot | Takes only what burns, through `smelting_click`; a shift-click sends there only what does not smelt, so a log goes to the input |
| The output | Only taken from, and all at once, so a screen's pointer never ends up with part of it |

## What it knows of the world

Nothing. It holds a cell and plain numbers. `run/src/item/` looks at the world for it: which furnaces are loaded, which have gone, and whether the block should be `furnace` or `furnace_lit`, which is what makes it give light. See that directory's README.

A store is a plain array that it walks. A tick walks it anyway, and a look by cell comes only with a block changing or a screen opening. 1024 is more furnaces than anyone keeps burning; the next one is refused and counted, rather than spending memory on what does not happen.
