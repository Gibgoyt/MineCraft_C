# config/recipes

One directory per recipe, and to see what a recipe does, open its one file:

```yaml
# config/recipes/iron_pickaxe/index.yaml
name: iron_pickaxe
kind: shaped
pattern: ["MMM", " S ", " S "]
keys: { M: iron_ingot, S: stick }
gives: iron_pickaxe
station: smithing
```

To add one: make `config/recipes/<name>/index.yaml`, and build. `compile/src/game_tables/recipes/` turns these into `tables/recipes/`, and refuses a field it does not know, a name that is nothing, and two recipes one grid could match, with the line. `run/src/item/crafting/` and `run/src/item/smelting/` use the tables.

The directory is usually the name of what it gives. A second way to the same thing says how: `dye_white_from_bone_meal`. `smelt_*`, `fuel_*` and `group_*` say what kind they are at a glance; only the last means anything to the tool.

## Kinds

| `kind` | What it is | Fields, other than `name` and `kind` |
|---|---|---|
| `shaped` | A pattern in a grid | `pattern`, `keys`, `gives`, `count`, `station`, `off` |
| `shapeless` | These things, anywhere in a grid | `ingredients`, `gives`, `count`, `station`, `off` |
| `smelt` | What a furnace, a blast furnace or a tanning rack makes of something, over time | `input`, `gives`, `count`, `ticks`, `station`, `off` |
| `fuel` | What burns, and for how long | `items`, `ticks`, `leaves` |
| `group` | A name for several items, wherever a recipe takes one | `items` |
| `alloy` | Counted inputs in the smith table's alloy slots, beside its grid: plan 005 phase 11 part B | `inputs`, `gives`, `count`, `station`, `heat`, `off` |

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `pattern` | shaped | 1 to 3 rows of 1 to 3 cells, all as long, each cell a letter or a space for nothing, in quotes. No empty row or column at an edge. It also matches mirrored left to right, and anywhere in a grid big enough | |
| `keys` | shaped | Each letter of the pattern, and what it is: an item or a group | |
| `ingredients` | shapeless | A list of 1 to 9 items or groups | |
| `input` | smelt | An item or a group | |
| `inputs` | alloy | A list of 1 or 2 of `{ item: <item>, count: <n> }`, each item once and not a group, each count 1 to what one slot of it holds: each input is a slot of its own | |
| `gives` | all that make something | An item. Not a group | |
| `count` | | 1 to what one slot of that item holds | 1 |
| `ticks` | fuel | 1 to 65535. For a smelt, how long one takes, and for a fuel, how long one burns, at 20 a second | 200, for a smelt |
| `items` | fuel, group | A list of items. A fuel's may name groups too; a group's may not | |
| `leaves` | | A fuel's: the item left in the slot after it burns, as a lava bucket leaves a bucket | nothing |
| `station` | | Where it is made. A shaped or shapeless recipe's: `hands`, `table`, `woodworking`, `stoneworking`, `smithing` or `leatherworking`. A smelt's: `furnace`, `blast_furnace` or `tanning_rack`. An alloy's: `smithing`, the only station with alloy slots | `table`, for a smelt `furnace`, and for an alloy `smithing` |
| `heat` | | An alloy's: a block that burns, `furnace` or `blast_furnace`, which must be lit within 4 blocks of the smith table along every axis | none needed |
| `off` | | Why it is turned off, in quotes. It is read and checked as any other, so what it names must exist, and the tables leave it out | on |

## Stations

A recipe says where it is made, and a grid makes only its own station's recipes. Plan 005 phase 6 gave each kind of making its own station, **and the crafting table makes only the other stations.** It should feel like a real game where things make sense, not a Minecraft sandbox. Ahmed took this order on 2026-11-10, and will change it after his research into Hypixel's crafting: every line of it is in these files, so moving a recipe is changing its `station`.

| Station | Its block, and size | Its grid | What is made there |
|---|---|---|---|
| `hands` | none | The 2 by 2 in the inventory | Logs to planks, the crafting table from 4 logs or 4 planks, and what needs no tool: dyes, bone meal, sugar, seeds, simple food, clay, snow, coarse dirt. A recipe here must fit 2 by 2 |
| `table` | `crafting_table`, 1 by 1 by 1 | 3 by 3 | **Only the stations:** the woodworking bench (3 logs), the stoneworking bench (3 cobblestone on 2 logs), the smith table (3 copper ingots over stone, a furnace, stone), the tanning rack (7 sticks) and the leather workbench (3 planks on 2 sticks) |
| `woodworking` | `woodworking_bench_west` and `_east`, 2 by 1 by 1 | 3 by 3 | Planks to sticks, 2 logs to 16 sticks, everything wooden and the wooden tools, and torches, beds, bows, arrows and paper |
| `stoneworking` | `stoneworking_bench_left` and `_right`, 2 by 1 by 1 | 3 by 3 | Everything stone and the stone tools, the furnace and the blast furnace, bricks, sandstone, terracotta, ice blocks, glass goods and TNT |
| `smithing` | `smith_table_front_left`, `_front_right`, `_back_left` and `_back_right`, 2 by 2 by 1 | 3 by 3, and the alloy slots: two counted inputs and an output (plan 005 phase 11 part B) | Iron, copper, gold and stainless steel: every metal tool, metal armour from leather armour, nuggets, buckets, shears, minecarts, iron doors; and alloys, stainless steel from 9 iron and 1 chromium |
| `leatherworking` | `leather_workbench_left` and `_right`, 2 by 1 by 1 | 3 by 3 | Leather armour, four pieces, and books |

