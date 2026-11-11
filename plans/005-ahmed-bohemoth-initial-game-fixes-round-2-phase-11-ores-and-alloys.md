# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 11: Ores and alloys

**Status:** part A in progress: ores in yaml (updated 2026-11-10)

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
- [ ] Add `chromium_ore` and `deepslate_chromium_ore` blocks, `raw_chromium` and `chromium_ingot` items, and their textures as our recipes
- [ ] Add a `chromium` ore file with the spawn numbers above, and measure how many veins a chunk gets with `bench_world_map`
- [ ] Add an `alloy` recipe kind with counted inputs, in `compile/src/game_tables/recipes/` and `config/recipes/README.md`
- [ ] Add alloy slots to the smith table screen from phase 6, and make stainless steel from 9 iron and 1 chromium
- [ ] Add stainless steel tools and armour, a tier above iron. Armour comes from leather armour, as phase 6 says
- [ ] Add tests: chromium spawns only in its z range and at about its rate; 9 iron and 1 chromium make stainless steel, and nothing else does; a stack of 8 iron makes nothing; moving ores to YAML changed no world

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_terrain_.*|run_item_.*|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 600 build/debug/bin/bench_world_map /tmp/mc_c_maps
    pgrep -a minecraft_c    # must print nothing

Ahmed: dig deep for chromium, smelt it, and make stainless steel at the smith table.

## Commits

Appended as they land:

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

