# compile/src/texture_pack/mctex

**Purpose:** the `.mctex` format, one translated texture as `textures/<GROUP>/<NAME>/index.mctex`.
**Tier:** Arena (depth 2), though it allocates nothing. The caller gives it the buffers.
**Owns:** nothing.
**Lifetime:** a `mctex_view_t` borrows the file's bytes, and is valid while the caller keeps them.
**Threads:** any. There is no shared state.
**Public functions** (`compile/include/texture_pack/mctex/index.h`):

- `mctex_parse`: checks every header field, the exact file size and the hash, and gives a view of the pixels.
- `mctex_encode`: writes a whole file.
- `mctex_pixel_bytes`: the pixel size of some dimensions, or false if they are over the limits.
- `mctex_hash`: FNV-1a 64.
- `mctex_status_text`: one sentence about a status.

**May be called by:** `texture_pack` and its parts, and `compile/src/texture_vector/`, which reads these files.
**Depends on:** glibc's `memcpy` and `memset`.

## The format

All little-endian. The header is 32 bytes, and `mctex_header_t` has the `static_assert`s:

| Offset | Type | Field | Note |
|---|---|---|---|
| 0 | `uint32_t` | `magic` | The bytes `MCTX` |
| 4 | `uint32_t` | `version` | 1 |
| 8 | `uint32_t` | `width` | Of one frame, 1 to 4096 |
| 12 | `uint32_t` | `height` | Of one frame, 1 to 4096 |
| 16 | `uint32_t` | `frame_count` | 1 to 256 |
| 20 | `uint32_t` | `_pad0` | Zero |
| 24 | `uint64_t` | `pixel_hash` | FNV-1a 64 of the pixel bytes |

Then exactly `width * height * frame_count * 4` bytes, at most 64 MiB: RGBA8, not premultiplied, frame 0 first, each frame top row first, with no gap between rows. Nothing may follow.

The file is deliberately not compressed. Git compresses it on its own, nothing reads it at run time, and a format this plain leaves little to get wrong. It holds no palette, mean or mips: those are derived in `texture_vector`, and `embed_assets` embeds that result.

The hash catches a damaged or hand-edited file. It is not security: nothing here defends against someone who means it, and it does not need to, since these files come out of this repository.
