# run/src/item

**Purpose:** what a player has. The Owner of `stack` and `inventory`, and of what Stages 13.2 and 13.3 add here.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_world` over it: what its parts keep for as long as a world is open. A `static_assert` checks that `inventory` fits.
**Lifetime:** Process for `item_t` and the region. World for what is in the arena: `item_open` starts it and `item_close` ends it, and nothing is kept until Stage 16.2 saves.
**Threads:** `main` for the pair and `item_open`, before the threads start, and `item_close`, after they stop. `item_hotbar_apply` and the inventory on `sim`. `item_hotbar_send` and `item_hotbar_scroll_send` from the thread that owns input, through two atomics on a cache line of their own.
**Public functions** (`run/include/item/index.h`):

- `item_init`, `item_deinit`: reserve the region; release it. Deinit is safe on a zeroed `item_t`.
- `item_open`, `item_close`: a world's start and its end.
- `item_hotbar_send`, `item_hotbar_scroll_send`: the number keys and the wheel.
- `item_hotbar_apply`: what they asked for, on the sim's next tick.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `heap`, `arena`, the generated `tables/items/`, and its parts below.

**Parts:** `stack`: one slot's worth. `inventory`: 36 slots and a hotbar.

## Why an item is not a dropped item

`run/src/entity/item_drop/` carries an index into `block_drop_items`, because all it ever holds is what blocks drop, and its bytes and pictures are held to by tests. `block_drop_items[i].item` is the `item_id_t` of that row, and `game` makes the change on pickup.
