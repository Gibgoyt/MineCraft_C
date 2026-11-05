# 003-ahmed-bohemoth-initial-game-fixes — phase 1: Worlds survive new blocks

**Status:** in progress: the gate check found the phase real, fixed the small things it found, and every agent-runnable check passes, the whole suite 107 of 107. Only Ahmed's own check after phase 2 is left   (updated 2026-11-05)

## Goal

Adding a block or item no longer locks you out of your worlds: a world saved before the change opens after it, with every block and item still what it was.

**Ahmed did not ask for this directly.** It comes first because phases 2, 3 and 4 all add blocks, and each would otherwise make the game refuse every saved world. It matters more as the game keeps growing.

## Why it's needed

- Block ids are given out alphabetically (`compile/src/game_tables/blocks/index.c:581`), so a new block shifts the ids after it. Item ids work the same way.
- Saves store those ids, so `save_world_table_hash` (`run/src/save/world/index.c:87`) hashes all the block names, and `save_world_open` refuses a mismatch: "saved with another block table, so its numbers would mean the wrong blocks" (line 378).
- That refusal is correct today. It stops a wrong world loading, but it also means any new block ends every world.

## Todo

- [x] Check whether `run/src/save/state/` stores item ids with the same problem: inventories, chests, furnaces, crafting grids, dropped items. It does, and more: see issue 1
- [x] Save the block and item name tables with each world, in `run/src/save/world/` and `run/src/save/state/`, and bump the format version. World format 5, state format 2, shared code in the new `run/src/save/names/`
- [x] On open, build an old-id to new-id map by name and translate each section and item as it's read. Sections in `run/src/save/world/`, a whole state file in the new `run/src/save/state/translate/`
- [x] Define what happens when a saved block or item no longer exists: the world opens, a block becomes air, an item is dropped, and the log says what was lost. Also a dropped item and a mob: see `run/src/save/state/translate/README.md`
- [x] Log clearly when a world is refused for being in the old format
- [x] Add tests in `tests/run/save/world/` and `tests/run/save/state/`: save, add a block to the table, reopen, and compare every block and item. New ctests `run_save_world_names` and `run_save_state_names`, and a real rebuild: see issue 2
- [x] Note in `config/blocks/README.md` that adding a block now keeps worlds
- [x] Update the pinned terrain digests and golden pictures only if ids really change them, with a line saying why. None updated: this phase adds no block, so no id moved, and the whole suite passes with them as they were. A later phase's new block will move ids, and those digests are its to update
- [ ] Ahmed: a world made before phase 2 still opens after phase 2

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_save_world|run_save_world_names|run_save_state|run_save_state_names|run_save_power|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: a world made before phase 2 still opens after phase 2.

Results, on this phase's code:

| Check | Result |
|---|---|
| The commands above, with the game tables' test run too | 8 of 8 passed (`run_save_world` 39 s), and `pgrep` printed nothing |
| Whole debug suite, `ctest --preset debug -j1` | 107 of 107 |
| Sanitize: `run_save_world_names`, `run_save_state_names`, `run_save_state`, `fuzz_save` | All passed |
| `build/sanitize/bin/fuzz_save --seconds 120` | 79015 inputs, 0 found wrong: 4240 states opened and 7214 worlds opened, saved over and opened again |

The gate check, in its own clean build, on main merged with `c3f3940`:

| Check | Result |
|---|---|
| The phase's commands | 7 of 7 passed, and `pgrep` printed nothing |
| Whole debug suite, `ctest --preset debug -j1` | 107 of 107, in 1663 s |
| Sanitize: `run_save_state_names`, `run_save_world_names` | Passed |
| `build/sanitize/bin/fuzz_save --seconds 90` | 4664 checks, 0 failed |
| A real world through real rebuilds | Issue 11 |
| 28 lines of the code broken one at a time | Issue 12 |

## Commits

Appended as they land:

- `a33ed30` Keep the names of what its numbers mean in each saved world and state, and find every block, item and mob by name on opening, so that adding one to a table no longer ends any world, and what a build has not got goes with a line in the log.
- `9f27353` List the save names directory, the state translation and their two new tests in the repository structure.
- `0d2fea2` Record phase 1 of the game fixes in its plan file: every box an agent can tick, the results, a real rebuild with a new block, and what was found on the way.
- `c3f3940` Test what a block being mined and a stack past its limits come to in a translated state, and take out a record check that nothing called any more.
- `b882544` Record the gate check of phase 1 in its plan file: a real world through a real rebuild, what breaking its code showed, and that only Ahmed is left.

## Issues found

**1. What a state file holds that is a place in a table, which the plan only partly listed.** Expected: inventories, chests, furnaces, crafting grids, dropped items. Found, by reading `run/include/` for every struct in `save_state_player_t`, `save_state_items_t`, chests, furnaces, drops and bodies:

