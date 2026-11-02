# compile/src/texture_pack/manifest

**Purpose:** reads and checks `textures/import.yaml`, which says which pack, at which commit, under which licence, and what becomes of each of its files.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** the `tp_manifest_t` lasts as long as the arena's contents.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/manifest/index.h`):

- `tp_manifest_load`: reads and checks the file, with a line number on any complaint.

**May be called by:** `compile/src/texture_pack/`.
**Depends on:** `compile/src/yaml/`, and `texture_pack/support`.

## The file

```yaml
pack:
  id: pixel_perfection
  title: Pixel Perfection
  url: https://...                 # https only
  commit: 7935d064...              # the full 40 digits, so that it pins
  licence: CC-BY-SA-4.0            # must be on the allowlist in index.c
  authors: [...]                   # required: the licence demands the credit
  evidence:                        # required: where the pack itself states its licence
    - { file: README.md, contains: "..." }
textures:
  - { from: default/default_dirt.png, to: blocks/dirt }
  - { from: default/default_mineral_coal.png, over: default/default_stone.png, to: blocks/coal_ore }
  - { from: default/default_water_source_animated.png, to: blocks/water_still, frames: 16 }
skip:
  - why: One reason, for all the files below it
    files: [a.png, b.png]
```

| Refused | Why |
|---|---|
| An unknown key | A typo must not silently mean nothing |
| A licence off the allowlist | CC0-1.0, CC-BY-4.0, CC-BY-SA-3.0 and CC-BY-SA-4.0 allow everything we do. NC and ND do not |
| A short or uppercase commit | It would not pin one commit |
| A `from`, `over`, `file` that is absolute or has `..`, a hidden component, or other bytes | These are opened inside an untrusted tree |
| A group other than `blocks`, `items`, `mobs`, `ui`; a name other than `[a-z0-9_]+` | These become output paths |
| Two rows writing one directory, one file imported twice, or both imported and skipped | The output or the coverage count would be wrong |
| `frames` outside 1 to 256; `over` together with `frames` | Compositing is defined for one frame |
