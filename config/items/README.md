# config/items

One directory per item, and to see what an item does, open its one file:

```yaml
# config/items/iron_pickaxe/index.yaml
name: iron_pickaxe
texture: items/iron_pickaxe
stack: 1
tool: pickaxe
tier: iron
durability: 250
damage: 4
```

To add an item: make `config/items/<name>/index.yaml`, and build. `compile/src/game_tables/items/` turns these into `tables/items/`, and refuses a field it does not know or a reference to something missing, with the line. Everything a block drops must have a file here, so a block's own item does too. The numbers are the published ones.

Blocks with no item, because nobody holds them: `air`, `water`, `lava`, `farmland`, `furnace_lit` and the eight `wheat_stage_*`. `wheat_seeds` places `wheat_stage_1`.

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores. Not `none`, which is the empty row | |
| `texture` | yes | `items/<name>` or `blocks/<name>`, whose `index.mctex` must exist | |
| `stack` | | 1 to 64: the most in one slot. 1 for what has a `durability` | 64 |
| `block` | if a block has its name | The block it places. Not `air` | nothing |
| `tool` | | `none pickaxe axe shovel hoe shears sword`, as a block's `tool` says. Needs a `durability` | `none` |
| `tier` | with a `tool` other than `shears` | `wood stone iron diamond gold`. Gold breaks what wood breaks | `none` |
| `durability` | with a `tool` or `armor` | 1 to 65535: the uses in it. Not on what places a block | does not wear |
| `damage` | | 0 to 100: what a hit does, in half hearts | 0 |
| `food` | | 1 to 20: half drumsticks eating it gives | not food |
| `saturation` | | 0 to 20: what eating it gives on top. Needs a `food` | 0 |
| `armor` | | `{ slot: head, points: 2 }`. `slot` is `head chest legs feet`, `points` 1 to 20. Needs a `durability`, and not with a `tool` | not armor |

What wears, what feeds and what protects is only read here: `run/src/item/equipment/` (Stage 13.3) and `run/src/player/survival/` (Stage 13.4) use it.
