# run/src/save/state/translate

**Purpose:** a state file saved with other block, drop, item or mob tables, rewritten in memory to mean the same things in this build's, by name.
**Tier:** Borrow (depth 3): it works in the file and the maps its caller lends it, and on its stack.
**Owns:** nothing.
**Lifetime:** one call.
**Threads:** whichever opens a world.
**Public functions** (`run/include/save/state/translate/index.h`):

- `save_state_translate`: the one job.

**May be called by:** `save/state`, on opening a world, and nothing else.
**Depends on:** `save/names`, `save/world`, for its statuses, `log`, `assert`, the structs of `save/state`, `item`, `entity` and `player`, and the generated tables.

## What it changes

| Where | Numbers | If this build has not got it |
|---|---|---|
| Inventory, pointer, crafting cells, armour, chests, furnace slots | item | The stack is gone, and the slot empty |
| Furnace `cooking` | item | No item, and `cook_ticks` goes back to 0 |
| Player's `held_item`, for eating and for swinging | item | No item, which only starts those over |
| Ring of a player's drops, items on the ground | drop row | The drop is gone. On the ground it counts in `lost_count` |
| Player's `mining_block`, `hand_block`, `last_mined` | block | Air. Mining stops, and a hand of it holds nothing |
| Each body's `kind` | mob | The body is gone: its slot goes back in the free ring, as for `entity_store_remove` |
| A crafting grid's `recipe` and `output` | recipe, item | Thrown away here, and worked out again by `save_state_apply` from the cells and this build's recipes |

A stack also keeps to what its item is today: a count past `max_stack` comes down to it, and wear past `durability` comes down to one use left.

The log gets one warning per name gone and a count of what went, so a person can see what a build took away.

## How it trusts a file

It trusts nothing, and believes only what it must. It is reached once the file's lengths, hashes, version, seed and generation are good, and only if the file's `table_hash` is not this build's. Any number it uses as an index is checked first: a section's length, a count against a capacity, an id against the length of its list. Anything else in the file it leaves for `save_state_check`, which `save_state_open` runs on the result, as it does on any file. So a forged state gets the same answer through here as it would without.

The disk is not touched: the file there stays as it was until the next save writes this one. A section may shrink and nothing grows until the new names go on the end, so it works in place and never needs a second image.
