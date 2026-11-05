# run/src/save/names

**Purpose:** a table's names as a list on disk, and what the numbers of a saved list mean in this build. It is what lets a block or an item be added without ending a world.
**Tier:** Arena (depth 2), and it allocates nothing: it works in the bytes its caller gives it.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any. It keeps nothing between calls.
**Public functions** (`run/include/save/names/index.h`):

- `save_names_live_count`, `save_names_live_name`: this build's four tables, by kind.
- `save_names_write`, `save_names_write_all`: them as lists.
- `save_names_read`, `save_names_are_live`, `save_names_next`: a list looked at, trusting nothing, and compared with this build's.
- `save_names_map`, `save_names_lost_log`: a list's numbers in this build's, and telling the log which it does not have.

**May be called by:** `save/world`, `save/state`, `save/state/translate`, and their tests.
**Depends on:** `log`, `assert`, and the generated block, item and mob tables.

## On disk

    uint32_t count, uint32_t bytes        a save_names_head_t
    name NUL name NUL ...                 bytes of them, count names
    zeros                                 up to 8

A name is 1 to 64 of `a-z 0-9 _`, the only characters a table's names have, since a name from a file goes into a log. A list that is anything else is not read. `bytes` of names hash, by FNV-1a, to what a world's and a state's `table_hash` has always been.

What a saved number means is its place in the list saved with it, and that place in this build is found by name. So a list says what the file holds, and this build's numbering can move as much as it likes. See `run/src/save/world/README.md` and `run/src/save/state/README.md` for what each does with it.
