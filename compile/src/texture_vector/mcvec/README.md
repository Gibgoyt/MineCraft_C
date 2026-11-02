# compile/src/texture_vector/mcvec

**Purpose:** the `.mcvec` format, one texture's vector data, written to `build/<preset>/generated/textures/` and never committed.
**Tier:** Arena (depth 2), though it allocates nothing. The caller gives it the buffers.
**Owns:** nothing.
**Lifetime:** an `mcvec_view_t` borrows the file's bytes, and is valid while the caller keeps them.
**Threads:** any. There is no shared state.
**Public functions** (`compile/include/texture_vector/mcvec/index.h`):

- `mcvec_file_bytes`: the size of a file with these counts, or false if they are out of range.
- `mcvec_encode`: writes a whole file, filling in magic, version, padding and hash.
- `mcvec_parse`: checks every header field, the exact size, the hash and every index, and gives a view of each section.
- `mcvec_status_text`: one sentence about a status.

**May be called by:** `compile/src/texture_vector/`, its test, and Stage 06.4's blob step.
**Depends on:** `compile/src/texture_pack/mctex/` for its limits and its FNV-1a hash.

## The format

All little-endian. The header is 64 bytes, and `mcvec_header_t` has the `static_assert`s, with the meaning of each field. Then, with no gaps and `N = width * height * frame_count`:

| Section | Bytes | Contents |
|---|---|---|
| palette | `palette_count * 4` | RGBA8 |
| indices | `N` | Into the palette |
| height | `N` | 0 to 255 |
| normal | `N * 2` | `int8_t` x then y, each scaled by 127. z is positive, `sqrt(1 - x*x - y*y)` |
| emissive | `N` | A light level from 0 to 15 |

`source_hash` is the `pixel_hash` of the `.mctex` it came from, so anything can tell a stale file from a current one.
