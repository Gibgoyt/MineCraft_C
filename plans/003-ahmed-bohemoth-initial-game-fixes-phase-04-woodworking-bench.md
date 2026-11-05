# 003-ahmed-bohemoth-initial-game-fixes — phase 4: Woodworking bench

**Status:** in progress: everything an agent can do is done and verified, the whole suite 113 of 113. Only Ahmed's own check is left, in the last todo   (updated 2026-11-05)

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

- [x] Add `config/blocks/woodworking_bench_{west,east}/index.yaml` and `config/items/woodworking_bench/index.yaml`
- [x] Add textures in `textures/blocks/woodworking_bench_*/` as our own procedural recipes, with a top that reads as one long bench across both halves
- [x] Add a `station` field in `compile/src/game_tables/recipes/`, and document it in `config/recipes/README.md`
- [x] Pass the station through `crafting_match` and `crafting_grid_start` in `run/src/item/crafting/`
- [x] Add a `crafting_bench` grid to `item_t` in `run/src/item/index.c`, and save it in `run/src/save/state/`. It is `item_t.borrowed_crafting_bench`, a pointer into the item arena: see issue 1
- [x] Add `station: woodworking` to the recipes in the table above, and add `config/recipes/woodworking_bench/`
- [x] Add two-voxel placing to `interaction_place` in `run/src/player/interaction/`, checking both cells
- [x] Add breaking both halves to `interaction_mine` and `run/src/entity/explosion/`, dropping one bench and leaving no half behind
- [x] Make either half open the screen, in `ui_screen_kind_of_block` (`run/src/ui/index.c:206`)
- [x] Update `tests/run/playthrough/` scripts, which craft a wooden pickaxe at the table and now need a bench
- [x] Add tests: each wood recipe makes nothing on the table and works on the bench, and each table recipe makes nothing on the bench. Placing is refused when either cell is blocked. Breaking either half drops exactly one. An explosion leaves no half
- [x] Add a scripted headless proof: craft a bench, place it, open it, make stairs, and check screenshots by eye
- [x] Update `plans/REPO_STRUCTURE.md`
- [ ] Ahmed: make a bench from a fresh world, place it, make stairs on it, confirm the table no longer makes them, then break either half

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 590 ctest --preset debug -R 'run_item_crafting|run_player_interaction|run_ui_screens|run_mob_creeper|run_entity|run_playthrough|run_save_state|compile_game_tables|standards_lint|clang_format' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: make a bench from a fresh world, place it, make stairs on it, confirm the table no longer makes them, then break either half.

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --windowed

Results:

| Check | Result |
|---|---|
| The commands above, debug, on the final code | 21 of 21 in 281 s, `run_playthrough` 82 s |
| Whole debug suite, `-j1`, on the code of `9ba8d07` | 113 of 113 in 1868 s |
| Release: `minecraft_c` built and linked, which is where the frame limit is checked for it | Linked. The release program was not played: the scripts ran on debug |
| Lines broken on purpose (issue 6) | All caught |
| `pgrep -a minecraft_c` after the last run | Printed nothing |

## Commits

Appended as they land:

- `9ba8d07` Add a woodworking bench two voxels long along x, with its own screen and grid, a station on each recipe so that everything made only of wood but what it takes to get a bench moves to it, breaking by either half and by a blast that takes both and gives one, and a played proof through the built program.
- `e9a78e9` List the bench module and its two tests in the repository structure.
- `8031328` Record phase 4 of the game fixes in its plan file: every box an agent can tick, the results, what was found on the way, and that only Ahmed is left.

## Issues found

**Open for Ahmed.** He was away, so the plan's defaults stand: wooden tools moved, and the recipe is 3 planks over 2 sticks.

1. Should wooden tools really move to the bench? I've moved them because they're made only of wood, but it adds a step before your first pickaxe.
2. Is the recipe (3 planks over 2 sticks) what you want?
3. "Always along x" means you can't turn a bench. Turning it needs four more blocks, or real block facing, which ladders and stairs could use too.

**Numbered below: what happened while building it.**

**1. A grid in `item_t` does not fit on main's stack.** Expected, from the todo: a `crafting_grid_t crafting_bench` in `item_t`. Actual: `item_t` grew from 512 to 640 bytes, as it is aligned to 64, and `run/src/main/index.c:1061:1: error: the frame size of 16512 bytes is larger than 16384 bytes`. Main's frame sits exactly at the limit, as phase 7's issue 3 warned. Done: `item_t.borrowed_crafting_bench`, a pointer in the 8 bytes of padding at offset 120, to a grid taken from `arena_world` in `item_open`. `item_t` and `app_t` keep their sizes. The limit was left alone. `item_close` sets the pointer to nullptr, as the arena is reset there.

**2. The save format changed, and old worlds' states are refused.** Expected: the grid is saved. Actual: `save_state_items_t` goes from 608 to 704 bytes, so `SAVE_STATE_VERSION` is 3, and a version 2 state is refused and left as it was. Direction said today's test worlds are deleted and old-format saves need no migration, so none was written. A world saved before this phase will not open. Each grid's `station` is checked on reading, by a forged-byte row in `run_save_state`, and the bench's cells go through the name translation, in `run_save_state_names`.

