# 003-ahmed-bohemoth-initial-game-fixes — phase 3: Stairs from every wood

**Status:** in progress: everything an agent can do in this phase is done and checked. Not played through the built program (issue 6). Only Ahmed's own check is left   (updated 2026-11-05)

## Goal

Stairs can be crafted from every kind of planks, and come out in that wood's colour.

**Ahmed's words:** "creating stairs with wood on a crafting table is not possible"

## What the code shows

The recipe does exist: `config/recipes/oak_stairs/index.yaml`, pattern `P  ` / `PP ` / `PPP`, gives 4. But:

- Its key is `oak_planks` alone, where most wood recipes take `group_planks`.
- `oak_stairs` is the only wooden stairs block in `config/blocks/`.

So spruce, birch or any other planks make nothing. **This is the likely cause, not a confirmed one.** Nothing tests this recipe, and which wood Ahmed used isn't known. The first todo settles it.

`oak_slab` has the same limit: oak planks only, and the only wooden slab. Slabs aren't part of this request, so they're left alone unless Ahmed wants them added (see Issues).

## Where these get crafted

Ahmed asked for stairs on the crafting table, and also for all woodworking to move to the new bench. So this phase makes them work on the table, and phase 4 moves them to the bench. **After phase 4, wooden stairs are bench-only**, and `cobblestone_stairs` stays on the table.

## Todo

- [x] Add a test in `tests/run/item/crafting/` that oak planks in the stairs pattern make `oak_stairs`, and run it before changing anything, to confirm or rule out a second cause: it passed on the unchanged game, so there is no second cause in the recipe tables or the grid (issue 1)
- [x] Add a `<wood>_stairs` block and item for each of `acacia`, `birch`, `cherry`, `dark_oak`, `jungle`, `mangrove`, `spruce`: `config/blocks/<wood>_stairs/index.yaml` and `config/items/<wood>_stairs/index.yaml`, with that wood's planks texture
- [x] Add a recipe for each: `config/recipes/<wood>_stairs/index.yaml`, same pattern, key `<wood>_planks`
- [x] Add the new stairs to `config/recipes/fuel_wood/index.yaml`, beside `oak_stairs`
- [x] Add every `BLOCK_ID_*_STAIRS` needed wherever wooden stairs are listed by id: search `run/src/` for `OAK_STAIRS`. The one place is `explosion_resistance` in `run/src/entity/explosion/index.c`
- [x] Add a test that each wood makes its own stairs, and that mixed planks make nothing: `tests/run/item/crafting/stairs/`, ctest `run_item_crafting_stairs`
- [x] Update `textures/ATTRIBUTION.md` only if a texture is added; none was added, so it is unchanged
- [ ] Ahmed: make stairs from at least oak and spruce on a crafting table, place them, and check they look right

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_item_crafting|run_item_crafting_stairs|run_save_world|run_terrain_biome|run_terrain_density|run_terrain_feature|run_terrain_hydrology|run_terrain_ore|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: make stairs from at least oak and spruce, and place them.

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --windowed

To see all eight woods in one picture from the test:

    mkdir -p /tmp/stairs_shots
    MC_STAIRS_SHOTS=/tmp/stairs_shots tools/test-or-build.sh timeout 280 ctest --preset debug -R '^run_item_crafting_stairs$' --output-on-failure

Results, debug preset:

| Check | Result |
|---|---|
| The commands above | 11 of 11 |
| Whole debug suite, `-j1`, before the new picture test and the fixes in issues 3 and 4 | 105 of 111 in 1727 s: the six failures are issues 3 and 4, and all six passed once fixed |
| Five lines broken on purpose (issue 5) | All five caught |
| Most card memory the new test held | 420 MiB, by `nvidia-smi` |
| `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c` after the last run | Printed nothing |

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: should slabs get the same treatment?** `oak_slab` has the same oak-only limit. Adding seven more slabs is the same kind of work, and I'd do it in this phase if you want it. Ahmed was away, so the plan's default stands: slabs stay oak only.

