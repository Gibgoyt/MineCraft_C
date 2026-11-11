# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 6: Crafting stations

**Status:** built and agent-verified on the defaults Ahmed took on 2026-11-10 (Issues found 1); left: Ahmed's gate, and his rearranging after his Hypixel research   (updated 2026-11-10)

## Goal

Each kind of crafting has its own station, and the crafting table only makes stations. You start with nothing, and can reach each station in a clear order. It should feel like a real game where things make sense, **not a Minecraft sandbox**.

**Ahmed's words:**

- "I can not create a stick from 2 logs on the wood bench???? ... please add all wood working stuff on the woodworking bench!!"
- "can't convert logs to planks also! planks to sticks!"
- "crafting table should only make the other benches I think very similar to hypixel but I need to properly research and implement these mechanics!"
- "No I don't want an iron working bench anymore!"
- "perhaps even a iron smith table that takes up 2x2x1 that I can use to smelt iron any metal ores, make any metal tools from here, I can only make armour from here too"
- "any metal does not cover diamond! it covers iron, copper, and gold, we can also make alloys!"
- "right now nothing is able to work to forge diamond that is perfectly fine tho!"
- "similar to hytale please let's create 2 tables for leather stuff 1. leather tanning rack 2. leather workbench"
- "no armour can be created without leather workbench! ... I think leather armor + iron put into the iron smith should make it into iron armour"
- "furnace + other stuff should probably make an iron smith table! but the furnace itself not sure what the heck that should be able to do!"
- "it should also be realistic, as not sure which ores need a furnace and which need a blast furnace???"

## Stations

| Station | Size | Makes |
|---|---|---|
| Crafting table | 1×1×1 | **Only the other stations** |
| Woodworking bench | 2×1×1 | Logs → planks, planks → sticks, 2 logs → sticks, everything wooden, **wooden tools** |
| Stoneworking bench | 2×1×1 | Everything stone, **stone tools** |
| Smith table | **2×2×1** | Works **iron, copper and gold**, and alloys such as stainless steel (phase 11): **all metal tools**, and **metal armour from leather armour** |
| Leather tanning rack | size to decide | Hides → leather, over time |
| Leather workbench | size to decide | Leather → **leather armour**, and other leather goods |
| Furnace | 1×1×1 | Food, glass, charcoal, bricks, and the low-heat ores (below) |
| Blast furnace | size to decide | The high-heat ores and alloys (below), **if Ahmed wants it** |

**There's no iron-working bench.** Ahmed dropped it.

**Diamond:** nothing can work diamond for now, so diamond tools and armour can't be made. Ahmed said that's fine. Their recipes are turned off, and diamonds stay as mined items.

## Armour comes from the leather workbench

1. Hides from animals (phase 4) → **tanning rack** → leather.
2. Leather → **leather workbench** → leather armour, in four pieces.
3. Leather armour piece + metal at the **smith table** → that metal's armour piece: iron, copper, gold or stainless steel.

So no armour exists without the leather workbench, as Ahmed asked. Step 3 is my suggestion for his "leather armor + iron put into the iron smith": the metal is riveted onto the leather, as real lamellar and brigandine armour was. **To be confirmed:** how much metal each piece takes. A starting point is the same count Minecraft uses for that piece in ingots.

## Furnace or blast furnace: what real metals need

**General metallurgy, from what I know, not looked up:**

| Metal | Melts at | How it was really made | Suggested station |
|---|---|---|---|
| Gold | 1064 °C | Often found almost pure; melted, not smelted | Furnace |
| Copper | 1085 °C | Smelted from ore in simple furnaces, from about 5000 BC | Furnace |
| Iron | 1538 °C | A bloomery reduced ore to a spongy bloom at about 1200 °C, hammered out. A blast furnace melts it | **Blast furnace** |
| Stainless steel | about 1400–1450 °C | Iron with chromium; today made in electric arc furnaces | **Blast furnace** |
| Chromium | 1907 °C | Not smelted in any old furnace; made by special methods | **Blast furnace,** as a game simplification |

So the furnace does food, glass, charcoal, bricks, copper and gold, and the blast furnace does iron, chromium and alloys. **The numbers are from general knowledge,** and phase 11's alloys are a game take on them.

**"Furnace + other stuff makes the smith table":** suggested recipe, a furnace + iron ingots + stone, made on the crafting table. Exact pattern to choose.

## The order you'd unlock things

**A proposal, for Ahmed to change after his research:**

1. **Hands (2×2):** 4 logs → crafting table.
2. **Crafting table:** logs → woodworking bench; cobblestone → stoneworking bench; furnace + iron + stone → smith table; leather tanning rack and leather workbench from wood and sticks.
3. **Woodworking bench:** planks, sticks, wooden tools.
4. **Stoneworking bench:** stone tools, furnace, blast furnace.
5. **Furnace:** copper and gold, from early on, as cooking is.
6. **Smith table:** copper and gold tools first, then iron once the blast furnace gives iron.
7. **Tanning rack and leather workbench:** once animals give hides.

