# 003-ahmed-bohemoth-initial-game-fixes — phase 1: Worlds survive new blocks

**Status:** not started   (updated 2026-11-04)

## Goal

Adding a block or item no longer locks you out of your worlds: a world saved before the change opens after it, with every block and item still what it was.

**Ahmed did not ask for this directly.** It comes first because phases 2, 3 and 4 all add blocks, and each would otherwise make the game refuse every saved world. It matters more as the game keeps growing.

## Why it's needed

- Block ids are given out alphabetically (`compile/src/game_tables/blocks/index.c:581`), so a new block shifts the ids after it. Item ids work the same way.
- Saves store those ids, so `save_world_table_hash` (`run/src/save/world/index.c:87`) hashes all the block names, and `save_world_open` refuses a mismatch: "saved with another block table, so its numbers would mean the wrong blocks" (line 378).
- That refusal is correct today. It stops a wrong world loading, but it also means any new block ends every world.

## Todo

- [ ] Check whether `run/src/save/state/` stores item ids with the same problem: inventories, chests, furnaces, crafting grids, dropped items
- [ ] Save the block and item name tables with each world, in `run/src/save/world/` and `run/src/save/state/`, and bump the format version
- [ ] On open, build an old-id to new-id map by name and translate each section and item as it's read
- [ ] Define what happens when a saved block or item no longer exists: the world opens, a block becomes air, an item is dropped, and the log says what was lost
- [ ] Log clearly when a world is refused for being in the old format
- [ ] Add tests in `tests/run/save/world/` and `tests/run/save/state/`: save, add a block to the table, reopen, and compare every block and item
- [ ] Note in `config/blocks/README.md` that adding a block now keeps worlds
- [ ] Update the pinned terrain digests and golden pictures only if ids really change them, with a line saying why

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_save_world|run_save_state|run_save_power|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: a world made before phase 2 still opens after phase 2.

## Commits

Appended as they land:

## Issues found

**Existing worlds cannot be carried over once.** Worlds saved today, such as `test_1`, have no name table, so the first build with this phase still refuses them. They're test worlds, so this should be acceptable. Worlds made after this phase survive from then on. The only way to keep today's worlds would be to ship today's table as a one-time migration, which isn't planned unless Ahmed asks.
