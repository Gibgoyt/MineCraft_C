# compile/src/texture_pack

**Purpose:** translates a pinned clone of an open texture pack into `textures/`, and checks that a `textures/` tree is whole with no pack at all.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its parts borrow it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/texture_pack/index.h` holds only its exit statuses:

- `texture_pack import --pack <clone> --manifest textures/import.yaml --textures textures`
- `texture_pack verify --manifest textures/import.yaml --textures textures`
- `texture_pack import-default --pack /tmp/mc_c_assets/minecraft_default --map textures/import_minecraft_default.yaml --textures textures_local`: Minecraft's default textures into an ignored copy, for this machine only. Its part `default_pack` has the rules

**May be called by:** a person re-importing, and ctest `compile_texture_pack`. **The build never runs it**, so a build needs neither the pack nor a network.
**Depends on:** its parts `support`, `mctex`, `manifest`, `pack`, `output`, `verify` and `default_pack`, and through them `compile/src/png/` and `compile/src/yaml/`.

## What an import does, in order

Nothing is written until step 5, so a refused pack leaves `textures/` exactly as it was.

1. **Manifest.** Reads and checks `textures/import.yaml`: names, paths, duplicates, and a licence from our allowlist.
2. **Pin.** Reads the clone's own `.git/HEAD` and refuses any commit but the manifest's. It starts no `git` process.
3. **Licence.** Requires each evidence text in the pack, and that its `texture_pack.conf` states that licence.
4. **Coverage and decode.** Every file in the pack must be imported, skipped with a reason, or licence evidence. A licence-like file no one has reviewed is refused. Then every texture is decoded, to see that it can be.
5. **Output.** Writes each texture, then `ATTRIBUTION.md`, through a temporary and a rename. A file that already holds the right bytes is not touched.
6. **Verify.** Runs the same check `verify` runs on the tree just written.

## What the pack can and cannot do to us

The pack is data from a stranger, so the only thing taken from it is decoded pixels.

- **It runs nothing.** No `git`, no shell, no interpreter, and no file of it is ever executed.
- **It cannot read outside itself.** Every step of every path is opened with `O_NOFOLLOW`, so a symbolic link is refused rather than followed, and a FIFO is refused without blocking.
- **It cannot write anywhere.** Output paths are built only from the manifest, whose names are `[a-z0-9_]+`, and from fixed names.
- **It cannot inject text.** A file name outside `[A-Za-z0-9._-]` stops the import. `README.md`, `index.yaml` and `ATTRIBUTION.md` hold manifest text only, and no pack text is echoed in a message.
- **It cannot exhaust memory.** Files, pixels and decode work each have a limit, checked before the memory is taken. `compile/src/png/` is bounds-checked and refuses malformed files.

**What it does not prove:** the pin checks `HEAD`, not the working files. An edited clone at the right commit would be imported, and the change would then show up as a diff in `git status` in this repository, since outputs are committed.

## Parts

| Part | Job |
|---|---|
| `support` | The arena, safe file reads, text and path checks, and writes |
| `mctex` | The `.mctex` format, which `texture_vector` also reads |
| `manifest` | `textures/import.yaml` |
| `pack` | Everything that touches the untrusted pack |
| `output` | Everything written into `textures/` |
| `verify` | The check of a tree, with no pack |
| `default_pack` | `import-default`: Minecraft's default textures into a copy that is never committed |

To re-import, see `textures/README.md`.
