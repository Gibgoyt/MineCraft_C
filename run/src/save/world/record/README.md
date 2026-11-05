# run/src/save/world/record

**Purpose:** one section's record: the bytes a delta file holds for it, from states and back to states, and whether a record is one a reader will take.
**Tier:** Borrow: everything comes from the caller, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept here.
**Threads:** any. It touches only what it is given.
**Public functions** (declared with the rest of `save/world`, in `run/include/save/world/index.h`): `save_world_record_encode`, `save_world_record_decode`, `save_world_record_decode_within`, `save_world_record_valid_within`, `save_world_record_levels`. `save_world_record_decode` holds a record to this build's table, and only tests use it: a world holds it to the length of its own list, with the `_within` forms.
**May be called by:** `save/world`, and tests. `save_world_record_valid_within` is also how a save decides what it may carry over from an old file: a good checksum says only that a record is what was written.
**Depends on:** `save/world`'s header, `world/section`, `world/tick` for the size of a page of levels, and the generated block table.

The format is in `run/src/save/world/README.md`.
