# compile/src/game_tables/items

**Purpose:** reads `config/items/<item>/index.yaml` and the tiers of `config/tiers/<tier>/index.yaml`, checks each file and what it refers to, and generates the item tables.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/items/index.h`):

- `game_tables_items_build`: the whole job, from a config directory to `<out>/items/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none. The fields are in `config/items/README.md`.

- A field it does not know, in an item or its armor, and a `name` that is not its directory's.
- A directory called `none`, which is row 0 and has no file.
- A texture that is not `items/<name>` or `blocks/<name>`, or has no `<textures>/<that>/index.mctex`.
- A `block` with no `config/blocks/<block>/index.yaml`, and `block: air`.
- An item with the name of a block that does not say `block:`: dirt that places nothing would be a mistake nothing else notices.
- A number out of range, or of the wrong type.
- What cannot go together: a tool with no durability, a tool with tiers and no `tier`, a `tier` on what has none, a stack of more than one of what wears, a durability on what places a block, `saturation` without `food`, and a tool that is also armor.
- A `tier` that is no file of `config/tiers/`. A tree with no `config/tiers/` has only `none`, so a tool in it is refused.
- In a tier's file (plan 005 phase 6): a field it does not know, a missing one, a `name` that is not its directory's, a `harvests` that is no rung of a block's tier, and a number out of range.

## The tables

`tables/items/index.h`, from the generated root:

| Name | What |
|---|---|
| `item_id_t`, `ITEM_ID_DIRT` | Item ids. **0 is `ITEM_ID_NONE`**, so a zeroed stack is empty. The rest sort by name, so adding an item moves those after it, and a save stores names |
| `item_defs[ITEM_COUNT]` | `item_def_t` by id: name, texture, damage, saturation, block, durability, stack, tool, tier, food, armor |
| `ITEM_STACK_LIMIT`, `ITEM_NO_BLOCK` | The most any stack holds; what `block` says when it places nothing |
| `item_tier_t`, `item_armor_slot_t` | The words the yaml uses, as numbers. The tiers are `none` and then every file of `config/tiers/` by name, `ITEM_TIER_COUNT` of them |
| `item_tiers[ITEM_TIER_COUNT]` | `item_tier_def_t` by tier: name, the axe's and the hoe's swings a second, speed on its own blocks in thousandths, and the `block_tier_t` it harvests |

- **`tool` is a `block_tool_t`,** in the same numbers as a block's own `tool`, since the two are compared.
- **`tier` is not `harvest_tier`.** Gold wears faster and digs faster than anything, but breaks only what wood breaks, so `tier` says gold and `harvest_tier`, which is a `block_tier_t`, says wood, as gold's file says. Copper's says stone.
- **A dropped item is not an item id.** `block_drops` and `run/src/entity/item_drop/` index `block_drop_items`, and each of those rows says its `item`.
- **A decimal is printed as it was written,** as in the block tables, so the tool never rounds.
