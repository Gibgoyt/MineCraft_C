# compile/src/sound_pack/mcsnd

**Purpose:** the `.mcsnd` format, one recorded sound as `sounds/<family>/<name>/index.mcsnd`.
**Tier:** Arena (depth 2), though it allocates nothing. The caller gives it the buffers.
**Owns:** nothing.
**Lifetime:** a `mcsnd_view_t` borrows the file's bytes, and is valid while the caller keeps them.
**Threads:** any. There is no shared state.
**Public functions** (`compile/include/sound_pack/mcsnd/index.h`):

- `mcsnd_parse`: checks every header field, the exact file size and the hash, and gives a view of the samples.
- `mcsnd_encode`: writes a whole file.
- `mcsnd_status_text`: one sentence about a status.

**May be called by:** `sound_pack` and its parts.
**Depends on:** glibc's `memcpy` and `memset`, and `run/include/types/sound_blob/` for the hash and the limits.

## The format

All little-endian. The header is 32 bytes, and `mcsnd_header_t` has the `static_assert`s:

| Offset | Type | Field | Note |
|---|---|---|---|
| 0 | `uint32_t` | `magic` | The bytes `MCSN` |
| 4 | `uint32_t` | `version` | 1 |
| 8 | `uint32_t` | `sample_rate` | 48000 |
| 12 | `uint32_t` | `sample_count` | 1 to 240000 |
| 16 | `uint64_t` | `sample_hash` | FNV-1a 64 of the sample bytes |
| 24 | `uint64_t` | `_pad0` | Zero |

Then exactly `sample_count` mono `int16_t`. Nothing may follow.

The hash catches a damaged or hand-edited file. It is not security.
