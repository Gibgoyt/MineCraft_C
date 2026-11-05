# compile/src/sound_pack/blob

**Purpose:** lays out and writes the sound blob from a loaded `sounds/` tree.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** the call.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/sound_pack/blob/index.h`):

- `sound_pack_blob_write`: writes the file, having proved the bytes with `sound_blob_check`, the same check the game uses.

**May be called by:** `sound_pack`.
**Depends on:** `sound_pack/tree`, `compile/src/texture_pack/support/`, and the format in `run/include/types/sound_blob/`.
