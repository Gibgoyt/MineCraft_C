# compile/src/texture_pack/default_pack

**Purpose:** `texture_pack import-default`: Minecraft's default textures, imported into a git-ignored copy of `textures/` for use on this machine only. Plan 005 phase 9.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** nothing outlives a call.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/default_pack/index.h`):

- `tp_default_import`: reads the map, checks the pin, checks that the map and the tree name the same textures, decodes and checks every entry, and only then writes.

**May be called by:** `compile/src/texture_pack/`, for `texture_pack import-default --pack <unpacked> --map textures/import_minecraft_default.yaml --textures textures_local`. `tools/import_minecraft_default.sh` runs it on a fresh copy. **The build never runs it.**
**Depends on:** `compile/src/yaml/`, and the sibling parts `texture_pack/support`, `texture_pack/mctex`, `texture_pack/pack` (`tp_pack_decode_image`, `tp_pack_composite_over_opaque`) and `texture_pack/output` (the group names).

## Why it is not `import`

`import` translates an open pack into `textures/`, which is committed. Mojang's art may not be published, and this repository is public, so it goes only into a copy that git ignores, and nothing committed changes. The pack is a client jar's `assets/minecraft/textures/`, with no git, no licence file to quote, and some 4000 files, most of which this game has no use for, so the commit pin, the licence evidence and the coverage walk of `import` do not apply. In their place:

- **Pin.** `tools/fetch_minecraft_default.sh` checks the jar against the SHA-1 it pins, and only then writes `mc_c_pin.txt`, `<version> <sha1>`, beside what it unpacked. The import requires exactly the map's `pack.version` and `pack.client_sha1` there.
- **Coverage of ours, not theirs.** Every texture directory in the tree is named by the map once, with a `from`, a `stack` or a `keep`, and every name in the map is in the tree. So a texture a later stage adds is refused until someone decides where its Minecraft picture is.
- **Size.** What comes out must be the width and height of ours, since a screen lays out its pictures by ours and a mob's skin by its layout. Its frame count must be ours, or 1: an animation may become a still, since the game draws frame 0 of everything but the crack, which must keep all its frames.

## The map

`textures/import_minecraft_default.yaml` has `pack` (id, title, version, client SHA-1, licence text), `tints` (names to `RRGGBB`) and `textures`. Each entry has `to`, our `group/name`, and one of:

- `from`: a file under `assets/minecraft/textures/`, with optional `crop: [x, y, width, height]` taken first, `tint` (a name from `tints`), `over` (an opaque file composited under it, after the tint) and `frames` (a vertical strip after the crop);
- `stack`: 2 to 32 files of one size, each one frame, top to bottom;
- `keep`: why ours stays.

A tint multiplies each colour channel by the tint's, over 255, rounded to nearest, in integers: Minecraft's grass, leaves, vines and water are grey, and coloured by biome when drawn, and our renderer has no biome tint yet.

## What it writes

For each `from` or `stack` entry, `index.mctex`, `index.yaml` with `source.kind: pack`, `pack: minecraft_default`, the version, the licence text and the file, and a `README.md` stating the changes. A `keep` entry's three files are left exactly as the copy had them. Every file goes through `tp_write_if_changed`, so a second run changes nothing. It writes no `ATTRIBUTION.md`: the copy keeps the committed one, whose section "What this file cannot promise" describes this tree.

## What the pack can and cannot do to us

The same as for `import`, by the same code: every pack path is a safe relative path from the map, read through `tp_read_contained`, which follows no symbolic link and refuses anything but a regular file; nothing from the pack is run; only decoded pixels reach a file, and the PNG decoder is bounds-checked and limited before it allocates. Output paths come only from the tree's own directory names, matched against the map.
