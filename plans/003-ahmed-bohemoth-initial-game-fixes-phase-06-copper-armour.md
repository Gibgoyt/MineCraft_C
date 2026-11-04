# 003-ahmed-bohemoth-initial-game-fixes — phase 6: Copper armour

**Status:** not started   (updated 2026-11-04)

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

- [ ] Add `config/items/copper_{helmet,chestplate,leggings,boots}/index.yaml`, with the stats above and `stack: 1`
- [ ] Add `config/recipes/copper_{helmet,chestplate,leggings,boots}/index.yaml`, with the same patterns as iron and key `copper_ingot`
- [ ] Add `textures/items/copper_*/` as our own procedural recipes, copying each `iron_*` drawing with a copper palette
- [ ] Add a test in `tests/run/item/crafting/` that each pattern of copper ingots makes its piece on the table
- [ ] Add a test in `tests/run/item/equipment/` that each piece goes in its own slot only, gives its points, and wears out
- [ ] Update the pinned item or texture counts only if a test holds them, with a line saying why

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_item_crafting|run_item_equipment|run_save_state|compile_game_tables|compile_texture_vector|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: smelt copper, make all four pieces on a crafting table, wear them, and check the pictures look right.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**Needs phase 1.** New items shift item ids, as new blocks do, so phase 1 goes first or saved worlds may be refused or misread. Phase 1's first todo checks the items half of this.

**Open for Ahmed: copper tools too?** You asked for armour, so that's all this covers. There are no copper tools either, and adding a tool tier needs a code change, as tiers are a fixed list (`wood stone iron diamond gold`, in `config/items/README.md`). `copper_ingot` will still have no other use.
