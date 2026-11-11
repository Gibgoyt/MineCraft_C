# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 11: Ores and alloys

**Status:** built and agent-verified: part A, ores in yaml, chromium ore, raw chromium and ingots; part B, the `alloy` recipe kind, the smith table's alloy slots, stainless steel from 9 iron and 1 chromium, and its tools and armour a tier above iron. Left: Ahmed's gate (updated 2026-11-10)

## Goal

Each ore's spawn rate lives in its own YAML file, so it can be read and tuned in one place. Chromium ore exists, and stainless steel can be made from 9 iron and 1 chromium. More ores and alloys can be added the same way.

**Ahmed's words:** "we can also make alloys! so let's also create many more ores. stainless steel let's add that for now please that is 9xiron and 1xchromium please! we have to add chromium ore! inside its yaml file you can create its spawn rate please!"

## How it is today

**Ores aren't in YAML at all.** Where each ore spawns is a table in code, `run/src/terrain/ore/index.c:53`:

```c
ORE_T("ore/copper", BLOCK_ID_COPPER_ORE, BLOCK_ID_DEEPSLATE_COPPER_ORE, -185, 96, 10, 32u * 256u),
```

Each row has: name, the ore block, its deepslate block, lowest and highest z, vein size, and veins per 256 chunks. Ores today: coal, copper, iron, gold, lapis, diamond, emerald.

**No alloys exist:** no way to mix metals, and no recipe takes counts, only one item per grid cell.

## Design

### An ore in one file

```yaml
# config/ores/chromium/index.yaml
name: chromium
block: chromium_ore            # in stone
deep_block: deepslate_chromium_ore
drops: raw_chromium
z_min: -250
z_max: -40
vein: 6                        # blocks in one vein
per_256_chunks: 1024           # veins: about 4 a chunk
```

`compile/src/game_tables/` reads these into the table `run/src/terrain/ore/` uses now, so moving the existing ores to YAML changes no world. The tests' pinned digests prove that.

**Chromium's numbers are a starting point:** rare, and deep only, below iron. Ahmed can tune them in the file.

### Alloys at the smith table

A grid cell takes one item, and 9 iron + 1 chromium is 10, so alloying gets **its own slots** on the smith table: a crucible with counted inputs.

```yaml
# config/recipes/alloy_stainless_steel/index.yaml
name: alloy_stainless_steel
kind: alloy
station: smithing
inputs: [{ item: iron_ingot, count: 9 }, { item: chromium_ingot, count: 1 }]
gives: stainless_steel_ingot
count: 10                      # to decide: 10 in, 10 out
heat: blast_furnace            # if phase 6 adds one
```

### Chromium and stainless steel

