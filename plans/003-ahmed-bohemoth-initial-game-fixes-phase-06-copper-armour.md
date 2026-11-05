# 003-ahmed-bohemoth-initial-game-fixes — phase 6: Copper armour

**Status:** in progress: everything an agent can do is done and checked, the whole debug suite 110 of 110 in one run, and copper played through the built program (issue 7). Only Ahmed's own check is left   (updated 2026-11-05)

## Goal

Copper ingots can be crafted into a helmet, chestplate, leggings and boots on the crafting table, and that armour can be worn, protects, wears out and is saved like any other.

**Ahmed's words:** "armour with copper does not work i can not make a helmet or any armour please let's fix that!"

## Why it happens

**Copper armour was never added.** This isn't a broken recipe:

- `config/items/` has leather, gold, iron and diamond armour, and no copper.
- `config/recipes/` has no copper armour recipe, and no copper recipe of any kind apart from smelting. `copper_ingot` is used by nothing, so smelting it leads nowhere.
- `textures/items/` has no copper armour pictures.

No game code needs changing. Armour is read from each item's YAML, and `equipment_armor_slot_of` (`run/src/item/equipment/index.c:171`) takes any item with an `armor:` field.

## Stats

**These numbers are proposed, not settled.** They're Ahmed's to change, each in one line of YAML. Copper sits just above gold on durability, a good way below iron, and just below gold on protection.

| Piece | Leather | Gold | **Copper** | Iron |
|---|---|---|---|---|
| Helmet | 1 pt, 55 uses | 2 pts, 77 | **2 pts, 121** | 2 pts, 165 |
| Chestplate | 3 pts, 80 | 5 pts, 112 | **4 pts, 176** | 6 pts, 240 |
| Leggings | 2 pts, 75 | 3 pts, 105 | **3 pts, 165** | 5 pts, 225 |
| Boots | 1 pt, 65 | 1 pt, 91 | **1 pt, 143** | 2 pts, 195 |
| **Total points** | 7 | 11 | **10** | 15 |

## Todo

- [x] Add `config/items/copper_{helmet,chestplate,leggings,boots}/index.yaml`, with the stats above and `stack: 1`
- [x] Add `config/recipes/copper_{helmet,chestplate,leggings,boots}/index.yaml`, with the same patterns as iron and key `copper_ingot`
- [x] Add `textures/items/copper_*/` as our own procedural recipes, copying each `iron_*` drawing with a copper palette
- [x] Add a test in `tests/run/item/crafting/` that each pattern of copper ingots makes its piece on the table
- [x] Add a test in `tests/run/item/equipment/` that each piece goes in its own slot only, gives its points, and wears out
- [x] Update the pinned item or texture counts only if a test holds them, with a line saying why

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_item_crafting|run_item_crafting_copper|run_item_equipment|run_item_equipment_copper|run_save_state|compile_game_tables|compile_texture_vector|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

- [ ] Ahmed: smelt copper, make all four pieces on a crafting table, wear them, and check the pictures look right.

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --windowed

Results, 2026-11-05, debug preset:

- All 9 tests above passed, with `run_item_crafting_copper` and `run_item_equipment_copper` added to the list. `pgrep -a minecraft_c` printed nothing.
- `build/debug/bin/tools/texture_vector check --textures textures --materials textures/vector.yaml`: 325 from the pack, 192 from recipes, 471 needed and all there.
- The tests catch bad data. With the copper helmet at 3 points and the boots keyed on `iron_nugget`, both copper tests failed: `table.recipe != CRAFTING_NO_RECIPE`, and `armor_points 3 != 2`. Restored afterwards.
- The pictures were drawn in a sheet next to iron and gold at 12 times size, and they have the same shapes in a copper colour. Ahmed still judges them in the game.

Gate check, in a clean build of this branch with main merged:

| Check | Result |
|---|---|
| The Verification commands above | 9 of 9 |
| Whole debug suite, `ctest --preset debug -j1`, in one run: issue 2 no longer stands | 110 of 110, in 1725 s |
| `run_item_crafting_copper`, `run_item_equipment_copper`, sanitize preset | 2 of 2 |
| The built program played by script, debug and release: issue 7 | Smelted, all four made, worn, protecting, wearing out, saved and reopened |
| `pgrep -af "$PWD/build"` after the last run | Nothing of this worktree's |

## Commits

Appended as they land:

- `4d6d763` Add copper armour: four items with the stats of plan 003 phase 6, the same patterns as iron in copper ingots, and our own pictures.
- `a0f022c` Test copper armour: each pattern on the table, each piece in its own slot with its points and wear, and a worn piece saved and loaded.
- `522fddc` List the two copper armour test directories in the repository structure.
- `3c52c92` Name the items in the HUD golden pictures, as the nth item that wears moved when copper armour came before diamond in the table.
- `fd77417` Record copper armour in its plan file: every box an agent can tick, the results, the HUD pictures it moved, and that only Ahmed is left.
- `bb4f397` Make the copper crafting test say what a missing recipe made, as it read the recipe table at row UINT32_MAX when a pattern made nothing.
- `533409e` Record the gate check of copper armour in its plan file: the built program played by script, the Verification command that missed its own tests, the whole suite in one run, and that only Ahmed is left.

## Issues found

**Needs phase 1.** New items shift item ids, as new blocks do, so phase 1 goes first or saved worlds may be refused or misread. Phase 1's first todo checks the items half of this.

**Open for Ahmed: copper tools too?** You asked for armour, so that's all this covers. There are no copper tools either, and adding a tool tier needs a code change, as tiers are a fixed list (`wood stone iron diamond gold`, in `config/items/README.md`). `copper_ingot` will still have no other use.

