# run/src/item/inventory

**Purpose:** what a player carries: 36 slots, the first 9 of them the hotbar, and which hotbar slot is in hand.
**Tier:** Arena. It takes its slots from the arena its parent gives it, once, and frees nothing.
**Owns:** nothing. `borrowed_slots` is in `run/src/item/`'s world arena.
**Lifetime:** World, as that arena is.
**Threads:** the sim's alone, or whichever one thread holds it in a run with no threads. Choosing a slot from `main` goes through `item_hotbar_send` in `run/src/item/`.
**Public functions** (`run/include/item/inventory/index.h`):

- `inventory_start`: empty, from an arena.
- `inventory_slot`, `inventory_slot_read`, `inventory_selected`: looking at slots.
- `inventory_select`, `inventory_scroll`: which hotbar slot is in hand.
- `inventory_add`, `inventory_add_stack`, `inventory_room`: putting in, and how much would fit.
- `inventory_count`, `inventory_remove`, `inventory_selected_take`, `inventory_slot_move_out`: what is there, and taking it away.
- `inventory_swap`, `inventory_quick_move`: moving between slots.
- `inventory_items_held`, `inventory_stacks_held`: totals.

**May be called by:** `run/src/item/`, `run/src/game/`, and what later stages put under `run/src/item/` and in the screens.
**Depends on:** `arena`, `assert`, `item/stack`.

## Where things go

As Minecraft does it, so a player's habits hold:

1. On the stack in hand, if it is of the same item.
2. On any other stack of it, from slot 0 up.
3. In the first empty slot, from slot 0 up, which is the hotbar first.

So what is picked up does not go to the selected slot if that slot is empty and another hotbar slot before it is too. What did not fit stays with the caller: an item stays on the ground.

Slot numbers: 0 to 8 are the hotbar, 9 to 35 the rows above it. Taking away goes from the last slot back, so that what is on the hotbar is the last to go. A shift-click goes the other way across that line from where it started.

## The books

`added_count = removed_count + inventory_items_held()`, as long as what is added and taken goes through these functions. A screen moving stacks with `inventory_slot` and a pointer goes past them while the pointer holds something. `refused_count` is what `inventory_add` had no room for, so what stays on the ground and is offered again each tick counts each time.
