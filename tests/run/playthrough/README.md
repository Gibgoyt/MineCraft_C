# tests/run/playthrough

**Purpose:** ctest `run_playthrough`: the whole game as a person plays it, in the built program, through its real menus, from scripts of keys and mouse with nothing done behind them.
**Tier:** test.
**Owns:** a directory under `/tmp`, removed at the end unless `MC_PLAYTHROUGH_KEEP` is set.
**Lifetime:** One run.
**Threads:** main, and the child it watches.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the built `minecraft_c`, the card (compute only). It opens no display and no input device.

- `fixtures/day.script`, seed 36: a new world, mines the mud underfoot and has it, gets out of the hole, cuts a mangrove by hand, whose first log brings the whole tree down (its next three clicks find nothing left), makes a crafting table from four logs in the inventory, places the table, opens it, sees a log make no planks there, makes a woodworking bench from three logs, places that, opens it, and on it makes planks, sticks and a wooden pickaxe (plan 005 phase 6), turns render distance down to 2 and back in the world that stays open, quits to the title and opens the world again.
- `fixtures/stairs.script`, seed 36: the day as far as an open bench, where two logs make eight planks and six of them four mangrove stairs on its grid, two go down on the ground, one is mined back by hand, the bench is mined by one half, both halves go and the one bench that comes down is picked up, and all of it is saved and opened again.
- `fixtures/bench.script`, seed 36: the day as far as a bench put down. It opens, the two logs left go on its grid, where they show sixteen sticks, and closing gives both back. It is saved with the bench standing and opened again, the bench's screen comes up from that, and its west half, on the left as it was placed, is mined by hand. Both go, the one bench is picked up, and it is saved and opened once more with nothing back.
- `../player/felling/fixtures/fell.script`, seed 36, which `tests/run/player/felling/README.md` describes: the day as far as the mangrove, then one cut with bare hands brings it down. It must say so in the report, and it is saved and opened again.
- `../player/interaction/door/fixtures/door.script`, seed 36 (plan 005 phase 2): stairs.script's day as far as the open bench, where two logs make eight planks and six of them three wooden doors. One is placed beside the bench, hinged on the bench's side, opened, shut, opened and shut again, and mined by its top half: both halves go and one door drops, which is walked over and picked up, and it is saved and opened again.
- `fixtures/night.script`, seed 2: stands in the open until dark, then turns and swings, and is saved and opened again. Since plan 005 phase 3 turned off zombies, skeletons and creepers, nothing comes all night: each report says 0 mobs came, struck, shot or burst, and the player never died. Before, zombies struck and died, skeletons shot, a creeper left a crater, and the player died and came back by the swing; the script is the same, and its swings now meet the air.

The scripts were worked out by hand from screenshots, so **a change to terrain, spawning or a screen's layout can leave them clicking on nothing.** That is what this test is for: see what it did with `MC_PLAYTHROUGH_KEEP=1`, and read the pictures it names.

Plan 003 phase 8 stage 7 aimed the day at seed 36 when the world's land changed, as seed 123's start no longer has a tree in reach.

It checks what the program's report says. It cannot check that the pictures look right: that is for a person with the screenshots.
