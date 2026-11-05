# config/recipes

One directory per recipe, and to see what a recipe does, open its one file:

```yaml
# config/recipes/iron_pickaxe/index.yaml
name: iron_pickaxe
kind: shaped
pattern: ["MMM", " S ", " S "]
keys: { M: iron_ingot, S: stick }
gives: iron_pickaxe
```

To add one: make `config/recipes/<name>/index.yaml`, and build. `compile/src/game_tables/recipes/` turns these into `tables/recipes/`, and refuses a field it does not know, a name that is nothing, and two recipes one grid could match, with the line. `run/src/item/crafting/` and `run/src/item/smelting/` use the tables.

The directory is usually the name of what it gives. A second way to the same thing says how: `dye_white_from_bone_meal`. `smelt_*`, `fuel_*` and `group_*` say what kind they are at a glance; only the last means anything to the tool.

## Kinds

| `kind` | What it is | Fields, other than `name` and `kind` |
|---|---|---|
| `shaped` | A pattern in a grid | `pattern`, `keys`, `gives`, `count`, `station` |
| `shapeless` | These things, anywhere in a grid | `ingredients`, `gives`, `count`, `station` |
| `smelt` | What a furnace makes of something | `input`, `gives`, `count`, `ticks` |
| `fuel` | What burns, and for how long | `items`, `ticks`, `leaves` |
| `group` | A name for several items, wherever a recipe takes one | `items` |

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `pattern` | shaped | 1 to 3 rows of 1 to 3 cells, all as long, each cell a letter or a space for nothing, in quotes. No empty row or column at an edge. It also matches mirrored left to right, and anywhere in a grid big enough | |
| `keys` | shaped | Each letter of the pattern, and what it is: an item or a group | |
| `ingredients` | shapeless | A list of 1 to 9 items or groups | |
| `input` | smelt | An item or a group | |
| `gives` | all that make something | An item. Not a group | |
| `count` | | 1 to what one slot of that item holds | 1 |
| `ticks` | fuel | 1 to 65535. For a smelt, how long one takes, and for a fuel, how long one burns, at 20 a second | 200, for a smelt |
| `items` | fuel, group | A list of items. A fuel's may name groups too; a group's may not | |
| `leaves` | | A fuel's: the item left in the slot after it burns, as a lava bucket leaves a bucket | nothing |
| `station` | | A shaped or shapeless recipe's: `table` or `woodworking`, where it is made | `table` |

## Stations

A recipe says where it is made, and a grid makes only its own station's recipes.

| Station | Its grids |
|---|---|
| `table` | The 3 by 3 of a crafting table, and the 2 by 2 in the inventory, so a recipe of 2 by 2 or less is made with bare hands too |
| `woodworking` | The 3 by 3 of a woodworking bench |

Anything made only of wood is `woodworking`, but for what it takes to get a bench: planks, sticks, the table and the bench itself. Two recipes of different stations may have the same pattern, as no grid can match both. A new station is one word here, one word in `GR_STATIONS` in `compile/src/game_tables/recipes/`, and one more grid in `run/src/item/`.

## Where these depart from the published recipes

Only where the game has no such item. Each is written here so that it can be changed back when the item arrives:

| Recipe | Published | Here | Why |
|---|---|---|---|
| `pumpkin_pie` | pumpkin, sugar, egg | pumpkin, sugar | No egg |
| `cookie` | 2 wheat, cocoa beans | 2 wheat, `dye_brown`, shapeless | No cocoa beans |
| `dye_grey_from_azure_bluet` | light grey dye | `dye_grey` | No light grey dye |
| `dye_cyan_from_blue_orchid` | light blue dye | `dye_cyan` | No light blue dye |
| `dye_orange_from_tulip` | orange tulip | `tulip` | One tulip |
| `dye_pink_from_oxeye_daisy` | light grey dye | `dye_pink` | No light grey dye, and grey has one flower |
| `light_gray_terracotta` | light grey dye | `dye_grey` | No light grey dye |
| `glass_door` | not published | 6 glass | The pack's own item |
| `bed` | 3 wool, 3 planks | 3 wheat, 3 planks | No wool. A straw mattress, so that a played game can have a bed. Ahmed may want another |

## What has no recipe, on purpose

Because the game does not yet have what they are made of, or how they are got is not crafting: `written_book`, `obsidian_glass_door`, `potion_*`, the buckets of water and lava, `dye_violet`, `dye_dark_green`, `dye_dark_grey`, and everything a block or a mob only drops. `compile/src/game_tables/items/` keeps them as items anyway.
