# asm/src/embed_assets

**Purpose:** links the generated texture blob into the program through `incbin`, so that a run needs no files.
**Tier:** Borrow in spirit: read-only data, with no code.
**Owns:** nothing. The bytes are part of the program's image.
**Lifetime:** Process.
**Threads:** any. The bytes never change.
**Public functions:** none, only data (`run/include/simd/embed_assets/index.h`):

- `embed_assets_texture_blob`: the blob, on a 64-byte boundary.
- `embed_assets_texture_blob_bytes`: its length.

**May be called by:** `run/src/texture/` only. Everything else asks `texture`.
**Depends on:** `build/<preset>/generated/textures/index.mctb`, which `cmake/generated.cmake` makes. NASM's `-MD` depfile lists what it `incbin`s, so a new blob rebuilds this. Stage 06.4 tested that by changing a texture.