**Stairs still face one way.** They rise towards `+y` whichever way you stand (Stage 09.1 issue 1). This phase doesn't change that.

**1. The cause: oak planks always worked, and other planks had no recipe.** Expected, from the plan: oak's key is likely the cause, with a second cause possible. The test of oak planks in the pattern, laid with the right button on a table, was built and run before any other change, at `38e02b5`: `ok oak_planks_in_the_pattern_make_oak_stairs_on_a_table (3 checks)`, 4 `oak_stairs`. So the recipe tables and the grid had no second cause, and the missing recipes for other planks were the problem. Which wood Ahmed used is still not known, and this does not test the crafting screen itself: see issue 6.

**2. `compile_game_tables` held the number of blocks to 140.** Expected: the listed test passes. Actual: `tests/compile/game_tables/index.c:102: about_120_blocks_with_air_first: CHECK failed: BLOCK_COUNT >= 110u && BLOCK_COUNT <= 140u`, as the count went from 138 to 145. Done: the band is now 110 to 170, leaving room for phase 4's two benches, and the test is named `between_110_and_170_blocks_with_air_first`.

**3. Adding blocks changed pinned digests, and it was only the numbers.** Expected: as in phase 2's issue 2, ids after the new blocks move. Actual: the whole suite failed on 7 pins in 5 tests: `run_terrain_biome`, `_density`, `_feature` (2), `_hydrology` (2) and `_ore`. To tell that from a change of land, the seven blocks were named `zz_*_stairs` for one build, which sorts them last so that no id moves: all 5 passed on the **old** pins, unchanged. The names were put back and the pins updated with a line each. `TERRAIN_GENERATOR_VERSION` stays 1, as the land is the same.

**4. `run_save_world` broke by chance when the block count moved.** Expected: it passes, as saving did not change. Actual: `tests/run/save/world/index.c:560: only_what_changed_is_on_disk: CHECK failed: world_edit_block_set(&rig->world->edit, -33 * 16, 33 * 16, 0, 3u) == WORLD_EDIT_STATUS_CHANGED`. Cause: the test's made-up columns fill each cell from a sum modulo `BLOCK_COUNT - 1`, and with 145 blocks that cell already held block 3, so setting it to 3 changed nothing: worked by hand from `sw_generate`, it holds 3 now and held 61 with 138 blocks. The game was right. Fixed in the test with `sw_cell_change`, which always sets a block other than the one there. Other fixed numbers in that file were left alone as they pass: `REQUIRE(generated != 11u ...)` guards some of them.

**5. Whether the tests notice.** Five lines broken at once and the new test run: spruce stairs in birch's texture, birch's recipe giving 2, mangrove stairs out of the fuel list, jungle stairs out of `explosion_resistance`, and cherry's item in oak's texture. All five failed with a message, and the spruce one failed both the table check and the picture: `CS_WOOD_ROWS[nearest].name == CS_WOOD_ROWS[i].name: "birch" != "spruce"`. All restored, and passing again.

**6. Beyond the plan's list: the card, but not the built program.** The Goal says "in that wood's colour", which the plan's tests would only have shown as a name in a table. So `the_card_draws_each_wood_in_its_own_colour` places each wood by the hands and reads the real picture from above: each wood's stairs came within 1 of its planks on each colour channel (spruce 97 74 57 for both, cherry 209 170 164 and 210 171 165), and nearest to its own of all eight. `all_eight.png` was also looked at by eye: eight stairs on stone in eight colours, each rising to the north. **Not done:** no script drove the crafting screen in the built program. Those clicks are the code every recipe goes through, and `run_ui_screens` covers that code, but stairs on the real screen are left to Ahmed's check above.

**7. Plan's Verification left out what this phase needed.** It had no `run_item_crafting_stairs`, which it could not have named, and none of the tests of issues 3 and 4, which only the whole suite found. Added above. The full suite was not run a second time after the fixes, as those were in tests alone and it holds the machine-wide lock for half an hour: the six that failed and the new test passed, as listed above.
