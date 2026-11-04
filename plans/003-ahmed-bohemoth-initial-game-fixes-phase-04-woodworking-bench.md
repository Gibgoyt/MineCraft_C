# 003-ahmed-bohemoth-initial-game-fixes — phase 4: Woodworking bench

**Status:** not started   (updated 2026-11-04)

## Goal

A woodworking bench exists: a block 2 long on x, 1 on y and 1 on z, with its own crafting screen. All woodworking happens on it, and the crafting table stays for everything else.

**Ahmed's words:** "keep the crafting table, but please create a wood-working bench so that all wood working is done on this bench", and "benches take up 2x1x1 (i.e. x,y,z) space".

## Direction this starts

Ahmed wants a Hypixel and Minecraft hybrid, and will shape it as he researches and plays Hypixel: "as I research hypixel and play it myself i will create a proper game here". This bench is the first step. Nothing else from Hypixel is designed here. The one thing built with more benches in mind is the `station` field below, since "benches" came up in the plural.

## Design

### A block two voxels long

Voxels hold only an id, so the bench is two blocks: `woodworking_bench_west` and `woodworking_bench_east`.

- **Always along x**, west half at the cell you place into and east half at `x + 1`, because blocks have no facing.
- **Placing:** both cells must take it, and the player must not be in either. Otherwise nothing is placed and the item is kept. A neighbouring column that hasn't loaded also refuses.
- **Breaking:** mining either half removes both and drops one bench. The same goes for an explosion.
- **Using:** the right button on either half opens the bench screen.

### Recipes say where they're made

Add one line to a recipe: `station: woodworking`. No line means the table, as now.

- `crafting_match` (`run/src/item/crafting/index.c:59`) takes the station and skips other stations' recipes.
- The 2×2 hand grid stays table-class, so hand recipes still work.
- A new station later is one new word in `config/recipes/README.md` and one more grid.

### What moves to the bench

Rule: **anything made only of wood moves. The exceptions are what you need to get a bench.**

| Where | Recipes |
|---|---|
| **Hands or table (bootstrap)** | All `*_planks`, `stick`, `crafting_table`, `woodworking_bench` |
| **Bench** | All `*_stairs` of wood, `oak_slab`, `wood_door`, `oak_sign`, `ladder`, `chest`, `boat`, `bowl`, `wood_axe`, `wood_hoe`, `wood_pickaxe`, `wood_shovel`, `wood_sword` |
| **Table, as now** | Anything with another material: `stone_*`, `iron_*`, `torch`, `bow`, `arrow`, `bed`, `cobblestone_stairs` |

This is my reading of "all wood working". Each recipe is one line to change.

**Getting a bench:** logs → planks → table → sticks → bench. **The recipe is invented:** 3 planks over 2 sticks (`PPP` / `S S`, `group_planks`, `stick`), made on the crafting table. Note that this makes wooden tools need a bench before your first pickaxe. That's a real change to the early game.

### Screen

Reuse `run/src/ui/screen_crafting/` with a different title and grid, and add `UI_SCREEN_WOODWORKING`. Grid contents return to the inventory on closing, as on the table.

## Todo

- [ ] Add `config/blocks/woodworking_bench_{west,east}/index.yaml` and `config/items/woodworking_bench/index.yaml`
- [ ] Add textures in `textures/blocks/woodworking_bench_*/` as our own procedural recipes, with a top that reads as one long bench across both halves
- [ ] Add a `station` field in `compile/src/game_tables/recipes/`, and document it in `config/recipes/README.md`
- [ ] Pass the station through `crafting_match` and `crafting_grid_start` in `run/src/item/crafting/`
- [ ] Add a `crafting_bench` grid to `item_t` in `run/src/item/index.c`, and save it in `run/src/save/state/`
- [ ] Add `station: woodworking` to the recipes in the table above, and add `config/recipes/woodworking_bench/`
- [ ] Add two-voxel placing to `interaction_place` in `run/src/player/interaction/`, checking both cells
- [ ] Add breaking both halves to `interaction_mine` and `run/src/entity/explosion/`, dropping one bench and leaving no half behind
- [ ] Make either half open the screen, in `ui_screen_kind_of_block` (`run/src/ui/index.c:206`)
- [ ] Update `tests/run/playthrough/` scripts, which craft a wooden pickaxe at the table and now need a bench
- [ ] Add tests: each wood recipe makes nothing on the table and works on the bench, and each table recipe makes nothing on the bench. Placing is refused when either cell is blocked. Breaking either half drops exactly one. An explosion leaves no half
- [ ] Add a scripted headless proof: craft a bench, place it, open it, make stairs, and check screenshots by eye
- [ ] Update `plans/REPO_STRUCTURE.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_item_crafting|run_player_interaction|run_ui_screens|run_entity.*|run_playthrough|run_save_state|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: make a bench from a fresh world, place it, make stairs on it, confirm the table no longer makes them, then break either half.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**Open for Ahmed:**

1. Should wooden tools really move to the bench? I've moved them because they're made only of wood, but it adds a step before your first pickaxe.
2. Is the recipe (3 planks over 2 sticks) what you want?
3. "Always along x" means you can't turn a bench. Turning it needs four more blocks, or real block facing, which ladders and stairs could use too.
