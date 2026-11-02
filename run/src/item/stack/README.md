# run/src/item/stack

**Purpose:** what one slot holds: an item, how many, and how worn. Everything anything may do to a stack, so that no caller can leave a count over the item's `max_stack` or an empty stack that still names an item.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing. A stack is 8 bytes in whoever holds it, and every function works on what it is given.
**Lifetime:** none of its own.
**Threads:** whichever holds the stacks; nothing here is shared.
**Public functions** (`run/include/item/stack/index.h`):

- `item_stack_drop_row`: which row of `block_drop_items` an item lies on the ground as.
- `item_stack_max`, `item_stack_make`: what a slot of an item holds; a new stack, cut down to that.
- `item_stack_is_empty`, `item_stack_is_valid`, `item_stack_can_merge`, `item_stack_room`: looking.
- `item_stack_move`, `item_stack_take`, `item_stack_swap`: changing.
- `item_stack_click_primary`, `item_stack_click_secondary`: a screen's two clicks, with what the pointer carries.

**May be called by:** anything under `run/src/item/`, `run/src/game/`, and the screens of Stage 14.3.
**Depends on:** the generated `tables/items/`.

## The rules

- **Zero bytes are the one empty stack.** That is why `ITEM_ID_NONE` is item 0, and a zeroed slot needs no setting up.
- **Worn stacks merge only if worn the same.** What wears has `max_stack` 1, so in practice it never merges, and the tables refuse anything else.
- **Moving takes what fits and says how much.** A caller never has to look first, and what did not fit stays where it was.
- **The clicks are Minecraft's.** Primary with an empty pointer picks up all; with something it can merge, puts down what fits; otherwise swaps. Secondary with an empty pointer picks up the larger half; with something it can merge, puts down one; otherwise swaps.