- Items: every `item_stack_t` (slots, pointer, crafting cells and outputs, armour, chests, furnaces), `smelting_furnace_t.cooking`, `player_survival_t.held_item`, `player_combat_t.held_item`.
- Rows of `block_drop_items`: `item_drop_t.item`, `player_drop_t.item`.
- Blocks: `player_interaction_t.mining_block`, `hand_block`, `last_mined`.
- Mobs: the entity store's `kind`, one for each body.
- Recipes: `crafting_grid_t.recipe`, a row of `recipe_defs`. **This was a bug nobody had hit:** `save_state_table_hash` does not cover recipes, so a change to the recipes alone left a grid holding a row that now means another recipe, or refused the world if the row had gone. Recipes have no names to find a row by, so a grid's `recipe` and `output` are no longer believed from any file: `save_state_apply` works them out from the cells. Phase 4 changes recipes, so it would have met this.

Done: all are translated, and a test covers each. No other number in a state file is a place in a table. `killed_by`, `held_tool` and `held_tier` are enums in the code, not generated tables.

**2. Whether it works with a real new block, which a test cannot have.** A table is fixed at build time, so the two new ctests write the files an older build would have written (see their READMEs) rather than having two builds. To check that this stands for the real thing, by hand, in this worktree, with a throwaway `config/blocks/aaa_probe/` and `config/items/aaa_probe/`, removed afterwards and never committed:

1. Built without it. `minecraft_c --headless --menu --saves <dir> --script <dig>` made and dug a world, generation 1. A second run took a screenshot.
2. Added the probe and rebuilt: `BLOCK_ID_AAA_PROBE = 1`, `BLOCK_COUNT` 135 from 134, so **every id but air's moved**. Reopened: "was saved with other blocks: 1 new here, 0 gone", "the state ... was saved with other tables, and each number in it has been found by name", "0 past reading", 0 bytes live, exit 0. **The screenshot was byte for byte the first**, and again with the file deleted first, to be sure it was written anew.
3. Dug on and saved in that build: `strings manifest` ends `yellow_terracotta`, `aaa_probe`, so the name went on the end and nothing moved.
4. Removed the probe and rebuilt: the world opened, with a warning naming `aaa_probe` for each of block, dropped item and item, "0 past reading", 0 bytes live, exit 0.

This goes a step past Ahmed's check, and does not replace it.

**3. The plan's way to do it, against what was built.** The plan reads as one map on open, and each section translated as it is read. For state that is what happens. For blocks a plain map is not enough: a delta file is only rewritten when part of it changes, and a record carried across is copied byte for byte, so with a build's own numbers in the records one world's files would hold several numberings at once, each needing its own table. So a world's numbers belong to the world: its manifest holds an append-only list, every record in every delta file means a place in it, and a new name goes on the end. No delta file ever needs rewriting, a save in a new build costs nothing extra, and a block that went and came back finds its records untouched. The cost is one lookup per voxel when a section goes in or out, only in a world whose list is not this build's: `names_same` skips both otherwise. See `run/src/save/world/README.md`.

**4. A body's `kind` was never checked.** Expected: `save_state_check` refuses what the game cannot live with. Actual: a forged `kind` at or past `MOB_COUNT` was believed, and code goes on to use it, such as `run/src/mob/spawn/index.c:173`. Found while adding the translation. Fixed: `save_state_check_bodies` refuses it, and `run_save_state_names` tests that.

**5. Tests that were right before and had to change.** All from the format, none loosened:

- `tests/run/save/world/index.c`: a new world's manifest is no longer 64 bytes, and its `regions_checksum` covers the list. "An older format" now forges a version from before generators, since version 4 now gets `SAVE_WORLD_ERROR_VERSION`.
- `tests/run/save/state/index.c`: the by-hand bytes have the names section and version 2. The full-size check allows for `SAVE_STATE_NAMES_BYTES_MAX`. `ss_fill` wrote grid cells without calling `crafting_grid_changed`, which the game never does, and reloading then gave a different `output`: `every_part_reloads_byte_for_byte_and_goes_on_the_same` failed with a first difference at byte 40, the sections hash. The fill now calls it.
- `tests/fuzz/save/index.c`: `fs_world_fix` took everything after a manifest's header for regions, and would have corrupted the list. It keeps the count and hashes whatever list is there. A new seed says it is from other tables, to reach the translation.

