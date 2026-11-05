# tests/run/save/world/names

**Purpose:** ctest `run_save_world_names`. Tests `run/src/save/names/` and what `run/src/save/world/` does with a list of names: a list's bytes by hand; a world saved under a table without one block, at the start, middle and end of the table, and opened in this build, with every voxel compared by name; opening it changing nothing on the disk; a save after it putting the name on the end of the world's list and moving no number; what is on its way to a file being in the same numbers; a block gone reading as air, with a warning, and coming back in a build that has it; a world in the old format refused and left as it was; and a list that cannot be believed.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/save/`, `run/src/world/`.

## How a build with another table is had, with no other build

A table is fixed when the program is built, so the test writes what an older build would have. It picks the list that build would have had, this build's less one name, and writes a world of numbers in that list's terms, all below its length and so all numbers this build's code takes without question. Then it replaces the list in the manifest, with its hash and checksums made to agree. The files on the disk are then exactly those of a world saved in that older build, and everything from there on goes through the real `save_open`.

No code in `run/` is there for this, as nothing in the program takes a list from anywhere but a manifest or its own tables.