| Item | From |
|---|---|
| `raw_chromium` | Mining chromium ore, with an iron pickaxe or better |
| `chromium_ingot` | Smelting raw chromium, at high heat (phase 6's blast furnace, if added) |
| `stainless_steel_ingot` | Alloying: 9 iron ingots + 1 chromium ingot |
| Stainless steel tools and armour | Smith table, a tier above iron |

### More ores, waiting for Ahmed's yes

"Many more ores": these each make a well-known alloy, so they suit the smith table.

| Ore | Alloy it makes |
|---|---|
| Tin | **Bronze:** copper + tin |
| Zinc | **Brass:** copper + zinc |
| Nickel | Nickel steel, and part of some stainless steels |
| Silver | Sterling silver, coins and the like |

**Not in this phase until Ahmed picks.** Each is one YAML file, two blocks, an item and a recipe, once this phase exists.

## Todo

- [x] Add `config/ores/` and its README. Read it in `compile/src/game_tables/`, and generate the table `run/src/terrain/ore/index.c` uses
- [x] Move every existing ore row to `config/ores/<ore>/index.yaml`. Check the terrain digests don't change, which proves the move changed no world
- [x] Add `chromium_ore` and `deepslate_chromium_ore` blocks, `raw_chromium` and `chromium_ingot` items, and their textures as our recipes
- [x] Add a `chromium` ore file with the spawn numbers above, and measure how many veins a chunk gets with `bench_world_map`
- [x] Add an `alloy` recipe kind with counted inputs, in `compile/src/game_tables/recipes/` and `config/recipes/README.md`
- [x] Add alloy slots to the smith table screen from phase 6, and make stainless steel from 9 iron and 1 chromium
- [x] Add stainless steel tools and armour, a tier above iron. Armour comes from leather armour, as phase 6 says
- [x] Add tests: chromium spawns only in its z range and at about its rate; 9 iron and 1 chromium make stainless steel, and nothing else does; a stack of 8 iron makes nothing; moving ores to YAML changed no world

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_terrain_.*|run_item_.*|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 600 build/debug/bin/bench_world_map /tmp/mc_c_maps
    pgrep -a minecraft_c    # must print nothing

Part B's own, through the same wrapper:

    tools/test-or-build.sh timeout 1500 ctest --preset debug -R 'run_item_|compile_game_tables|run_save_state|run_ui_screens|run_ui_font|run_ui_hud|run_playthrough|standards_lint|clang_format' --output-on-failure

Ahmed: dig deep for chromium, smelt it, and make stainless steel at the smith table.

## Commits

Appended as they land. Rebased onto main at `8c013588` (phases 2, 10, 13 and 14a) on 2026-11-11; the hashes are after it:

- `40b4e5d6` Move the ore table to config/ores/, one yaml file per ore, generated by game_tables; no world changes.
- `2dee53da` Add chromium ore in stone and deepslate, raw chromium and chromium ingots, smelted in the furnace for now, with our own textures; repin the terrain digests the new block numbers moved.
- `84a5dd13` Spawn chromium from config/ores/chromium: z -250 to -40, veins of 6, exactly 4 a chunk, laid last; count veins a chunk in bench_world_map, test its range and rate, and repin the land it changed.
- `d07a6536` Hold the UI's tables under 1.25 MiB in its tests, now that the chromium items took them past 1 MiB, and record plan 005 phase 11 part A's tests and measurements.
- `1ec364ae` Repin the terrain digests after rebasing onto the doors: the chromium blocks' renumbering and chromium's veins, proved again, and list config/ores/ in the repository's structure.
- `19c7ff48` Add alloys: an alloy recipe kind with counted inputs, the smith table's alloy slots that make 10 stainless steel ingots from 9 iron and 1 chromium, saved in a new state section, and stainless steel tools and armour a tier above iron with our own textures; make the smith table from copper so copper tools come before iron.

## Issues found

**Needs phase 6's smith table** for alloying, and its blast furnace for high heat, if Ahmed agrees to one.

**Open for Ahmed:**

1. Do 9 iron + 1 chromium make 10 stainless steel ingots? Default: 10.
2. Which of tin, zinc, nickel and silver to add, and when? Default: none until you say.
3. Chromium's depth and rarity. Default: z -250 to -40, about 4 veins a chunk.


### 2026-11-10, part A: ores in yaml and chromium

Part A is the ores: the yaml, chromium's blocks, items and spawning. The alloy recipe kind, the smith table's slots and stainless steel wait for phase 6, and a later agent does them.

**1. The table is `config/ores/`, one file per ore or rock, and every row of it moved, rock too.** The table in `run/src/terrain/ore/index.c` held the rocks (dirt, gravel, granite, diorite, andesite, tuff) as well as the seven ores, and all 26 rows are one table that is laid down in order, so all of it moved: 13 files. An ore with more than one range of depths (iron has four) lists them under `veins`, each with a `row` name; a file of one row has the fields at the top, as the design above shows. Two things the design did not have were needed to keep every world the same. **`order`**: rows are laid in the table's order, each replacing what the last left, and gravel's two rows are not next to each other, so each row says its place (10 to 260, in steps of 10, as they were). **`row`**: a row's stream is `terrain_rng_salt("ore/<row>")`, so the names are kept exactly (`iron_high`, `gold_deep`, ...). `spread` (`uniform` or `triangle`) and `min_column_height` (emerald's 120) are the rest. `drops` is checked: both blocks' own files must drop it; what mining gives is still decided there.

