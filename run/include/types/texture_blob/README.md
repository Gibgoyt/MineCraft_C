# run/include/types/texture_blob

**Purpose:** the one definition of the texture blob's format, shared by the tool that writes it and the game that reads it.
**Tier:** Borrow in spirit: a header of types and arithmetic, with no memory of its own and no source file.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any.
**Public functions** (`run/include/types/texture_blob/index.h`):

- `texture_blob_level_dimension`, `texture_blob_level_count`, `texture_blob_level_frame_bytes`: the size of a mip level.
- `texture_blob_align_up`: rounds an offset up to a power of two.

**May be called by:** `compile/src/texture_blob/`, `run/src/texture/`, and tests.
**Depends on:** nothing.

## Why it is under types

`plans/REPO_STRUCTURE.md` section 11 says `compile/` may include from `run/` only under `types/`, and that `run/` never includes from `compile/`. A blob needs one layout on both sides, and a second copy would be free to drift. The `static_assert`s on every offset then guard it in both programs.