A test will walk this order from a world with only logs and cobblestone, and fail if any recipe can't be reached.

## Todo

- [x] **Ahmed:** confirm the order, the blast furnace, and where mixed things go: torches, beds, bows. Defaults taken by Ahmed's 2026-11-10 instruction: the proposed order, a blast furnace, torches, beds and bows on the woodworking bench (Issues found 1)
- [x] Add stations to `compile/src/game_tables/recipes/` and `config/recipes/README.md`: `woodworking`, `stoneworking`, `smithing`, `leatherworking`, plus tanning as a timed process like smelting
- [x] Add the stoneworking bench, 2×1×1, like the woodworking bench
- [x] Add the smith table: four blocks in 2×2×1, placed only where all four cells are free, broken together dropping one. Its screen has a crafting grid and alloying slots (phase 11)
- [x] Add the tanning rack: hides in, leather out over time, saved with the world
- [x] Add the leather workbench: leather armour and other leather goods
- [x] Add the blast furnace, if Ahmed agrees: like the furnace, for high-heat ores
- [x] Move every recipe to its station: all wood to woodworking, all stone to stoneworking, metal tools and metal armour to smithing, leather armour to leatherworking, high-heat ores to the blast furnace. The crafting table keeps only stations
- [x] Add copper tools, and a copper tier in `config/items/README.md`'s tool tiers. Today it's a fixed list in code (`wood stone iron diamond gold`)
- [x] Turn off diamond tool and armour recipes, saying why in their YAML
- [x] Add a test that **every** recipe is reachable from logs and cobblestone, walking the stations in order, with nothing circular
- [x] Add a test that each recipe works on its own station only, and nothing is made in two places
- [x] Add a test that armour can't be made without the leather workbench in the chain
- [x] Update `run_playthrough` and every script and test that crafts

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_item_.*|run_ui_screens|run_player_interaction.*|run_playthrough|run_save_state|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: start a new world with nothing, and work up through each station to iron armour.

## Commits

Appended as they land:

## Issues found

**Built from 2026-11-10 on Ahmed's instruction** (2026-11-10: "build it now with the proposed order; I will change it after my Hypixel research"). The order, the stations and every recipe's place are in YAML, so rearranging is changing `station:` lines.

**1. The defaults taken, by Ahmed's 2026-11-10 instruction.** The proposed order, as written above. A blast furnace: yes, for iron, chromium and alloys, with the furnace for food, glass, charcoal, bricks, stone, copper and gold. Sizes: tanning rack 2×1×1, leather workbench 2×1×1, blast furnace 1×1×1. A metal armour piece is Minecraft's ingot count for that piece around the leather piece (helmet 5, chestplate 8, leggings 7, boots 4). Torches, beds and bows (and arrows) are woodworking. The smith table is a furnace, iron ingots and stone on the crafting table: `["III", "SFS"]`. Diamond tools and armour are `off`, with the reason in each file.

**2. The station layout,** for phase 11 part B and for Ahmed to rearrange (`config/recipes/README.md` has it in full):

