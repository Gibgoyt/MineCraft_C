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

**May be called by:** `save`, `save/autosave`, and `game`, which opens a world with it.
**Depends on:** `save/world`, for its statuses, `file`, `arena`, `log`, `assert`, the stores in `player`, `item`, `entity` and `mob`, whose fields it reads and writes as a file's numbers, the generated tables, and glibc's `opendir`.

## What a file may say, and what the game then writes

`save_state_check` asks more than that a number be finite: it refuses what the game could not then live with, as a state with good hashes can be an edited one. A speed past `SAVE_STATE_SPEED_MOST`, which physics would stop the program for, an eye past `SAVE_STATE_EYE_MOST`, a fall or a body's tally past `SAVE_STATE_TALLY_MOST`, or air the game never has. What is alive can pass those limits, as an arrow keeps flying, so `save_state_capture` writes such a number at its limit, and something that is not a number as 0. The game goes on with what it has, and the file is one it believes.

## On disk

    <world>/state.<g>       this file; g is the save of the blocks it goes with
    <world>/state.<g>.tmp   one on its way there, which is never believed

All numbers are little-endian. A file is a `save_state_header_t`, then sections, each a `save_state_section_t` and its bytes, padded with zeros to 8. Section ids and their contents are in the header, with every size and offset asserted. A section is there at most once. The time is always there, and the rest only if the game that wrote it has that part: a game with no mobs writes no minds, and a file's minds are not used by one. An id that version 1 does not have is damage.

| Section | Holds |
|---|---|
| Time | World ticks, the time of day, where the player looks |
| Player | Body, health, hands and swing, as they are |
| Items | Inventory, the two crafting grids, armor, what the pointer carries, and the books |
| Chests, furnaces | Each one, by the cell it stands in |
| Drops, arrows | Each one, and how the next will scatter |
| Bodies | Every array of the entity store for its bodies, the slot tables, the numbers blasts go on with |
| Spawn, minds | What each mind is up to, indexed by slot, and each one's random numbers |

`header.table_hash` covers the names of the block, drop, item and mob tables, in order. A build that moves one would read other things from the same numbers, so it refuses the file. `header.sections_hash` covers everything after the header.

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
