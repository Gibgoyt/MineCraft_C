# tests/fuzz/save

**Purpose:** ctest `fuzz_save` and the program `fuzz_save`: fuzzes everything a game believes from a saves directory: section records, `state.<generation>`, a manifest with its delta file, `world.meta`, and `settings.yaml`.
**Tier:** test.
**Owns:** a directory under `/tmp` for one run, and the game's own parts it builds, released at the end.
**Lifetime:** One run.
**Threads:** see `tests/fuzz/support/`. No job system, so the minds' searches run on the thread that ticks, and no card.
**Public functions:** none; a program.
**May be called by:** ctest, and a person: see `tests/fuzz/README.md`.
**Depends on:** `tests/fuzz/support/`, `run/src/save/`, `run/src/ui/menu/catalog/`, `run/src/settings/persist/`, `player`, `item`, `entity`, `mob`, `world`, and the small world in `tests/run/physics/fixtures/`.

## What an input is

The first byte says which of the five. A state or a world has every length and hash put right before it runs, unless one time in eight, as those would otherwise stop nearly every mutation at the door. `fix` is itself checked: on the seeds, files the real game wrote, it must change nothing, or the fuzzer has fallen out of step with the format and would be testing nothing.

A state or a record is mostly changed in place, by `mutate`: a section, each the same, and a value inside it, often one a float or a count should never be. A change of length or a byte thrown anywhere is refused on sight, which measured 6 states getting in out of 6000.

## What it holds them to

- **Record:** a refusal writes nothing. An acceptance gives only states in the table and levels up to 8, and those blocks come back through the encoder as the same blocks. It does not say the bytes come back: an encoder always chooses the same palette order, and a reader does not ask for it.
- **State:** `save_state_check` on exact memory; then the real way in, `save_state_open` on a file. What is accepted is put into a real player, inventory, entities and mobs, and ticks, in the order of `game_sim_tick`: items, searches, zombies, spawns, skeletons, creepers and their blasts, entities, combat, arrows, the player and its survival. Then it is saved, and the game must believe what it wrote. The seed has a creeper by the player with its fuse nearly run, as a fuse is 30 ticks and an input has 8.
- **World:** the real `save_open` on the files. What is accepted is read as a game reads it, every column of the region and some far lines, into memory ASan watches, and no state may be past the table. Then a save over it, which reads the old file to make the new one: if that works, the world must open again, with no more damaged sections than it found.
- **`world.meta`:** a name with an end.
- **`settings.yaml`:** nothing past its range. What is written reads back the same, with no problems.

## What it does not do

A search does not run on a worker, as a job system's threads would not survive the fork: `mob_init` is given none, which is its way of running them on the sim. An arrow is not given the player to hit. An input may leave a block changed in the world in memory, for the next to meet, though a blast's floor is put back, so a failure that needs that may not come again with `--replay`.