**2. Generated, not hand-written.** `compile/src/game_tables/ores/` writes `tables/ores/index.{h,c}`: `ore_rows[ORE_ROW_COUNT]` of `ore_row_def_t`, sorted by `order`, with the same 24-byte layout and field names `terrain_ore_row_t` had, which is now a typedef of it. `TERRAIN_ORE_ROWS` is `ORE_ROW_COUNT`, and `terrain_ore_t` is sized from it. It refuses an unknown field, a missing block or item, a drop the blocks do not give, a number out of range, `z_min` not below `z_max`, two rows of one name or one order, and row fields beside `veins`, each at its line: `compile_game_tables` holds 21 such cases, and that a good tree comes out in `order` and not file order.

**3. The move changed no world.** With the table generated from the yaml and nothing else changed, every terrain pin held: `run_terrain_ore` (`ORE_DIGEST_PIN 0x42C989080FAB0EA9`), `run_terrain_density`, `_biome`, `_cave`, `_cave_walk`, `_feature`, `_feature_own`, `_fields`, `_hydrology` and `_seed` all passed unchanged, and `run_terrain_ore`'s row-by-row check of the table against the README too.


**4. Chromium's blocks and items.** `chromium_ore` (hardness 3) and `deepslate_chromium_ore` (4.5), both `tool: pickaxe, tier: iron`, so an iron pickaxe or better gets anything from them, as diamond ore; each drops one `raw_chromium`. Their items place them, as every ore's does. `raw_chromium` and `chromium_ingot` are plain items. `smelt_chromium_ingot` smelts `group_chromium_source` (the two ores and `raw_chromium`, as iron and gold do) into `chromium_ingot` **in the furnace for now**: phase 6 adds the blast furnace, and moves this recipe there (the master's decision: the blast furnace for iron, chromium and alloys). The four textures are our own recipes in `textures/` (stone or deep rock with blue-grey flecks, a lump, a bar), looked at beside iron's before committing; `texture_vector recipes` painted them, `vector.yaml` needs them, and `import_minecraft_default.yaml` keeps them with "Minecraft has no chromium", so a Minecraft-textured build still has them.

**5. The new blocks moved every terrain digest, and the land is the same.** `chromium_ore` sorts after `chest_*` and `deepslate_chromium_ore` after `deepslate`, so every block after either moved by one or two, and seven pins failed, the same seven as phase 12's issue 10: density, biome, feature's generator row and forest, hydrology and its flood, and ore. A temporary back-conversion (each voxel's id turned back into the numbering from before, state bits kept; no chromium is in that land yet, as no ore file names it until the next commit) gave each of the seven its old pin exactly: density `0xA0ADF8098E10E291`, biome `0x7347D2E2EB87F8B2`, feature generator `0xE91D8A079B8A9907`, forest `0xC02077A1494E53C4`, hydrology `0xDDE694659EE1A8CB`, flood `0x5B83274074AD61E1`, ore `0x42C989080FAB0EA9` (all five tests passed with it in, against their pins of before). So they are repinned, each with a "Plan 005 phase 11" line, the generator row's comment keeping its old value, and `TERRAIN_GENERATOR_VERSION` stays 1. The back-conversion is not in the tree. The new numbers: density `0xC2F281D94B183C73`, biome `0x61B0BCF196502DAB`, feature generator `0x1C12ABD3D06BA98A`, forest `0x413E4785BDFC5EB5`, hydrology `0xE64537232F845B1D`, flood `0x67D09CD97B4A3533`, ore `0x65B3F42B566CBB61`. `run_item_*` and `compile_game_tables` passed unchanged.

**6. Chromium spawns: `config/ores/chromium/index.yaml`, the phase's numbers.** z -250 to -40, vein 6, `per_256_chunks: 1024`, uniform, `order: 270`, so it is laid last. An ore replaces only rock and never another ore, so the last row moves no vein of any other row: it only turns stone, granite, diorite, andesite (to `chromium_ore`) and deepslate, tuff (to `deepslate_chromium_ore`) where its own veins fall. **1024 is exactly 4 a chunk**, as 1024 is a whole 4 times 256 (no chunk draws a fifth), and no vein is lost, since all of -250 to -40 is in the world.

**7. The new ore is the only change, proved, and the pins moved for it.** With the file in place but `per_256_chunks: 0`, all seven pins of issue 5 held exactly (biome, density, feature, hydrology passed; ore printed `0x65B3F42B566CBB61`), and only the tests that expect chromium at 1024 failed. So the rows before it are untouched, and at 1024 the pins moved by chromium's veins alone: biome `0x61B0BCF196502DAB` to `0x355633F628196CF5`, feature generator `0x1C12ABD3D06BA98A` to `0x50954B550A408385`, forest `0x413E4785BDFC5EB5` to `0x1DF57BB280481128`, ore `0x65B3F42B566CBB61` to `0x782E6E5ADF9AE17F`. Density and hydrology (its digest and its flood) held, as they stop before the ore stage. Each has a "Plan 005 phase 11" line.

**8. `TERRAIN_GENERATOR_VERSION` stays 1, and the generator row changes: a choice for Ahmed to confirm.** `run/src/terrain/README.md` says any change that gives a seed other blocks raises the version, and a save made on another version is refused with no migration. Raising it would refuse every world Ahmed has saved. The only difference chromium makes is ore inside rock that a save's edits have not touched, which is no seam and no new mountain, so the row was changed instead, as its own comment allows ("changing a row leaves worlds saved on the old land opening on the new"), with the old value in the comment. Old worlds open, and chromium is in any rock they have not dug. If Ahmed would rather keep the rule strict, raise the version to 2 and add a row with this value.

**9. Measured with `bench_world_map`.** It now prints, from its 64 tiles of every stage (16384 chunks), each ore row's veins a chunk by the new `terrain_ore_chunk_veins` (the ore stage's own draws, with no box) and each ore's voxels a chunk in both forms. Debug build, the default seed 5569068473609503046, exit status 0, 2026-11-11: **chromium 4.00 veins a chunk, as the file asks, and 22.0 voxels a chunk, 66% of them `deepslate_chromium_ore`** (against 26 in all-stone columns: caves, water and the other ores take the rest). Beside it: gold 8.00 + 0.50 veins and 73.5 voxels a chunk, diamond 7.13 + 0.11 veins (a triangle reaching below the world loses some) and 29.7 voxels, lapis 74.7, iron 455.5, coal 425.5. So chromium comes out a little rarer than diamond by voxels, deep only: a starting point Ahmed can tune in its file.

**10. Tests.** `run_terrain_ore`: the table typed out again has chromium's row (27 rows); `each_ore_lies_at_its_depths_in_its_own_form` counts chromium too and holds it to -250 to -37 (-40 and a vein's reach of 3); the new `chromium_lies_only_from_minus_250_to_minus_40_at_4_veins_a_chunk` lays 64 chunks of all-stone columns from the bottom of the world to z 100 and checks exactly 4 veins in every chunk, chromium only from -249 (above the bedrock) to -37, some of it above -60, 12 to 36 voxels a chunk (4 veins of about 6), and most in its deepslate form: 256 veins in 64 chunks, 4 in each, 26.0 voxels a chunk, 62% deepslate, z from -249 to -38. The all-stone guide over 256 chunks says 26.2 a chunk, 63.9% deepslate, z -249 to -39, mean -147. "Moving ores to YAML changed no world" is issue 3: every pin held across the move commit.

