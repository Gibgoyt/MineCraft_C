# run/src/texture/registry

**Purpose:** finds a texture's id from its name.
**Tier:** Arena (depth 2): takes its table from an `arena_t *` that `run/src/texture/` gives it.
**Owns:** nothing. The table lies in its parent's arena.
**Lifetime:** Process.
**Threads:** `main` for init and deinit. After that it only reads, so any thread may look up.
**Public functions** (`run/include/texture/registry/index.h`):

- `texture_registry_init`: builds the table.
- `texture_registry_deinit`: empties the handle.
- `texture_registry_find`: name to id.

**May be called by:** `run/src/texture/`.
**Depends on:** `arena`, `run/src/texture/blob/`. It uses that sibling's types and its hash, which `plans/REPO_STRUCTURE.md` section 11 requires this line to say: `texture/blob`.

A blob's entries are sorted, so a binary search would need no table. A hash table is here because names are looked up as tables load, once per block and mob, and because a miss, which is what a typo gives, then costs one short probe.
