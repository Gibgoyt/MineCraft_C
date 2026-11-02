# run/src/item/container

**Purpose:** chests: 27 slots at a cell.
**Tier:** Arena. `container_start` takes its array from the arena `run/src/item/` gives it, once per world.
**Owns:** nothing. `container_store_t` is a field of `item_t`, and `borrowed_chests` is in its world arena.
**Lifetime:** World.
**Threads:** the sim's alone.
**Public functions** (`run/include/item/container/index.h`):

- `container_start`: an empty store.
- `container_at`, `container_open`, `container_remove`: the chest at a cell; the same making one; ending one and giving back what it held.
- `container_slot`: a slot, for a screen's clicks.
- `container_add_stack`: a stack in, on to stacks of it and then the first empty slot.
- `container_quick_move_in`, `container_quick_move_out`: shift-clicks across, with the inventory's books kept.
- `container_items_held`: how many items it holds.

**May be called by:** `run/src/item/`, and the screens of Stage 14.3.
**Depends on:** `arena`, `item/stack`, `item/inventory`.

## What it is not

- **One chest is one block.** Two side by side stay two: a double chest is not in this stage's plan.
- **It knows nothing of the world.** `run/src/item/` makes one when a chest goes down, and spills it when one goes. See that directory's README.
- **A look by cell is a walk of the array,** which is plain and is only done when a block changes or a screen opens: nothing here runs in a tick. The store is full at 4096, and the next is refused and counted.
