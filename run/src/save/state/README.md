# run/src/save/state

**Purpose:** what a world is besides its blocks: player, inventory, what is worn and held, chests and furnaces, items on the ground, arrows, bodies, minds, and how far time has run, in one file that goes with one save of the blocks.
**Tier:** Arena: everything it holds comes from the arena `save` gives it, for one world.
**Owns:** nothing on the heap. One image of a whole file, and the world's path.
**Lifetime:** World.
**Threads:** only the thread holding the world captures and applies, and only with nothing else using the image: `save/autosave` sees to that. Any one thread writes.
**Public functions** (`run/include/save/state/index.h`):

- `save_state_open`, `save_state_close`: a pair. Open reads and checks, and refuses a file it cannot believe, having changed nothing.
- `save_state_kept_start`, `save_state_apply`: what a file holds, for a world being opened.
- `save_state_capture`, `save_state_write`, `save_state_forget`: one save, split so that the writing can be on a worker.
- `save_state_check`, `save_state_section_find`, `save_state_table_hash`: what a file may say, for this and for a test.

Its child `translate` rewrites a file from other tables, as `save_state_open` reads it.

**May be called by:** `save`, `save/autosave`, and `game`, which opens a world with it.
**Depends on:** `save/world`, for its statuses, `save/names`, `save/state/translate`, `file`, `arena`, `log`, `assert`, the stores in `player`, `item`, `entity` and `mob`, whose fields it reads and writes as a file's numbers, the generated tables, and glibc's `opendir`.

## What a file may say, and what the game then writes

`save_state_check` asks more than that a number be finite: it refuses what the game could not then live with, as a state with good hashes can be an edited one. A speed past `SAVE_STATE_SPEED_MOST`, which physics would stop the program for, an eye past `SAVE_STATE_EYE_MOST`, a fall or a body's tally past `SAVE_STATE_TALLY_MOST`, or air the game never has. What is alive can pass those limits, as an arrow keeps flying, so `save_state_capture` writes such a number at its limit, and something that is not a number as 0. The game goes on with what it has, and the file is one it believes.

## On disk

    <world>/state.<g>       this file; g is the save of the blocks it goes with
    <world>/state.<g>.tmp   one on its way there, which is never believed