**11. The four new items took the UI's tables past the 1 MiB its tests held them to.** Every item gets a picture in the HUD's and the game screens' tables, and `run_ui_font` and `run_ui_hud` failed on `arena_tables.used < 1 MiB`: it is now 1056640 bytes of the 1.5 MiB arena (`UI_ARENA_TABLES_BYTES`, unchanged). The two checks now hold it under 1.25 MiB, `run_ui_font` prints the figure, and `run/src/ui/README.md` says it. Phase 2's door items will add to it the same way.

**12. Tests, 2026-11-11, on `5a8adeda` and the fix after it.** `tools/test-or-build.sh cmake --build --preset debug`: exit status 0. `tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_terrain_.*|run_item_.*|compile_game_tables|standards_lint|clang_format)$' --output-on-failure`: all 20 pass (`run_terrain_biome`, `_feature` and `_ore` after their repin, re-run alone). `tools/test-or-build.sh timeout 600 build/debug/bin/bench_world_map /tmp/mc_c_maps`: exit status 0, issue 9's numbers. Wider, for the new blocks and items: `run_save.*`, `run_ui.*`, `run_world.*`, `run_game.*`, `run_player.*`, `run_mob.*`, `run_texture.*`, `compile_texture.*` and `render_.*`, 56 tests: 54 passed, and `run_ui_font` and `run_ui_hud` failed on issue 11's budget, and pass with it. No golden picture changed. `pgrep -a minecraft_c`: nothing.