**3. What the plan's list did not name, all needed.** `run/src/game/tick/index.c`: a third grid spills on death. `run/src/ui/screen_crafting/`: its view, draw and click take a grid and a title. `run/include/ui/screen_inventory/index.h`: `UI_SCREEN_WOODWORKING`. New module `run/src/world/bench/`, as placing, mining and blasts must agree on which half is which, as `world/wall_torch` does for torches. `textures/items/woodworking_bench/`: an icon of its own, since either half's face alone looks like nothing in a hotbar. `config/recipes/fuel_wood`: a bench burns as a table does. `explosion_finish`: its loop's second half became `explosion_around_break`, so that it serves both halves.

**4. An explosion always gives the bench.** The plan says "The same goes for an explosion". Any other block gives its drops one time in `power`, so a creeper at power 3 would lose two benches in three. I read the plan as exactly one, every time, as a torch on a wall already does. To change it, remove `bench_half_other` from the condition in `explosion_break_one`. The other half goes with no drop, and what stood on or hung from it goes as well.

**5. A half is never left, and two paths of that are not reached by a test.** If the other half's column is not loaded, mining holds at all but broken and a blast leaves the bench standing: both tested, at x = 63 of the test world. Placing that finds no room for the east half takes the west half back out: reached only by a broken line in issue 6, when the column-not-loaded check was removed and `a_neighbouring_column_not_loaded_refuses` still passed, as the east half's edit failed and the west came back out. **Not tested, by reading only:** the edit having no memory for the half that is mined after the other went. Mining finishes it on the next tick, and a blast puts the other half back. Nothing in these tests can run a world's section pool out at a chosen edit, so these stay as they are.

**6. Whether the tests notice.** Each broken and run against `run_item_crafting_station`, `run_player_interaction_bench`, `run_mob_creeper` and `run_ui_screens`, then restored from the commit, with the suite's results above from the unbroken code:

| Broken | Caught by |
|---|---|
| `crafting_match` ignores the station | 5 tests in `run_item_crafting_station` |
| The other half's cell not checked when placing | `a_block_in_the_east_cell_refuses_and_keeps_the_item`, `aimed_into_what_is_there_a_block_in_either_cell_refuses`, `the_player_in_either_cell_refuses` |
| A blast does not always give the bench | `a_blast_at_either_half_or_both_takes_the_bench_and_gives_one`, `no_real_blast_leaves_half_a_bench_and_each_bench_gone_gives_one` |
| The bench's screen clicks on the table's grid | 3 tests in `run_ui_screens` |
| `bench_half_other`'s answer ignored by mining and placing | 10 tests in `run_player_interaction_bench` |
| The same, by a blast | 4 tests in `run_mob_creeper` |

The first attempt at this built nothing, as the broken lines failed `-Werror`, and ctest passed on the old programs. That result was thrown away, and the lines rewritten to compile.

**7. `block_drops[].item` is not an item id.** Expected: comparing it with `ITEM_ID_WOODWORKING_BENCH`. Actual: `CHECK failed: ib_drops_count(...) == 1u: 0 != 1`, as a drop carries a row of `block_drop_items`, whose `.item` is the id. A mistake in my tests, not the game: fixed in the tests. On the way, `block_drop_items[].block` is found by name, so the bench's row says `BLOCK_NO_ID`, as no block is called `woodworking_bench`. `item_defs[].block` says the west half, and that is what a real game places from. **Left alone:** `player_interaction_hand_take`, used only by a run with no inventory, cannot pick up a bench. No played mode is such a run.

**8. Pictures changed and pins did not.** `crafting_ready.png` and `crafting_done.png` in `tests/run/ui/screens/fixtures/` are redrawn, as that replay now makes a stone pickaxe, a wooden one being no table's. `woodworking_ready.png` and `woodworking_done.png` are new. All four read by eye: titled `Crafting` and `Woodworking`, with the right items and output. The other pictures came out byte for byte. Phase 3's issue 3 led me to expect terrain pins to move with the ids. They did not: `yellow_terracotta` is the only block after the two new ones, and all terrain tests passed unchanged.

**9. The played proof, by eye and by report.** `tests/run/playthrough/fixtures/stairs.script`, seed 36, through the built debug program, from the title. Report, the same before and after the save: `the player mined 7 blocks and placed 4`, `the last mined was woodworking_bench_east`, `the inventory holds 8 items in 5 stacks`. Pictures read: `table_no_stairs.png` has `Crafting` with six planks in the pattern and no output. `bench_made.png` has the bench's icon in slot 4. In `bench_placed.png`, seen from an earlier run of the same aim, the top reads as one long bench, with its grain and holes running across the join and a saw and a vise on it, and the side shows tools hung under an apron. `stairs_ready.png` has `Woodworking` with the same six planks and 4 in the output. `bench_broken.png` has both halves gone and one small bench lying on the ground. `bench_in_inventory.png` has it in the hotbar. `day.script` holds the same for the pickaxe: `table_no_pickaxe.png` shows no output, and `pickaxe.png` has it in slot 4 from the bench's screen. `stairs_reloaded.png` is held by the test to exist and was not read. How it looks in play is Ahmed's to say.

**10. Aiming a bench takes more care than aiming a table.** The first script's aim, turned 450 from the table, where stairs had gone down, was refused with the item kept: the ground was clear where aimed. Which thing filled the east cell, the player or the table, was not worked out, since the refusal is what the plan asks. At 600 it goes down. **A player will meet this:** the bench goes east from where you aim, whichever way you face, and nothing on screen says why it did not go. Ahmed may want a hint, or turning (open question 3). Not done, as it was not asked for.

**11. Plan's Verification.** It named no `run_mob_creeper`, where a blast is tested, or the two new tests. Its pattern is now broad enough for them. It is 590 s, as a Bash call is limited to 10 minutes, and took 281.
