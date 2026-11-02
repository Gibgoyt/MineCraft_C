# run/include/simd/embed_assets

**Purpose:** declares for C the data that `asm/src/embed_assets/` links in.
**Tier:** Borrow in spirit: a header, with no source file of its own here.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. The bytes never change.
**Public functions:** none, only `embed_assets_texture_blob` and `embed_assets_texture_blob_bytes`.
**May be called by:** `run/src/texture/` and its test. Everything else asks `texture`.
**Depends on:** `asm/src/embed_assets/`.