**13. Rebased onto main after phase 2's doors (2026-11-11), and every pin proved again.** Main had added `wood_door`, `iron_door` and `glass_door` and repinned the same seven pins, so the rebase conflicted only on those pins and their comments (both sets of notes kept) and on `textures/README.md`'s count, now 231 of our own textures. Main's phase 10 and 14a merged cleanly. After the rebase, with the chromium file at `per_256_chunks: 0` and the temporary back-conversion of issue 5, all seven gave main's door-era pins exactly, with no chromium voxel seen: density `0xC2F281D94B183C73`, biome `0xB1B81FB056636BFA`, feature generator `0x92403624016ACD4A`, forest `0xC53099C2759E5522`, hydrology `0xFCD1BA821441882A`, flood `0x67D09CD97B4A3533`, ore `0xE5E11DFC2D901869`. The same run's digests before back-conversion are the numbers with chromium's two blocks added and no chromium laid: density `0x852115BE32F7F98D`, biome `0x3472E7A9C9A42A73`, feature generator `0x0A5E69C7BA33DB77`, forest `0x8881A61AD34A3D27`, hydrology `0x8579B6C6CD735924`, flood `0x807D0BBA1F2B2B1D`, ore `0xB9CCB9501FA218B1`. Density, hydrology and flood are pinned at those values (the doors and the chromium blocks had each moved stone and water by two, which is why density and flood had come out the same on both branches). At 1024, as before, only four moved, by chromium's veins: biome `0xA6E7EB530487F99B`, feature generator `0x963084164D7E4DAE` (the row's comment gives `0x0A5E69C7BA33DB77` for a rate of 0), forest `0x9C3DACA353F60E1C`, ore `0x5D04B9F951A2FCD9`. `TERRAIN_GENERATOR_VERSION` stays 1, as accepted. The UI's tables with the doors' and chromium's items stay under the 1.25 MiB of issue 11 (`run_ui_font` and `run_ui_hud` pass). `plans/REPO_STRUCTURE.md` now lists `config/ores/`.

**14. Tests after the last rebase, 2026-11-11, at `1ec364ae`.** Build: exit status 0. `tools/test-or-build.sh timeout 1500 ctest --preset debug -R '^(run_terrain_.*|run_item_.*|compile_game_tables|standards_lint|clang_format|run_ui_font|run_ui_hud|run_player_interaction_door)$'`: 23 of 23 pass; the UI's tables are 1056640 bytes of 1572864. `tools/test-or-build.sh timeout 600 build/debug/bin/bench_world_map /tmp/mc_c_maps`: exit status 0, chromium 4.00 veins and 22.0 voxels a chunk (66% deep), as in issue 9. Pins between the two rebases were not moved by phase 13, which adds no block. `pgrep -a minecraft_c`: nothing.

### 2026-11-10, part B: alloys and stainless steel

Built on phase 6's stations, rebased on main at `2764ffd8` ("Mark phase 6 done").