All numbers are little-endian. A file is a `save_state_header_t`, then sections, each a `save_state_section_t` and its bytes, padded with zeros to 8. Section ids and their contents are in the header, with every size and offset asserted. A section is there at most once. The time is always there, and from version 4 the calendar right after it (`save_state_calendar_t`: the world clock's whole days, the last census week and `D0`, plan 004 phase 01), and the rest only if the game that wrote it has that part: a game with no mobs writes no minds, and a file's minds are not used by one. An id that this version does not have is damage. Names are always there, last.

| Section | Holds |
|---|---|
| Time | World ticks, the time of day, where the player looks |
| Player | Body, health, hands and swing, as they are |
| Items | Inventory, the three crafting grids, armor, what the pointer carries, and the books |
| Chests, furnaces | Each one, by the cell it stands in |
| Drops, arrows | Each one, and how the next will scatter |
| Bodies | Every array of the entity store for its bodies, the slot tables, the numbers blasts go on with |
| Spawn, minds | What each mind is up to, indexed by slot, and each one's random numbers |
| Sexes | From version 5, with the bodies: each body's sex, in the bodies' order (plan 005 phase 4) |
| Passive | From version 5, with the minds: what each animal is up to, its yields' waits too, indexed by slot, and the animals' random numbers |
| Alloy | From version 6, with the items: the smith table's alloy slots, one `crafting_alloy_t` (plan 005 phase 11 part B) |
| Names | The block, drop, item and mob tables' names, as `run/src/save/names/` lists them: what every number above means |

`header.table_hash` covers the names of the block, drop, item and mob tables, in order. `header.sections_hash` covers everything after the header.

## A build with other tables

The file names the tables its numbers are places in. If its `table_hash` is not this build's, `save_state_open` hands it to `translate/`, which rewrites it in memory to this build's numbers, by name, and only then does the usual check look at it. A world keeps every stack and body of a thing that has merely moved. One whose name has gone from a table is taken out, and the log names what went: `translate/README.md` has the table of it. The file on disk stays as it was until the next save writes this image over it.

A state does not need what `run/src/save/world/` needs, a list of its own for ever, as a whole file is written at every save: each is in the numbers of the build that wrote it.

A grid's `recipe` and `output` are not believed from any file, as `recipe_defs` has no name to find a row by: `save_state_apply` works them out from its cells, so a change of recipes neither refuses a world nor leaves it with a grid offering what it no longer makes.

Versions 3, 4, 5 and 6 are read. Version 6 (plan 005 phase 11 part B) adds the alloy section: the two inputs of the smith table's alloy slots and how many have been made, of the smith table's station, each a stack that can be; its output, recipe and heat are written empty and worked out again on opening, as a grid's are. A file without one, as every file before, opens with the slots empty; it is not refused for having one at an older version, as nothing older wrote one. `translate/` translates its two stacks. Version 5 (plan 005 phase 4) adds two sections and changes none: a file with bodies has the sexes, one byte each of `ENTITY_STORE_SEX_` for as many as the bodies say, with zero padding, and a file with minds has the animals'; a file from before has neither, and must not. `save_state_apply` sets every body of an older file to `ENTITY_STORE_SEX_NONE`, as every body then was, and leaves the animals' mind as it started. A translation that takes bodies away takes their sexes away with them, in order. A version 3 file has no calendar section, and must not have one; `save_state_apply` then leaves the caller's calendar as it was, so the world opens on a new one, Year 1, Day 80, week 11, with no census owed, and says so in the log. A version 4 file must have one, with zero padding, fewer than 2^32 days, `D0` below 365, and a last census week the saved clock has reached: at most `(D0 + days + 1) / 7`. A file from before version 2 has no names, and gets `SAVE_WORLD_ERROR_VERSION` and a line in the log. A world that old has been refused for its blocks already.

Same numbers, same bytes: reloading a world, saving it with nothing having ticked and comparing gives no difference, and `tests/run/save/state/` does exactly that.

Nothing in a file is believed until all of it has been looked at: section lengths, counts against capacities, each stack by `item_stack_is_valid`, places finite and in reach, a body's box what physics would accept, every slot belonging to one body or the free ring and never both. A hash only says that bytes are as they were written, and a forged number with a good hash is refused too.

## A save and its blocks are one save

1. The state goes to `state.<g+1>`, through `file_write_atomic`, when blocks are to follow. Nothing refers to it yet.
2. `save_world_commit_write` makes generation g+1. **The manifest's rename is the save, for both.** Before it the world is g's blocks and `state.<g>`, and after it g+1's and `state.<g+1>`.
3. `state.<g>` is removed.

When no block changed, the manifest stays at g, and `state.<g>` is replaced through `file_write_atomic`'s rename, which is the save.

So a kill anywhere leaves a matching pair, and at worst extra files, which `save_state_open` removes once it has read what it needs. It removes only names that are exactly `state.<digits>` or that with `.tmp`.

No file for the current generation means a world saved with no game, such as by `save_world_flush`, or a world of generation 0 nobody has played. Its state starts afresh and it says so. A bad file is another matter: it stops the world from opening and stays as it was.

## What is not in a file

- What a mind last heard from a path search. Its next tick asks again, as after any change of the world.
- Where the player aims, the grids of who stands where, and what the card holds: each is worked out again by a tick.
- `edit_cursor`: a world's edit log starts again when it opens. An edit made after `item_world_tick` in the very tick a save is captured reaches the file as a block change that the stores never heard of: see Stage 16.2's issues.
- Scheduled block ticks, such as water still flowing, in `world/tick`: see the same.
- Whether a screen is up. What is in a crafting grid or on the pointer is kept where it was, so opening a screen after a reload finds it there, and closing one returns it to the inventory.
