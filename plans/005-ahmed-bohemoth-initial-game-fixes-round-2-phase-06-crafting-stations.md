# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 6: Crafting stations

**Status:** blocked: waiting for Ahmed's research into Hypixel's crafting, and his answers below   (updated 2026-11-06)

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

- [ ] **Ahmed:** confirm the order, the blast furnace, and where mixed things go: torches, beds, bows
- [ ] Add stations to `compile/src/game_tables/recipes/` and `config/recipes/README.md`: `woodworking`, `stoneworking`, `smithing`, `leatherworking`, plus tanning as a timed process like smelting
- [ ] Add the stoneworking bench, 2×1×1, like the woodworking bench
- [ ] Add the smith table: four blocks in 2×2×1, placed only where all four cells are free, broken together dropping one. Its screen has a crafting grid and alloying slots (phase 11)
- [ ] Add the tanning rack: hides in, leather out over time, saved with the world
- [ ] Add the leather workbench: leather armour and other leather goods
- [ ] Add the blast furnace, if Ahmed agrees: like the furnace, for high-heat ores
- [ ] Move every recipe to its station: all wood to woodworking, all stone to stoneworking, metal tools and metal armour to smithing, leather armour to leatherworking, high-heat ores to the blast furnace. The crafting table keeps only stations
- [ ] Add copper tools, and a copper tier in `config/items/README.md`'s tool tiers. Today it's a fixed list in code (`wood stone iron diamond gold`)
- [ ] Turn off diamond tool and armour recipes, saying why in their YAML
- [ ] Add a test that **every** recipe is reachable from logs and cobblestone, walking the stations in order, with nothing circular
- [ ] Add a test that each recipe works on its own station only, and nothing is made in two places
- [ ] Add a test that armour can't be made without the leather workbench in the chain
- [ ] Update `run_playthrough` and every script and test that crafts

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_item_.*|run_ui_screens|run_player_interaction.*|run_playthrough|run_save_state|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: start a new world with nothing, and work up through each station to iron armour.

## Commits

Appended as they land:

## Issues found

**Blocked on purpose.** Ahmed is researching Hypixel and Hytale before deciding, so this doesn't start until he confirms the order.

**Needs phases 4 and 11:** hides come from animals, and alloys and chromium from ores.

**This changes the whole early game.** Every test and script that crafts will need updating.

**Open for Ahmed:**

1. A blast furnace for iron, chromium and alloys, with the furnace for copper, gold, food and glass? Default: yes.
2. Sizes for the tanning rack, leather workbench and blast furnace? Default: 2×1×1, 2×1×1 and 1×1×1.
3. How much metal makes each metal armour piece from its leather one? Default: Minecraft's ingot count for that piece.