**15. An `alloy` recipe kind, with counted inputs.** `config/recipes/<name>/index.yaml` with `kind: alloy`, `inputs: [{ item, count }, ...]` (1 or 2, each a plain item named once, each count up to what one slot of it holds), `gives`, `count`, `station` (only `smithing`, the one station with alloy slots, and its default) and `heat`, optional. `compile/src/game_tables/recipes/` emits `recipe_alloys[RECIPE_ALLOY_ROWS]` (`recipe_alloy_t`, 24 bytes) and `RECIPE_ALLOY_COUNT`; a tree with no alloys has one row of nothing, as a table of no rows does not compile. It refuses each bad input, station and heat at its line, and **two alloys of one station with the same inputs**, whatever their counts or order: slots holding enough for both would match both. `compile_game_tables` has 20 new bad cases and the good tables, one with heat and a tree with none. A recipe matches when each input is in a slot of its own with **at least** its count and nothing else is in the slots, either way round; taking it takes the counts and leaves the rest.

**16. The choice on heat, as the default said: stainless steel is made at the smith table with no lit blast furnace needed.** The iron and the chromium have each been through the blast furnace already. It stays data: `heat: blast_furnace` (or `furnace`) on an alloy makes it need a block of that process burning within 4 blocks of the smith table along every axis (`CRAFTING_ALLOY_HEAT_REACH`). The smith table's screen asks `smelting_lit_near` at every click and every tick it is up, so a furnace going out takes the output away. `alloy_stainless_steel` has no `heat`, and its file says how to add it.

**17. The smith table's alloy slots work.** `item_t.borrowed_crafting_alloy`, a `crafting_alloy_t` (two inputs, the output, the count made, the station, the heat; 40 bytes) from the item arena, in the 48 bytes of padding `item_t` still had, beside the bench grid. Phase 6's three faint slots at texture x 150 are now `UI_SCREEN_ZONE_BLOCK`'s slots 0 to 2 of the smith table's screen (`ui_screen_smithing_places`, `_view`, `_click`): an input takes the usual two clicks, the output gives one making onto the pointer (or none, if the pointer cannot take all ten), shift on the output makes all it can into the inventory, shift on an input sends it to the inventory. Closing the screen gives the inputs back as the grid's cells are, and dying spills them with the grids. 9 iron + 1 chromium make **10** stainless steel ingots (Ahmed took 10).

**18. Saved: save state version 6, with an alloy section.** `SAVE_STATE_SECTION_ALLOY` (15) holds the `crafting_alloy_t`, written with the items; its output, recipe and heat are written empty and worked out on opening from this build's recipes, as a grid's are. It is refused unless it is the smith table's and holds stacks that can be, and `translate/` translates its two stacks. **`SAVE_STATE_VERSION` is now 6**, as an older reader would refuse the new section; versions 3 to 6 are read, and a file with no alloy section, as every one before, opens with the slots empty. Phase 6 kept version 5 by sharing the bench grid; the slots could not be squeezed into `save_state_items_t` (no padding left), so a section of their own was the clean way.

**19. Stainless steel tools and armour, a tier above iron.** `config/tiers/stainless_steel/`: harvests what iron does (diamond's blocks stay diamond's, so obsidian still needs a diamond pickaxe, which is off), speed 7000 to iron's 6000, axe 1.0 and hoe 3.5 swings a second. Tools: durability 500 to iron's 250, damage iron's plus a half (pickaxe 4.5, sword 6.5, shovel 5; axe 9 and hoe 1 as iron). Armour: helmet 2 points, chestplate 7, leggings 5, boots 2, each with 1 toughness, durability 1.6 times iron's (264, 384, 360, 312). **The numbers are mine, a starting point to tune.** Each recipe is iron's pattern in stainless steel ingots at the smith table, the armour round its leather piece, as phase 6's metal armour is. Ten new items, no new block: nothing in terrain moved, and the block count stays 164 of 170. The item tier enum renumbered (`stone` and `wood` moved up one), which nothing saves.