| Station | Block | What it makes |
|---|---|---|
| `hands` (new, see 3) | the inventory's 2×2 | crafting table (4 logs), dyes, bone meal, sugar, seeds, bread, cookie, pumpkin pie, mushroom stew, clay, snow, coarse dirt |
| `table` | crafting table | only the stations: woodworking bench `["LLL"]` logs, stoneworking bench `["CCC","L L"]`, smith table `["III","SFS"]`, tanning rack `["SSS","S S","S S"]` sticks, leather workbench `["PPP","S S"]` |
| `woodworking` | woodworking bench 2×1×1 | planks, sticks (2 planks → 4, **2 logs → 16**), wooden tools, stairs, slab, sign, door, ladder, chest, boat, bowl, bed, torch, bow, arrow, paper |
| `stoneworking` | stoneworking bench 2×1×1 | stone tools, furnace, **blast furnace** `["BBB","BFB","SSS"]` (brick, furnace, stone), stone bricks/slab/stairs, bricks, sandstone, terracotta, andesite, mossy cobblestone, packed and blue ice, glass bottle and door, TNT |
| `smithing` | smith table 2×2×1 | copper, iron and gold tools; metal armour from leather armour; nuggets and ingots from nuggets; bucket, minecart, iron door, shears, flint and steel. **Phase 11 part B's alloys go here:** its screen has three alloy slots drawn right of the output (two inputs above one output, at texture x 150), faint and taking no clicks. The alloy recipe can be a new `kind` the smith table's alloy slots match, or a smelt with `station: blast_furnace` (the blast furnace's process is ready for alloys) |
| `leatherworking` | leather workbench 2×1×1 | leather armour, book |
| smelt `furnace` | furnace | food, glass, charcoal, brick, stone, smooth stone, terracotta, deepslate, dried kelp, green dye, copper, gold, and coal, diamond, emerald and lapis from their ores |
| smelt `blast_furnace` | blast furnace (lit while burning) | iron and chromium |
| smelt `tanning_rack` | tanning rack, no fuel | hides (cow, pig, goat: `group_hides`) → leather, 600 ticks |

**3. A `hands` station was needed, and is my addition.** "The crafting table should only make the other benches", but some things are made with no tool and fit no trade: dyes from flowers, sugar, seeds, simple food. Before, the hands' 2×2 made the table's recipes that fitted it. Now the hands have their own station: their 2×2 makes only `hands` recipes, which must fit 2×2 (the compiler refuses one that does not), and the table's 3×3 only `table` ones. The table itself is made in the hands from 4 logs, as the proposal's step 1 says. Two recipes changed to fit: **bread** is 3 wheat shapeless (was a row of 3), and **melon** (9 slices) is `off`, as no 2×2 holds nine and no station makes food. Ahmed may prefer a kitchen station later.

**4. Copper tools come after iron, not before.** The proposal's step 6 says "copper and gold tools first, then iron once the blast furnace gives iron", but the smith table's recipe takes iron ingots, so the smith table itself needs the blast furnace. Taken as written; if Ahmed wants copper first, change `smith_table`'s `I: iron_ingot` to `copper_ingot` (one line), and the walk test still passes.

**5. Bows wait for string.** The bow is woodworking and takes `string`, as before, which nothing drops (no spiders). Left as it was ("use what exists"): it waits for a string source.

**6. One grid for every station with its own block.** `item_t.borrowed_crafting_bench` is shared by the woodworking and stoneworking benches, the smith table and the leather workbench: opening a station's screen sets its station, and closing gives its cells back as before. So no save format change: `SAVE_STATE_VERSION` stays 5. A save's hands grid of the table's station (every save before this) opens as the hands', and the bench grid may be any of those four stations. The smelting store's record gained a `process` byte in its padding (`smelting_furnace_t` stays 64 bytes), 0, the furnace, in every older save; a save is refused only for a process past `RECIPE_PROCESS_COUNT`.

**7. Tool tiers are data: `config/tiers/<tier>/index.yaml`** (`harvests`, `speed`, `axe_attack_speed`, `hoe_attack_speed`), read by `compile/src/game_tables/items/`, which emits `item_tiers[]`. `equipment_tool_speed_milli` and the axe's and hoe's attack speed now read it, and the fixed `ITEM_TIER_*` code (gold's 12000, the attack-speed switches) is gone. The block ladder (`none wood stone iron diamond`, what a block needs) stays in the blocks compiler. Copper: harvests stone, speed 5000, durability 190, Minecraft 1.21.9's. Equipment's tool speed now returns each tier's speed (it returned 0 but for gold, meaning "go by the rung"); the numbers are the same.

**8. Stations of more than two blocks.** `run/src/world/bench/` is now a table of shapes (`BENCH_SHAPES`): parts in order, each by right and ahead of the first in the placer's frame. `bench_parts` gives every part's step and voxel; `bench_half_other` stays for two-part ones. Placing (`player_place_plan_t` now has 4 cells and each cell's `was_there`; `PLAYER_PLACE_CELLS` = 4 = `ENTITY_RAYS_PREVIEW_CELLS`, so the blue preview draws the smith table's four), mining (every other part goes, the one mined drops one) and blasts (the same, put back if the part listed finds no room) all go by it. A screen opens at the station's first part, if it is there, so a tanning rack's slots are found from either half.

**9. Tests.** `run_item_crafting_station` is rewritten: the table makes only stations; every recipe at its station and no other, nothing made at two stations; **the walk** (every recipe and smelt reached from what the world gives, logs and cobblestone among it, a station once its item is made, a smelt once its block and fuel are had, what only a tiered block drops once a tool harvests it) printing `rounds: hands 1, table 2, woodworking 3, stoneworking …`; no armour without the leather workbench; metal armour from leather; logs to planks and sticks only at the bench; tanning with no fuel and iron only in the blast furnace. `run_item_crafting_copper` is the smith table's patterns round the leather piece. `run_player_interaction_bench` has the smith table each way, mined by any part, refused by any cell, and the other two-long stations. `run_ui_screens` has smithing and tanning replays with goldens, and opens every new station's screen in a real game. `render_feature_shapes` has a stations scene with two new goldens. `compile_game_tables` checks stations, processes, `off`, the hands' size and tier files.

**10. The playthroughs are retuned, keeping their intent.** `day.script`, `stairs.script`, `bench.script` and `door.script` now open with the new start: four logs, one in each of the inventory's cells, make the crafting table; a log on the table makes no planks (`table_no_planks.png`, which replaces `table_no_pickaxe.png` and `table_no_stairs.png`); three logs in a row make the woodworking bench; on the bench, two logs give eight planks (`planks.png`). Day: two planks to four sticks, then the pickaxe. Stairs and doors: their patterns from six of the planks. Bench: the two logs left go on its grid (showing 16 sticks) and come back on closing. The clicks were worked out from the screens' layout (window at 144, 74, scale 2) by a small generator, and checked in the screenshots kept by `MC_PLAYTHROUGH_KEEP=1`: the table, the bench and its title, the pickaxe in slot 4 with 3 planks and 2 sticks beside it, three doors made, one placed and opened beside the bench. The tree gives 9 logs that are picked up (the first run showed none left after 4 + 3 + 2), so the inventory lines are now 7 items in 4 stacks (day and stairs), 3 in 2 and 4 in 3 (bench). The door that comes down is now walked over and picked up, so the door's hand ends with 3, not 2.

**11. The terrain digests moved by renumbering alone, proved.** Twelve new blocks sort among the others (`blast_furnace*` after `birch_*`, `leather_*`, `smith_*`, `stoneworking_*`, `tanning_*`), so seven pins failed: density, biome, feature generator row and forest, hydrology and its flood, and ore. With a temporary back-conversion in each test's digest (each voxel's id turned back into the numbering from before, state bits kept), all seven gave their old pins exactly (density `0x852115BE32F7F98D`, biome `0xA6E7EB530487F99B`, feature generator `0x963084164D7E4DAE`, forest `0x9C3DACA353F60E1C`, hydrology `0x8579B6C6CD735924`, flood `0x807D0BBA1F2B2B1D`, ore `0x5D04B9F951A2FCD9`: density, feature and ore passed whole; biome and hydrology failed only their column digests, which are not voxels and which the conversion garbles). The back-conversion is not in the tree. Repinned with "Plan 005 phase 6" lines: density `0x86525EAFA0922E11`, biome `0x3FC59F20EBEF2F5B`, feature generator `0xCFD3A129B38458E6` (the row's comment keeps its old value), forest `0xFECA655E585C850A`, hydrology `0xA9A8D4C2374442E2`, flood `0x9E3FB320B6C42CB1`, ore `0x7E24EC83BDAC1B5B`. `TERRAIN_GENERATOR_VERSION` stays 1. 164 blocks now, of the 170 `compile_game_tables` allows: the next phase to add blocks must raise that bound.

**12. Pictures.** Regenerated with `MC_GOLDEN_WRITE=1` and looked at: `run_ui_screens`' `inventory_carrying.png` and `inventory_tooltip.png` (four logs making a crafting table, which ends in slot 3), `crafting_ready.png` and `crafting_done.png` (the stone pickaxe now at the stoneworking bench, titled "Stoneworking"). New: `smithing_ready.png` (the smith table's grid and its three faint alloy slots under "Alloy"), `tanning_working.png` and `tanning_done.png` (no fuel slot, no flame, two hides to two leather), and `render_feature_shapes`' `stations.png` and `stations_back.png` (the smith table each of the four ways, the three two-long stations, the blast furnace cold and lit). The two tests are already in `MC_ART_TESTS`. 43 new procedural textures (the stations' faces and items, the blast furnace, copper tools), each in `textures/vector.yaml` and `textures/import_minecraft_default.yaml` (the smithing table, blast furnace and copper tools mapped to Minecraft's, the rest `keep`), with `blocks/blast_furnace_front_lit` emissive as the furnace's is. The UI tables stay under their 1.25 MiB (`run_ui_font`, `run_ui_hud` pass).

**13. `tests/run/item/crafting/index.c` is 1560 lines,** past the 1500 where a file should split (the 2000 limit holds): the blast furnace and tanning rack in the world went beside the furnace's own test, which needs its world fixture. A split is left for whoever next adds to it.

**History:** blocked on purpose until 2026-11-10, while Ahmed researched Hypixel and Hytale; he then said to build the proposal and change it later.

**Needs phases 4 and 11:** hides come from animals, and alloys and chromium from ores.

**This changes the whole early game.** Every test and script that crafts will need updating.

**Open for Ahmed:**

1. A blast furnace for iron, chromium and alloys, with the furnace for copper, gold, food and glass? Default: yes.
2. Sizes for the tanning rack, leather workbench and blast furnace? Default: 2×1×1, 2×1×1 and 1×1×1.
3. How much metal makes each metal armour piece from its leather one? Default: Minecraft's ingot count for that piece.
