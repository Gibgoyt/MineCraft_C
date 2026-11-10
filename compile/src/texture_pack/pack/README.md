# compile/src/texture_pack/pack

**Purpose:** everything that looks at the untrusted pack: its pinned commit, its licence, whether the manifest accounts for each file in it, and decoding one texture.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** a `tp_image_t` lasts until the caller resets the arena it was loaded with.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/pack/index.h`):

- `tp_pack_check_commit`: reads `.git/HEAD`, through a loose ref or `packed-refs`, and requires the manifest's commit.
- `tp_pack_check_licence`: requires every evidence text, and that `texture_pack.conf`, if it states a licence, states that one.
- `tp_pack_check_coverage`: walks the pack. Every file must be imported, skipped, a base under another texture, or evidence, and every file the manifest names must exist.
- `tp_pack_load_entry`: reads, decodes, splits into frames, and composites one entry.
- `tp_pack_decode_image`: reads and decodes one PNG, as one frame, under the same limits.
- `tp_pack_composite_over_opaque`: the `over` sum on its own.

**May be called by:** `compile/src/texture_pack/`, and its part `default_pack`, which uses only the last two.
**Depends on:** `compile/src/png/`, and the sibling parts `texture_pack/support`, `texture_pack/manifest` and `texture_pack/mctex`.

## Notes

The walk uses an explicit stack, since recursion is banned, and it skips only `.git` and `.gitignore`, and only at the top of the clone. A symbolic link counts as a file: it must be listed like any other, and it is only ever skipped, because `tp_read_contained` will not follow it.

`over` composites one image on an opaque base of the same size. The sum is done in integers with rounding to nearest, so it gives the same bytes on any machine, and a base with any transparency is refused rather than guessed at.

Limits, all checked before the memory is taken: 16 MiB for a PNG file, 64 MiB of pixels (`MCTEX_MAX_PIXEL_BYTES`), and 256 MiB of decoder work.
