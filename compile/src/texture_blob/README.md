# compile/src/texture_blob

**Purpose:** makes the mip chains, and packs every texture and its vector data into the one blob the game embeds.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its part is given buffers from it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/texture_blob/index.h` holds its exit statuses:

- `texture_blob --textures textures --vectors <directory> --out <file>`: reads each `<textures>/<group>/<name>/index.mctex` and `<vectors>/<group>/<name>/index.mcvec`, refuses a `.mcvec` not made from that `.mctex`, and writes the blob through a temporary and a rename. The same input gives the same bytes.

**May be called by:** `cmake/generated.cmake`, after `texture_vector vectors`, and ctest `compile_texture_blob`.
**Depends on:** `mip/`, `compile/src/texture_pack/` parts `support`, `mctex` and `output`, `compile/src/texture_vector/mcvec/`, and `run/include/types/texture_blob/`, which defines the format.

## Parts

| Part | Job |
|---|---|
| `mip` | One texture's chain. Its README has the rules |
