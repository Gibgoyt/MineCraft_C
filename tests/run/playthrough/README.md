# tests/run/playthrough

**Purpose:** ctest `run_playthrough`: the whole game as a person plays it, in the built program, through its real menus, from scripts of keys and mouse with nothing done behind them.
**Tier:** test.
**Owns:** a directory under `/tmp`, removed at the end unless `MC_PLAYTHROUGH_KEEP` is set.
**Lifetime:** One run.
**Threads:** main, and the child it watches.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the built `minecraft_c`, the card (compute only). It opens no display and no input device.

- `fixtures/day.script`, seed 36: a new world, mines the mud underfoot and has it, gets out of the hole, takes four logs from a mangrove, crafts planks, a table and sticks in the inventory, places the table, opens it and makes a wooden pickaxe, turns render distance down to 2 and back in the world that stays open, quits to the title and opens the world again.
- `fixtures/night.script`, seed 2: stands in the open until dark, then turns and swings. Zombies strike and die, skeletons shoot, a creeper leaves a crater, the player dies and comes back by the swing, and all of it is saved and opened again.

The scripts were worked out by hand from screenshots, so **a change to terrain, spawning or a screen's layout can leave them clicking on nothing.** That is what this test is for: see what it did with `MC_PLAYTHROUGH_KEEP=1`, and read the pictures it names.

Plan 003 phase 8 stage 7 aimed the day at seed 36 when the world's land changed, as seed 123's start no longer has a tree in reach.

It checks what the program's report says. It cannot check that the pictures look right: that is for a person with the screenshots.
