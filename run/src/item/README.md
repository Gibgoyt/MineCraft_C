# run/src/item

**Purpose:** what a player has, and what stands in the world holding items. The Owner of `stack`, `inventory`, `crafting`, `smelting`, `container` and `equipment`.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_world` over it: what its parts keep for as long as a world is open. A `static_assert` checks that `inventory`, `container` and `smelting` fit.
**Lifetime:** Process for `item_t` and the region. World for what is in the arena: `item_open` starts it and `item_close` ends it, and nothing is kept until Stage 16.2 saves.
**Threads:** `main` for the pair and `item_open`, before the threads start, and `item_close`, after they stop. `item_hotbar_apply`, `item_world_tick`, the inventory, the two crafting grids, both stores and what is worn on `sim`. `item_hotbar_send` and `item_hotbar_scroll_send` from the thread that owns input, through two atomics on a cache line of their own.
**Public functions** (`run/include/item/index.h`):

- `item_init`, `item_deinit`: reserve the region; release it. Deinit is safe on a zeroed `item_t`.
- `item_open`, `item_close`: a world's start and its end.
- `item_hotbar_send`, `item_hotbar_scroll_send`: the number keys and the wheel.
- `item_hotbar_apply`: what they asked for, on the sim's next tick.
- `item_world_begin`, `item_world_tick`: following a world's edits, and a tick of what stands in it.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `heap`, `arena`, `world/edit`, `world/map`, the generated `tables/items/` and `tables/blocks/`, and its parts below.

**Parts:** `stack`: one slot's worth. `inventory`: 36 slots and a hotbar. `crafting`: 2x2 and 3x3 grids and what they make. `smelting`: furnaces. `container`: chests. `equipment`: what tools do and how they wear, and armor.

## How chests and furnaces follow the world

Neither store knows of the world, and the world knows of neither. `item_world_tick` joins them, once a tick on the sim, called by `run/src/game/`:

1. **It reads the world's edit log,** with a cursor of its own, as anyone may. So whatever changes a block is seen, by any road: a hand now, an explosion later. Only a change of *kind* counts, so a furnace lighting up does nothing here.
2. **A chest or furnace that went spills.** Its stacks go through the function it is given, and `game` throws them on the ground. A broken chest loses nothing.
3. **One that came gets its slots.** Anything still held for that cell is dropped, as it can only be left from a column that has been unloaded.
4. **If the log has gone round past the cursor,** it asks the world about each chest and furnace instead. That takes more than the 65536 edits the log holds, between two ticks.
5. **Every furnace in a loaded column ticks,** and its block is set to `furnace` or `furnace_lit` to match, so the light follows through the world's own road. One in a column not loaded waits. One whose column has come back without it is dropped, since nothing is saved until Stage 16.2.

What is not here is opening one: the right button still places, and the screens are Stage 14.3's. `container_open` and `smelting_open` both also work on a block that nothing has yet given slots, such as one a world generated.

**What a chest holds when its column unloads** waits with it, and goes with that column's edits when it comes back without the chest, until Stage 16.2 saves both. Such a chest's slots are only found and dropped if a new chest goes in the same cell, since no tick walks chests: they count against the 4096 until the world closes.

## Why an item is not a dropped item

`run/src/entity/item_drop/` carries an index into `block_drop_items`, and its bytes and pictures are held to by tests. That table has a row for every item since Stage 13.2, in the order of the ids, so a chest can spill anything. `block_drop_items[i].item` is the `item_id_t` of a row, `item_stack_drop_row` is the way back, and `game` makes the change at each end. **Wear does not survive lying on the ground,** as a drop has no field for it, so a worn tool spilled from a chest comes back new. Stage 13.3 could not change that from its own paths, since it takes a field in `item_drop_t` and wear through its pickup: see Issue 2 in that stage's plan file.
