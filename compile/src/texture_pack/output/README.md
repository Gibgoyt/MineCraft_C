# compile/src/texture_pack/output

**Purpose:** writes what goes into `textures/`: each texture's `index.mctex`, `index.yaml` and `README.md`, each group's `README.md`, and `ATTRIBUTION.md`.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** nothing outlives a call.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/output/index.h`):

- `tp_output_texture`: one texture's three files.
- `tp_output_groups`: each group's directory and `README.md`.
- `tp_output_attribution`: `ATTRIBUTION.md`: the pack, its authors, commit, licence and coverage, and a row for each texture.
- `tp_output_group_count`, `tp_output_group_at`: the groups, for whoever walks the tree.

**May be called by:** `compile/src/texture_pack/`, and `texture_pack/verify` for the group names.
**Depends on:** the sibling parts `texture_pack/support`, `texture_pack/manifest`, `texture_pack/pack` and `texture_pack/mctex`.

Each file is a pure function of the manifest and the pixels, with no date and no absolute path, so importing twice gives the same bytes and a second run changes nothing. CC BY-SA asks for the author, the source, the licence and a statement of changes, and `ATTRIBUTION.md` and each `README.md` carry all four.
