# run/src/save/world

**Purpose:** a world on disk as its seed and the sections that changed: delta files with versioned headers, replaced only through a rename.
**Tier:** Arena: everything it holds comes from the arena `save` gives it, for one world.
**Owns:** nothing on the heap. One `flock`ed descriptor, from `save_world_open` to `save_world_close`, and one mutex.
**Lifetime:** World.
**Threads:** see below. Only the thread holding the map captures; any one thread writes a save; any thread reads.
**Public functions** (`run/include/save/world/index.h`):

- `save_world_open`, `save_world_close`: a pair. Open refuses a world it cannot be sure of, and writes nothing over it.
- `save_world_capture_dirty`, `save_world_capture_column`: copy changed sections out of the world, flags and all.
- `save_world_commit_begin`, `save_world_commit_write`: one save, split so that the writing can be on a worker.
- `save_world_flush`: capture and write until nothing is unsaved.
- `save_world_column_overlay`: what is saved of a column, over what generates there. `save_world_column_overlay_levels`: and its levels, for `world_tick_section_levels_restore`.
- `save_world_levels_from`: where captures read levels from, which `game_open` sets.
- `save_world_far_line`: for the far field, on workers: what is saved of one line of blocks, over what it sampled.
- `save_world_far_next`, `save_world_far_lost_take`: which columns have been captured since the far field last asked, through a ring from the sim to the gpu thread; or that it lost count and should look at everything.
- `save_world_record_encode`, `save_world_record_decode`, `save_world_record_decode_within`, `save_world_record_valid_within`, `save_world_record_levels`, in `record`, and `save_world_table_hash`: the record format and what a build's numbers mean.
- `save_world_status_name`, `save_world_step_hook_set`: a status for a log line; a test's way to die at a known step.

**May be called by:** `save`, `save/autosave`, and `game`, which runs it.
**Depends on:** `file`, `arena`, `log`, `assert`, `save/names`, `world/edit`, `world/column`, `world/section`, `world/tick`, `TERRAIN_GENERATOR_VERSION` from `terrain`'s header, the generated block table, and glibc's `opendir`, `flock`, `qsort` and pthreads.

## On disk

    <world>/world.meta                 run/src/ui/menu/catalog's: the name, and the seed the menu opens it with
    <world>/blocks/manifest            what is saved: the seed, a list of regions and the names of the blocks, in one atomic file
    <world>/blocks/r.<x>.<y>.<g>       a delta file: the changed sections of 32 x 32 columns, written by save number g
    <world>/blocks/damaged.<x>.<y>.<g> a delta file a later save replaced and could not wholly read: kept for whoever wants what is left
    <world>/blocks/lock                empty; held with flock while a game has the world

A world is `terrain_generate_box` of its seed, with these sections written over the result. A section that has not changed is not on disk at all, and a world nobody has touched is one manifest of its 64 byte header and its list of names. Blocks are kept, and the levels of what flows among them, since a voxel of fluid with no level is a source and a world read back without them floods. Light is worked out again when a column comes in, as for one that has never been saved, and that is why a change of light alone never dirties anything for saving. A level changing with no block changing flags its section itself: see `run/src/world/tick/`.

All numbers are little-endian. Every struct, its size and each field's offset are in the header, and no byte is left to a compiler's padding. A file's `version` is `SAVE_WORLD_VERSION`. Each file has a checksum of its header, and each column's list of sections and each section's record have their own, so that one bad byte costs what it is in and is never believed.

A record is `uint16_t bits, uint16_t palette_count`, the palette in the order states first appear in `section_voxel_index` order, then one index per voxel, packed from the low bits of each byte. Bits is 0, with one state; 1, 2, 4 or 8, the fewest that hold the palette, up to 256 states; or 16, with no palette and the 4096 states themselves. So the same blocks are always the same bytes, whatever forms the section went through in memory. A section with any flowing fluid has the top bit of `bits` set and ends in `world_tick_section_levels`' 2048 bytes, half a byte per voxel, each nibble 0 to 8 or the record is refused. Version 1 had no levels.

A manifest ends in a list of names, in the form `run/src/save/names/` writes, and a state number in any record of any delta file is a place in that list, not in this build's table. Numbers are given out once and stand for ever: a world starts with the table of the build that made it, and later names only ever go on the end.

- **A build with a new block**, wherever it falls in the alphabet, opens the world, and finds each number's block by name. The new block joins the list in memory, at the next free number, and reaches the disk in the same manifest as the first delta that could use it. No delta file is rewritten for this, and a record carried from an old file to its replacement keeps its bytes.
- **A build without a block** the list names opens the world too. That block reads as air, a warning names it, and a section is air there only once something else in it changes and it is saved. Its name stays in the list, and a record nothing has touched has it still, so that a later build that has it back finds it where it was.
- **The maps** are made once, at open, and only read after: `borrowed_file_to_live` for what comes out of a record, `borrowed_live_to_file` for what goes in. A world whose list is this build's has `names_same`, and neither is used, so that the record of the same blocks is the same bytes it would have been.

