# run/include/types/sound_blob

**Purpose:** the one definition of the sound blob's format and of the sound families, shared by the tools that write and name them and the game that reads them.
**Tier:** Borrow in spirit: a header of types and arithmetic, with no memory of its own and no source file.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any.
**Public functions** (`run/include/types/sound_blob/index.h`):

- `sound_family_name`, `sound_kind_name`: the words `config/blocks/` and `sounds/` use.
- `sound_blob_check`: checks a whole blob, hash included, and gives a view of it.
- `sound_blob_find`: the variants of one family and kind.
- `sound_blob_samples`: an entry's samples.
- `sound_blob_hash`, `sound_blob_align_up`: FNV-1a 64, and rounding an offset up to a power of two.

**May be called by:** `compile/src/sound_pack/`, `compile/src/game_tables/blocks/`, the generated `tables/blocks/index.h`, `run/src/audio/`, and tests.
**Depends on:** nothing.

## Why it is under types

`plans/REPO_STRUCTURE.md` section 11 says `compile/` may include from `run/` only under `types/`, and that `run/` never includes from `compile/`. The blob needs one layout on both sides, and the block table and the sound tree have to mean the same family by the same number. The `static_assert`s guard the layout in every program.