A smelt is done over time in a block with three slots, input, fuel and output:

| Station | Its block, and size | Fuel | What is done there |
|---|---|---|---|
| `furnace` | `furnace`, 1 by 1 by 1 | yes | Food, glass, charcoal, bricks, stone, and the ores copper, gold and iron (iron since Ahmed, 2026-11-12) |
| `blast_furnace` | `blast_furnace`, 1 by 1 by 1 | yes | The high-heat ore chromium (iron smelts in the furnace, Ahmed 2026-11-12). An alloy whose file says `heat: blast_furnace` needs one lit by the smith table |
| `tanning_rack` | `tanning_rack_left` and `_right`, 2 by 1 by 1 | **none**: by time alone | Hides into leather, 30 seconds each |

**Metal armour is leather armour with metal riveted on:** at the smith table, the leather piece in the middle and Minecraft's count of ingots for that piece around it. So no armour can be made without the leather workbench.

**Diamond can't be worked by anything yet** (Ahmed, 2026-11-06), so its tools and armour have `off`, and diamonds stay as mined.

The order you would unlock them in, which `run_item_crafting_station` walks from what the world gives:

1. **Hands:** 4 logs make a crafting table.
2. **Table:** 3 logs make a woodworking bench.
3. **Woodworking bench:** planks, sticks and the wooden tools. A wooden pickaxe mines cobblestone.
4. **Table:** cobblestone on logs makes the stoneworking bench; sticks and planks make the tanning rack and the leather workbench.
5. **Stoneworking bench:** stone tools and a furnace. **Furnace:** stone, bricks from clay, copper and gold.
6. **Table:** copper, stone and a furnace make the smith table. **Smith table:** copper and gold tools.
7. **Stoneworking bench:** bricks round a furnace on stone make the blast furnace. **Blast furnace:** chromium. **Furnace:** iron too, from raw iron or iron ore. **Smith table:** iron tools, then stainless steel in its alloy slots and its tools.
8. **Tanning rack:** hides from cattle, pigs and goats into leather. **Leather workbench:** leather armour. **Smith table:** metal armour.

Two recipes of different stations may have the same pattern, as no grid can match both. A new station is one word here, one word in `GR_STATIONS` (or `GR_PROCESSES` for a smelt) in `compile/src/game_tables/recipes/`, its block, and its screen's row in `run/src/ui/`.

## Where these depart from the published recipes

Only where the game has no such item. Each is written here so that it can be changed back when the item arrives:

| Recipe | Published | Here | Why |
|---|---|---|---|
| `pumpkin_pie` | pumpkin, sugar, egg | pumpkin, sugar | `egg` is an item now, but until hens lay it nobody can get one |
| `bread` | 3 wheat in a row | 3 wheat, shapeless | Made in the hands, whose grid is 2 by 2 (plan 005 phase 6) |
| `crafting_table` | 4 planks | 4 logs or 4 planks, in the hands | Ahmed, 2026-11-11: logs turn into planks in the inventory's 2 by 2, so a table and benches can be reached from planks |
| `woodworking_bench`, `stoneworking_bench`, `smith_table`, `tanning_rack`, `leather_workbench`, `blast_furnace` | not published, but for the smithing table and the blast furnace | see Stations | The stations of plan 005 phase 6. The smith table takes copper since plan 005 phase 11, so that copper tools come before iron |
| `alloy_stainless_steel` and the stainless steel tools and armour | not published | 9 iron ingots and 1 chromium ingot make 10 stainless steel ingots, and they are worked as iron is | Ahmed's alloys, plan 005 phase 11 |
| `stick_from_logs` | not published | 2 logs, 16 sticks | Ahmed's "a stick from 2 logs on the wood bench" |
| `iron_helmet` and the other metal armour | the ingots alone | the ingots round the leather piece | No armour without the leather workbench (plan 005 phase 6) |
| `tan_leather` | not published | a hide on a tanning rack, 30 s, no fuel | Like Hytale's tanning rack |
| `cookie` | 2 wheat, cocoa beans | 2 wheat, `dye_brown`, shapeless | No cocoa beans |
| `dye_grey_from_azure_bluet` | light grey dye | `dye_grey` | No light grey dye |
| `dye_cyan_from_blue_orchid` | light blue dye | `dye_cyan` | No light blue dye |
| `dye_orange_from_tulip` | orange tulip | `tulip` | One tulip |
| `dye_pink_from_oxeye_daisy` | light grey dye | `dye_pink` | No light grey dye, and grey has one flower |
| `light_gray_terracotta` | light grey dye | `dye_grey` | No light grey dye |
| `glass_door` | not published | 6 glass | The pack's own item |

## What has no recipe, on purpose

Because the game does not yet have what they are made of, or how they are got is not crafting: `written_book`, `obsidian_glass_door`, `potion_*`, the buckets of water and lava, `dye_violet`, `dye_dark_green`, `dye_dark_grey`, and everything a block or a mob only drops. `compile/src/game_tables/items/` keeps them as items anyway.

Turned `off`, with the reason in each file: every diamond tool and piece of armour, as nothing can work diamond yet, and `melon`, whose nine slices take a 3 by 3 that the hands have not got. The bow takes `string`, which nothing drops yet, as there are no spiders: it waits for a source.