`manifest.table_hash` is a hash of the names in that list. `regions_checksum` covers the list as well as the regions, so a damaged one is refused as any other damage. A hash that does not match a list whose checksum is good is an edit, and it gets `SAVE_WORLD_ERROR_TABLES`. So does a list too full for this build's new names to join, which at 16384 names or half a megabyte is far off.

A world from before version 5 has no list, and nothing says what its numbers mean: it gets `SAVE_WORLD_ERROR_VERSION`, a line in the log saying so, and stays as it was. There is no migration, as those worlds were all test worlds that had been deleted when this came in.

`manifest.generator` is the `TERRAIN_GENERATOR_VERSION` of the land the deltas were cut from. A column is that land with what the deltas say over it, so on another generator's land a saved edit would meet a seam, or lie inside a new mountain. Such a world gets `SAVE_WORLD_ERROR_GENERATOR`, and the list says why. The format before version 4 could not say, and each such world was made on an older shape, so it gets the same answer. There is no migration: it stays on the disk as it was.

## A save, and what a crash does to one

1. For each region touched, in order: the old file's sections with the new ones over them go to `r.x.y.g.tmp`, which is synced, closed and renamed to `r.x.y.g`, a name nothing refers to yet.
2. The directory is synced, so those names are on the disk.
3. `manifest` is replaced through `file_write_atomic`: temporary, sync, rename, sync of the directory. **That rename is the save.** Before it the world is the last save, whole, and after it this one, whole.
4. The files it replaced are removed.

A kill or a power cut anywhere leaves extra files at worst. `save_world_open` removes `*.tmp` and any `r.*` the manifest does not name. It never removes `damaged.*`, or a name it does not make. No world is ever a mix of two saves, and `run/src/save/state/` keeps its file beside a generation.

`save_world_open` refuses, changing nothing, for: a bad header or list, another version, another seed, another generator, a list of blocks that is not what its manifest says, a file the manifest names being missing or of the wrong size, and delta files with no manifest. It also refuses a world another game has open. None of these can be a crash's doing. A bad section or column in a file that is otherwise fine does not stop a world: that place gets what generates there, it is logged and counted in `sections_damaged`, and the next save of that region drops it and keeps the old file as `damaged.*`. That goes for a record whose checksum is good and whose contents no reader takes, too. A manifest at the last generation there is, which only an edit can make, opens, and every save of it fails with `SAVE_WORLD_ERROR_TOO_MANY` and leaves it as it was, as the count going round would make a world that will not open.

## Who does what, when

| Call | Thread | Takes |
|---|---|---|
| `save_world_capture_*`, `save_world_commit_begin`, `save_world_flush` | The one holding the map | The mutex, per section, for a store of a few words |
| `save_world_commit_write` | Any one, not during another or a begin | The mutex at the end, for a swap. No lock while it does I/O |
| `save_world_column_overlay` | Any | The mutex to look at what is on its way, and to copy one region's record. Never during I/O |

A section goes one way: the set being captured to, then the set being written, then the current manifest's file. A write publishes the new manifest and retires its set in one step under the mutex. A reader looks at both sets first and at the manifest after, so a section that has left the sets by the time it looks is in the manifest it sees then. A reader whose file has been replaced under it gets `FILE_ERROR_NOT_FOUND` and looks again at the newer manifest. Only a column not in the map may be read like this, since edits to a live one are not here.

Capture calls no heap and does no I/O. Each set holds 64 MiB or 32768 sections, whichever comes first. Past that a capture says `SAVE_WORLD_FULL` and leaves the flags alone, so nothing is lost: the caller writes and tries again. `game_sim_step` asks the autosave for that when a column on its way out will not fit, and keeps the column until it is captured.

A failed write keeps its set and is tried first the next time, and in the meantime readers still see it, so the game plays on with its changes intact, and a world whose disk is full loses nothing until it is closed. All this memory is about 138 MiB of address, and pages cost nothing until touched.

## What this does not do

Only blocks are here, and it does not decide when. `run/src/save/autosave/` calls `save_world_capture_dirty` and `save_world_commit_begin` on the sim and `save_world_commit_write` on a worker, with `run/src/save/state/` keeping the rest of a world beside a generation, and it is the only writer while a game runs. `save_world_flush` remains for a world with no game.

## Limits

65536 regions in one world, each half a kilometre on a side, with nothing charged to a region with no change. Past that a save fails and says so. A region file is 216 MiB at the very worst with a list of up to 256 names, of 49152 sections of more than 16 different states each, and 384 MiB once a list passes 256; it is rewritten when any of it changes, and it is read whole only then.
