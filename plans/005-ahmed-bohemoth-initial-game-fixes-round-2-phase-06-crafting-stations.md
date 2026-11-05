# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 6: Crafting stations

**Status:** blocked: waiting for Ahmed's research into Hypixel's crafting, and his choice of how it works   (updated 2026-11-05)

## Goal

Each kind of crafting has its own bench: all woodworking on the woodworking bench, all stoneworking on a new stoneworking bench, and the crafting table only for making benches. You start with nothing, and can reach each bench in a clear order.

**Ahmed's words:**

- "I can not create a stick from 2 logs on the wood bench???? the wood bench has interception with the crafting table please add all wood working stuff on the woodworking bench!!"
- "can't convert logs to planks also! planks to sticks!"
- "we also need a stone-working bench for stone pick-axes and tools etc..."
- "crafting table should only make the other benches I think very similar to hypixel but I need to properly research and implement these mechanics!"
- Later: "we also need to make a stoneworking bench, and an iron-working bench please. wooden tools, stone tools, iron tools. perhaps even a iron smith table that takes up 2x2x1 that I can use to smelt iron any metal ores, make any metal tools from here, I can only make armour from here too"

## Stations Ahmed has asked for

| Station | Size | Makes |
|---|---|---|
| Crafting table | 1×1×1 | Only the other stations |
| Woodworking bench | 2×1×1 | Everything wooden, including wooden tools |
| Stoneworking bench | 2×1×1 | Everything stone, including stone tools |
| Iron-working bench | 2×1×1 | Iron tools |
| Smith table ("perhaps") | **2×2×1** | Smelts any metal ore, makes any metal tool, and is **the only place armour is made** |

**Open for Ahmed:** the iron-working bench and the smith table both make iron tools. Is the smith table the iron-working bench grown up, so one station, or are both wanted? The default is **one station, the smith table**, because two stations making the same thing is the overlap he disliked between the table and the woodworking bench.

**Default reading, until Ahmed says otherwise:**

- "Any metal": iron, gold and copper. Diamond isn't a metal, so diamond tools also go to the smith table until Ahmed says otherwise.
- "Armour only here": **all** armour, leather included.
- "Smelt any metal ore": the furnace stops taking metal ores, and keeps food, glass, stone and charcoal.

**2×2×1 is new:** the bench model is 2 voxels along x. The smith table is four voxels, 2 along x and 2 along y: four blocks, placed and broken together. It faces no way, as blocks have no facing (phase 2's question).

## Why it happens

Plan 003 phase 4 moved only part of the woodworking. It kept planks, sticks and the crafting table off the bench, because you need those to get a bench in the first place:

| Recipe | Where today |
|---|---|
| Logs → planks (`oak_planks` etc.) | Hands or table |
| Planks → sticks (`stick`) | Hands or table |
| Crafting table (4 planks) | Hands |
| Woodworking bench (3 planks over 2 sticks) | Table |
| Wood stairs, slab, door, sign, ladder, chest, boat, bowl, wooden tools | **Bench only** (20 recipes) |
| Stone tools, cobblestone stairs, furnace, torch and everything else | Table |

So the bench can't make planks or sticks, both stations feel like they overlap, and there's no stoneworking bench. **No recipe makes sticks from logs anywhere.**

## The order you'd unlock things

If planks and sticks go only on the bench, something has to come before it, or a new world can never get one. **This is a proposal, for Ahmed to change after his research:**

1. **Hands (2×2):** 4 logs → crafting table. Nothing else.
2. **Crafting table:** makes benches only. Logs → woodworking bench. Cobblestone with a little wood → stoneworking bench.
3. **Woodworking bench:** logs → planks, planks → sticks, 2 logs → sticks (Ahmed asked for this one), and everything else made only of wood, including wooden tools.
4. **Stoneworking bench:** stone tools, cobblestone stairs and slabs, stone bricks, furnace.
5. **Smith table (2×2×1),** made on the crafting table from iron and stone: smelts metal ores, makes every metal tool and every piece of armour.
6. **Mixed materials,** such as torches, beds and bows: which station each goes to is Ahmed's call. Until then the table keeps them, which breaks "the crafting table only makes benches" for now.

## Todo

- [ ] **Ahmed:** finish the Hypixel research, and confirm or change the order above, including where mixed materials go
- [ ] Add a `stoneworking` station to `compile/src/game_tables/recipes/` and `config/recipes/README.md`, with its grid in `run/src/item/`, saved in `run/src/save/state/`
- [ ] Add a stoneworking bench, two voxels long like the woodworking bench: blocks, item, textures, screen, placing and breaking
- [ ] Add the smith table: four blocks in 2×2×1, placed only where all four cells are free, broken together dropping one. Its screen has a crafting grid and a smelting slot with fuel, as the furnace does
- [ ] Add a `smithing` station: every metal tool and every piece of armour moves there. Metal ores move from the furnace to the smith table's smelting
- [ ] Add a test that armour can be made at the smith table and nowhere else
- [ ] Move each recipe to its station as decided, and add the new ones: crafting table from 4 logs, woodworking bench from logs, sticks from logs
- [ ] Add a test that **every** recipe is reachable from a world with only logs and cobblestone, by walking the stations in order. So no recipe can be stranded, and no station is circular
- [ ] Add a test that each recipe works on its own station and nowhere else, and that nothing is made in two places, so stations don't overlap
- [ ] Update `run_playthrough` and its scripts, `tests/run/player/felling/fixtures/fell.script`, and any test that crafts
- [ ] Update `config/recipes/README.md` with the order, so it reads as one page

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_item_crafting.*|run_ui_screens|run_player_interaction.*|run_playthrough|run_save_state|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: start a new world with nothing, and work up through each bench to a stone pickaxe.

## Commits

Appended as they land:

## Issues found

**Blocked on purpose.** Ahmed wants to research Hypixel's crafting before deciding: "I need to properly research Hypixel mechanics before I so bluntly speak on it!!". So this phase doesn't start until he confirms the order.

**This changes the early game a lot.** Every test and script that crafts anything will need updating.