**6. Wiring outside this phase's paths**, all forced by sizes or by the fuzzer's list: `run/include/save/index.h` (`save_t` 704 to 768 bytes, with `_pad0[16]` before the 64-byte-aligned autosave), `run/include/main/index.h` (`app_t` 16000 to 16064), `cmake/hardening.cmake` (the two new files in the fuzzer's instrumented list), `run/src/save/README.md`, `tests/fuzz/save/README.md`.

**7. Left alone: a smaller `stack` or `durability` in an item's config, with no name changing.** Expected a world to survive it. Actual: a build with the same names has the same hash, so no translation runs, and `item_stack_is_valid` refuses a stack over the new limit: `SAVE_WORLD_ERROR_DAMAGED`, for the whole world. A translation does bring such a stack within limits, so this only happens when nothing was added at the same time. Not in this phase's goal, and no phase here plans such a change. Escalated by this note: the fix would be to settle stacks on every open, not only a translated one.

**8. Sizes to watch.** `run/src/save/world/index.c` is 1499 lines, one short of the standard's split at 1500, so the next change there should split it, likely the names into `run/src/save/world/names/`. `tests/run/save/world/index.c` is 1990 lines of the hard 2000, which is why the new tests have their own directories.

**9. A refused-for-old-format world has only been seen forged.** The check and its message are tested on a manifest and a state file with an older version written in, as today's old-format worlds were deleted and no build of the old format is kept. The bytes of such a file past its header are not read before the refusal, so this should be the same as a real one.

**10. Existing worlds cannot be carried over once.** Worlds saved before this phase, such as `test_1`, have no name table, so they are refused. Ahmed deleted today's test worlds, so nothing is lost, and the table's default stands. Worlds made after this phase survive from then on.

**11. Gate check: a real world through a real new block, read from the pictures.** Expected: what Ahmed would see, with nothing done behind the game. In `--headless --menu`, on a scratch `--saves` directory, under the mutex:

1. `tests/run/playthrough/fixtures/day.script`, seed 36, on this build: a person mines mud and mangrove logs, crafts, places a crafting table and makes a wooden pickaxe. The world and its inventory were read from `world.png` and `inventory.png`: the table underfoot, and mud, a log, 3 planks, the pickaxe and 2 sticks.
2. The world was copied. A control copy was opened in the same build and looked at with a script.
3. `config/blocks/aaa_probe/` and `config/items/aaa_probe/` were added, never committed, and the game rebuilt: `BLOCK_ID_AAA_PROBE = 1`, `BLOCK_COUNT` 135, so every id but air's moved. The other copy was opened with the same script: "saved with other blocks: 1 new here", and **both screenshots were byte for byte the control's** (sha256 `c8b0e168...` inventory, `b5f59061...` world). The log said the same 5 blocks mined, last `mangrove_log`, 8 items in 5 stacks, slot 4 `wood_pickaxe`, 0 sections past reading, 0 bytes live, exit 0. The manifest ended `yellow_terracotta`, `aaa_probe`.
4. The probe was removed and the game rebuilt. That world opened: a warning naming `aaa_probe` for each of block, dropped item and item, the same report, the same inventory in the picture, 0 past reading, 0 bytes live, exit 0.
5. A copy with version 4 forged into its manifest, with the header's checksum good: the log said "is in version 4 of the format, from before a world kept the names of its blocks", the world list showed "Could not open it: from another version" in red, Play was left greyed, the program went on, and the state file stayed the same bytes.

A placed block with a moved number and a chest did not go through this, as a script has no way to come by a probe block: `run_save_world_names` and `run_save_state_names` cover them, and issue 12 says those tests notice. Ahmed's check is still his.

**12. Gate check: whether the tests notice. They mostly did, and missed four lines.** Expected: breaking the translation fails a test. 28 single lines of `run/src/save/state/translate/`, `run/src/save/world/` and `run/src/save/names/` were broken one at a time, and `run_save_state_names` and `run_save_world_names` run against each, the sources restored after each.

- Caught at once: 24, including a chest, furnace, armour, pointer, ground drop, ring drop, body kind or held item not translated, a world's reads or writes not translated, a gone block not air or not logged, a new name not put on the list, an old world not refused, a map by number only, and any character allowed in a name.
- **Missed, three behaviours the translate README claims and no test looked at:** a block being mined that has gone (`mining` and `progress` left set, and with them `mining_block`), a stack over its item's limit today, and wear past `durability` or on what no longer wears. Fixed: new test `a_block_being_mined_that_has_gone_stops_the_mining_and_a_stack_keeps_to_what_its_item_is_today`. Those lines broken again were all caught. The code was already right.
- Missed and left: zeroing a grid's `output` in `state_translate_items`. `save_state_apply` works it out again anyway, so nothing can tell. It stays, so that a translated file never holds a number from another table.

**13. Gate check: code nothing called.** Expected: none. Actual: `save_world_record_valid` had no caller left once the `_within` forms came in, in `run/` or `tests/`, though two READMEs still named it. Fixed: taken out, and the READMEs corrected. `save_world_record_decode` is only called by `tests/run/save/state/index.c`, and stays, with its README saying so. The comment over `save_names_live_count` promised a hash function that does not exist, and was corrected.

**14. Gate check: what else was looked for and not found.** No stubs or TODOs. Every table name allowed by `compile/src/game_tables/` is a name `save_names_read` accepts, both at most 64 bytes of `a-z 0-9 _`, so a build cannot write a list it would refuse. Nothing else on disk holds a table's number: `file_write_atomic` and `FILE_MODE_WRITE` outside `run/src/save/` are the screenshot, the profiler's report, `world.meta` and the settings, and those last two hold no id. Other `uint16_t` fields in the player, item, entity and mob structs saved are counts and ticks; `entity_explosion`'s `block` is not saved. Issue 7 stays as it was: not this phase's goal, and escalated there.
