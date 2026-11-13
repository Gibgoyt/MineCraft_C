# tests/run/playthrough

**Purpose:** ctest `run_playthrough`: the whole game as a person plays it, in the built program, through its real menus, from scripts of keys and mouse with nothing done behind them.
**Tier:** test.
**Owns:** a directory under `/tmp`, removed at the end unless `MC_PLAYTHROUGH_KEEP` is set.
**Lifetime:** One run.
**Threads:** main, and the child it watches.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the built `minecraft_c`, the card (compute only), and for the tree: `terrain`, `climate`, `job`, `game/spawn`. It opens no display and no input device.

- `fixtures/day.script`, seed 36: a new world, mines the block underfoot and has it, goes by `/tp` to the tree the test found (below), facing its root, cuts it by hand, which brings the whole tree down, walks in over the logs and turns back, makes a crafting table from four logs in the inventory, places the table, opens it, sees a log make no planks there, makes a woodworking bench from three logs, places that, opens it, and on it makes planks, sticks and a wooden pickaxe (plan 005 phase 6), turns render distance down to 2 and back in the world that stays open, quits to the title and opens the world again.
- `fixtures/stairs.script`, seed 36: the day as far as an open bench, where two logs make eight planks and six of them four stairs of the tree's wood on its grid, two go down on the ground, one is mined back by hand, the bench is mined by one half, both halves go and the one bench that comes down is picked up, and all of it is saved and opened again.
- `fixtures/bench.script`, seed 36: the day as far as a bench put down. It opens, the two logs left go on its grid, where they show sixteen sticks, and closing gives both back. It is saved with the bench standing and opened again, the bench's screen comes up from that, and its west half, on the left as it was placed, is mined by hand. Both go, the one bench is picked up, and it is saved and opened once more with nothing back.
- `../player/felling/fixtures/fell.script`, seed 36, which `tests/run/player/felling/README.md` describes: the block underfoot, then by `/tp` (its `@fell` line) to the same tree the day finds, a look up at it, and one cut with bare hands brings it down. It must say so in the report, and it is saved and opened again.
- `../player/interaction/door/fixtures/door.script`, seed 36 (plan 005 phase 2): stairs.script's day as far as the open bench, where two logs make eight planks and six of them three wooden doors. One is placed beside the bench, hinged on the bench's side, opened, shut, opened and shut again, and mined by its top half: both halves go and one door drops, which is walked over and picked up, and it is saved and opened again.
- `fixtures/night.script`, seed 2: stands in the open until dark, then turns and swings, and is saved and opened again. Since plan 005 phase 3 turned off zombies, skeletons and creepers, nothing comes all night: each report says 0 mobs came, struck, shot or burst, and the player never died. Before, zombies struck and died, skeletons shot, a creeper left a crater, and the player died and came back by the swing; the script is the same, and its swings now meet the air.

**The tree.** The day, stairs, bench and door scripts need 9 logs from one tree: 4 for the table, 3 for the bench and 2 for
planks. The land near seed 36's start moves with each plan that changes the terrain (plan 006 phase 03 turned its mangrove of
10 into an acacia of 4), so, as `tests/cuda/under_water/` finds its sea, the test finds the tree from the terrain itself. It
opens seed 36's climate as a new menu world does, finds the start with `game_spawn_find`, and makes the land in tiles of 48
blocks, each with a margin of 16, square by square out from the start's (out to about 1150 blocks; since plan 007 phase 03 the first fit tree is a mangrove of 16 logs 883 blocks off), chunk by chunk as a column
job makes them. It takes the nearest tree of the first square that has one fit: 12 to 64 logs (9 used, 3 to spare, and one
stack, so hotbar slots 4 to 6, which the scripts use, stay free), all within 3 blocks across of its root, the root on the
ground, no other tree's log within 3 blocks, the ground within 3 flat and clear, and a stand 2 to 3 blocks off whose eye sees
the root. Since plan 006 phase 03 that is a jungle tree 65 blocks from the start: nothing else near it has 12 logs.

Each of the four scripts has a line `@tree`. The test replaces it with: the `/tp` to the stand (with 25 s to wait if the tree
is more than 64 blocks off, as the dive waits), the turn to the root from a new world's first look (which a `/tp` keeps), one
cut that brings the tree down, a walk in over the logs, and a second `/tp` to one place in the trunk's block, facing along an
axis. A line `@feet` later in a script is that second `/tp` again, back to the same place with the look kept: `stairs.script`
steps in to pick up the stair it mined (on plan 006 phase 04's land its drop lay out of reach) and comes back by it. That
last place is what the rest of the scripts were tuned to by their screenshots: the table, the bench, the stairs and
the door land by where the eye is in its block (`PT_FEET_AHEAD`, `PT_FEET_RIGHT` in `index.c` say why). The script so made is
run from the test's temporary directory, and kept with it under `MC_PLAYTHROUGH_KEEP`. What the report says is read against
that tree: its log's name, `the tree feller felled K`, its wood's stairs, and the inventory as at least what the day makes and
the same again from the save, since more of the fallen logs, or a sapling or stick the leaves gave, may be picked up as well.

The scripts were worked out by hand from screenshots, so **a change to terrain, spawning or a screen's layout can leave them clicking on nothing.** That is what this test is for: see what it did with `MC_PLAYTHROUGH_KEEP=1`, and read the pictures it names.

Plan 003 phase 8 stage 7 aimed the day at seed 36 when the world's land changed, as seed 123's start no longer has a tree in reach.

It checks what the program's report says. It cannot check that the pictures look right: that is for a person with the screenshots.