**20. Ten textures, our own recipes.** `textures/items/stainless_steel_{ingot,pickaxe,sword,axe,shovel,hoe,helmet,chestplate,leggings,boots}`: copper's shapes and chromium's bar in a cool grey steel palette, painted by `texture_vector recipes`, each in `vector.yaml`'s needs and in `import_minecraft_default.yaml` as `keep` ("Minecraft has no stainless steel"). Looked at in the new golden. `textures/README.md` now counts 284 of our own (it said 231, from before phase 6's 43).

**21. Phase 6's ordering problem is fixed: the smith table is made from copper.** `config/recipes/smith_table/index.yaml` now has `I: copper_ingot` (was `iron_ingot`), so copper and gold tools come before iron, as phase 6's proposal said (its issue 4). The walk in `run_item_crafting_station` now holds it: smithing is reached after the furnace (not after the blast furnace), and it prints `rounds: hands 1, table 2, woodworking 3, stoneworking 6, furnace 7, blast furnace 9, smithing 9, tanning 5, leatherworking 5; 12 in all` and `pickaxes: copper round 9, gold 9, iron 10, stainless steel 11`, checking copper and gold before iron and iron before stainless steel. The walk now records each recipe's round and walks the alloys too (an alloy once its station is there, its inputs are had and its heat if it names one), and every alloy must be reached. `config/recipes/README.md`'s order is rewritten to match.

**22. Tests.** New `run_item_crafting_alloy`: 9 iron and 1 chromium make 10 in either slot, taking exactly those, the rest staying; a full pointer takes nothing; shift-click makes 50 from 64 iron and 5 chromium and closing gives the 19 iron back; 8 iron and 1 chromium make nothing either way, nor 9 chromium and 1 iron, nor either alone, and over every pair of items at full stacks only iron with chromium makes stainless steel, at the smith table only, and no grid or furnace makes it; heat: none needed, and `smelting_lit_near` sees a lit blast furnace at 4 and not a furnace at 5; each stainless piece is iron's pattern at the smith table only; the tier is above iron. `run_ui_screens`: an alloy replay (`fixtures/alloy.script`, golden `alloy_ready.png`) clicks 9 iron and 1 chromium in and takes 10 stainless steel out, and closing gives back what is left. `run_save_state`: the alloy slots (2 chromium, 9 iron) reload byte for byte and make 10 again, and three forged numbers in the section are refused. `run_item_equipment` counts 24 armour pieces (was 20) and `run_item_crafting_station`'s no-armour walk 20 (was 16).

**23. Pictures.** `smithing_ready.png` changed: the three alloy slots are now ordinary slots, no longer dimmed. New `alloy_ready.png`: 9 iron and 1 chromium in the inputs, 10 stainless steel in the output, the pointer's tooltip on the chromium. Both made with `MC_GOLDEN_WRITE=1` and looked at. The UI's tables are 1098240 bytes with the ten new item pictures, under the 1.25 MiB budget (`run_ui_font`, `run_ui_hud` pass); no budget raised.

**24. Also changed:** `plans/REPO_STRUCTURE.md` lists `tests/run/item/crafting/alloy/`.

**25. The random clicks of `run_ui_screens` cover the smith table too:** its 49 places (the grid, its output and the three alloy slots) are in the layout test, and 20000 random clicks on its screen lose no item from the grid, the alloy inputs or anywhere else. The alloy output is left out, as a grid's output is: it makes new items.

**26. Tests, 2026-11-11, on `19c7ff48` and the random clicks after it.** `tools/test-or-build.sh cmake --build --preset debug`: exit status 0. The whole debug suite, `tools/test-or-build.sh timeout 2700 ctest --preset debug -I 1,85`, then `-I 86,400` after the random-clicks change: 85 of 85 and 77 of 77 pass, `run_playthrough` (94.2 s), `run_ui_screens`, `run_save_state`, `run_save_state_game`, `_names` and `_calendar`, every `run_item_*`, `run_terrain_*`, `compile_game_tables`, `standards_lint` and `clang_format` among them. `tools/test-or-build.sh timeout 600 build/debug/bin/bench_world_map /tmp/mc_c_maps`: exit status 0, chromium still 4.00 veins and 22.0 voxels a chunk. No terrain pin moved: no block was added. `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c`: nothing.
