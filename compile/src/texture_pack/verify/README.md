# compile/src/texture_pack/verify

**Purpose:** checks a `textures/` tree against the manifest, with no pack and no network.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** nothing outlives a call.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/verify/index.h`):

- `tp_verify_tree`: prints every problem it finds, and returns true if there were none.

**May be called by:** `compile/src/texture_pack/`.
**Depends on:** `compile/src/yaml/`, and the sibling parts `texture_pack/support`, `texture_pack/manifest`, `texture_pack/mctex` and `texture_pack/output`.

## What it checks

- **`ATTRIBUTION.md`** states the licence, its URL, the commit, the source and the title, credits each author, and has a row for each texture.
- **Every texture directory**, whatever its source, has a `README.md` and an `index.yaml` that parses. Each group has a `README.md`, and no other directory sits beside the groups.
- **Each directory with `source.kind: pack`** must be in the manifest, its `index.yaml` must say what the manifest says, its `README.md` must state the licence, and its `index.mctex` must parse, match its hash, and be the size its `index.yaml` says.
- **Each texture the manifest imports** must be there.

A directory with any other `source.kind`, such as Stage 06.3's recipes, needs only its two files: what a recipe holds is that stage's to check. Extra keys in an imported `index.yaml` are allowed here, though a re-import replaces that file.
