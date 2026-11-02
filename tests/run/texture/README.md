# tests/run/texture

**Purpose:** ctest `run_texture`, covering `run/src/texture/` with `blob` and `registry`, and `asm/src/embed_assets/`: the blob linked into this program is the one the build made, every texture in `textures/` is in it and can be found, and damage is refused.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `run_texture`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/texture/`, `asm/src/embed_assets/`, the committed `textures/`, and `build/<preset>/generated/textures/`. It needs no server, no display, no network, and no pack.

## What it checks

- **The embedding.** Same bytes as the generated file, on a 64-byte boundary.
- **Lookup.** The texture directories under `textures/`, counted here independently, are each found under their own name and no other; near misses are refused; ids follow the names.
- **Every texture, against its source files.** Level 0's RGB is byte for byte its `.mctex`, each alpha is what its class promises at every level, every level's size is right, the vector data is byte for byte its `.mcvec`, and each mip lies inside the pixels section.
- **Real textures.** The pack's leaves are holes and stay roughly as dense down the chain, a lit furnace is opaque, and an animated texture has distinct frames. The last level of an opaque, square, one-frame texture agrees with `mean_rgba`, which `texture_vector` works out by its own path.
- **Damage.** About thirty fields of the header and entries, each changed with its hash mended so the check behind it is what answers.
- **Owner.** Init and deinit balance the heap, a refused blob leaves nothing behind, and a zeroed `texture_t` is safe to look in and to deinit.