**Decided by the plan's defaults, as Ahmed is away.** Stats as proposed in the table above, and no copper tools. Both remain his to change.

**1. Copper armour broke the HUD's golden pictures.** Expected: `run_ui_hud` to pass, as nothing in the HUD changed. Actual: `tests/run/ui/hud/index.c:778: golden_pictures_match: CHECK failed: wrong == 0u: 748 != 0`, and the same for 187 pixels, in 5 pictures. Cause: `state_golden_full` chose its hotbar with `item_that_wears(n)`, the nth wearing item by id. Ids are in name order, so the four copper pieces landed between `bow` and `diamond_axe` and moved n 4 and 8 onto other items. Fixed in the test, the one wiring outside this phase's list: it now names the items the pictures were drawn with, `ITEM_ID_BOW`, `ITEM_ID_DIAMOND_HELMET` and `ITEM_ID_DIAMOND_SHOVEL`. The fixtures are unchanged and pass, which also shows the drawing did not change, and no later item can shift them. The phase's Verification list does not include `run_ui_hud`, so only the whole suite found this.

**2. The whole suite was run in pieces.** Expected: one run of 110 tests. Actual: my own `timeout 1700` stopped ctest at test 90, `rc=124`, with tests 1 to 89 passed and none failed. So 90 to 110 were run after it with `ctest -I 90,110`: 20 passed and `run_ui_hud` failed, which is issue 1. After that fix `run_ui_hud` was rerun alone and passed. No one run of 110 exists: the suite takes more than 28 minutes here.

**3. Not played in the built program.** Crafting, wearing and wear are tested through `run/src/item/` on real tables, and a worn copper chestplate through `run_save_state`. What the running game does with armour is covered for iron in `run_item_equipment`, and copper takes the same code, as only `armor:` in the YAML says what it is. No script drove copper through the real program. Ahmed's check above covers that.

**4. The work was already in this worktree, uncommitted.** Expected: a clean tree after the merge. Actual: copper's items, recipes, pictures and tests were sitting uncommitted, dated about half an hour before this run's agent started, most likely from an earlier interrupted attempt. Nothing was thrown away: all of it was read against this plan, built and tested as above, and then committed.

**5. Gate check: the phase's own Verification did not run its own tests.** Expected: the command above runs the copper tests. Actual: its `-R` listed the old names, anchored, so it ran 7 and neither `run_item_crafting_copper` nor `run_item_equipment_copper`. The results said they were "added to the list", and only there. Fixed: both are in the command.

**6. Gate check: the crafting test read past its table when a recipe was gone.** Expected: a pattern that makes nothing fails with a message. Actual: three lines read `recipe_defs[crafting_match(...)]`, and `CRAFTING_NO_RECIPE` is `UINT32_MAX`, a read far past the table. Only a broken recipe gets there, so it never showed while the data was right. Fixed in `bb4f397` with a helper, `cc_makes`. With the boots keyed on `iron_nugget` and the leggings on `slot: feet`, both tests failed with messages, such as `cc_makes(cells, CRAFTING_SIDE_TABLE) == piece->item: 0 != 50` and `equipment_armor_slot_of(piece->item) == slot: 3 != 2`, and did not crash. Data restored. Both pass under the sanitize preset. Nothing was wrong in the game.

**7. Gate check: the built program, played by script, closes issue 3.** Expected: Ahmed's words hold in `minecraft_c --headless --menu`. Done, at 640x480, seed 36: the menu made and saved a world. Only then was its state file edited, by a script outside the repo, as in phase 2's issue 10: a furnace, a table, 24 raw copper and 3 coal in the hotbar, with the books and hashes put right. Everything after that was real keys and mouse:

| What | Result |
|---|---|
| Smelted on a placed furnace | Report: "24 items smelted, 3 of fuel burned"; picture: 24 ingots in the output |
| All four on a placed table | Pictures: each pattern, then its piece on the output, with "Copper Helmet, 121 of 121 uses", and 176, 165 and 143 for the others |
| Worn three ways | Helmet by the right button in hand, chestplate by shift-click, leggings and boots by the pointer. Leggings on the head were refused, staying on the pointer. Report: "10 armor points worn" |
| Saved and reopened | All four in the state file with their wear, and 10 points after reopening |
| Protects: the same night of 1600 ticks, in copper and with nothing on, 4 zombie blows of 3 each | Health 14.91 against 10.83. The difference, 4.08, is exactly what the formula gives: 4 x (3 - 1.98) |
| Wears | 4 blows, wear 4 on every piece; the wear bars show in the inventory |
| Wears out | Helmet at 120 of 121 and boots at 142 of 143: 2 more blows, "7 armor points worn... 2 pieces of armor have broken", their slots empty, chestplate and leggings worn on by 2 |
| Dying | All 4 spilled on the ground, by the drops in the state file. 13 blows to die in copper, 8 with nothing on |
| Copper from ore | A placed `copper_ore` mined with a stone pickaxe dropped 5 `raw_copper`, by the state file |
| Release | The same smelt, craft and wear script gives the same report, and `inventory_worn.png` byte for byte the same as debug's |

Debug: heap "0 bytes live" at each exit. Also checked: each picture's rows are the same as iron's, with a copper palette; `texture_vector recipes` on a copy of `textures/` changed 0 files. Not in the repo: the scripts were written for this one check, and no test drives copper through the built program. The in-game code is the one iron's tests in `run_item_equipment` hold, so no new test was added. How the pictures look stays Ahmed's.

**8. Gate check, out of scope: the world gives items away by itself.** Seen in the same report as phase 2's issue 13: 119 items dropped in 5251 ticks with nothing mined, on seed 36. Left alone, as it's already written down there.
