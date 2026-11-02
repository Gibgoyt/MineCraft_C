# compile/src/png

**Purpose:** our own bounds-checked PNG decoder for the build-time tools. Texture packs are untrusted input.
**Tier:** Owner (depth 1 of `compile/src`), though it allocates nothing.
**Owns:** nothing. The caller gives it the file, the pixels and the work memory, and `png_probe` says how much is needed.
**Lifetime:** nothing outlives a call.
**Threads:** any thread. There is no shared state.
**Public functions** (`compile/include/png/index.h`):

- `png_probe`: checks the chunk structure and every CRC, and reports the image's properties and memory needs.
- `png_decode`: decodes a file to 8-bit RGBA, top row first, or refuses it and zeroes the pixels.
- `png_status_text`: one sentence about a status, for messages.

**May be called by:** other `compile/` tools, first `texture_pack`. Nothing in `run/`.
**Depends on:** `compile/src/inflate/`, and libc's `memcpy`, `memset`, `memcmp` and `vsnprintf`.

## What it decodes

Every PNG color type at every depth PNG defines: gray 1/2/4/8/16, RGB 8/16, palette 1/2/4/8, gray+alpha 8/16, RGBA 8/16.
Adam7 interlacing, all five filters, `PLTE`, `tRNS`, and IDAT split across any number of chunks, empty ones included.

The output is always RGBA8, not premultiplied. 16-bit samples become `(v * 255 + 32767) / 65535`, and lower depths scale so
their maximum is 255. A `tRNS` color key compares full-depth samples, before any scaling.

**No color management.** `gAMA`, `sRGB`, `cHRM` and `iCCP` are ignored and the stored values are returned: a texture's bytes
are what its author wrote. Ancillary chunks are skipped, after their CRC is checked. APNG frames are ignored, so an animated
PNG gives its default image.

## What it refuses

It is stricter than libpng, since a build should stop on a bad texture rather than quietly use it. Each status comes with a
message and a file offset in `png_error_t`.

| Refused | Status |
|---|---|
| A wrong signature | `PNG_BAD_SIGNATURE` |
| A file ending inside a chunk or before `IEND` | `PNG_TRUNCATED` |
| A chunk length over 2^31 - 1, or a type that is not four letters | `PNG_BAD_CHUNK` |
| A wrong CRC on any chunk, ancillary ones included | `PNG_BAD_CRC` |
| `IHDR` not 13 bytes, a zero dimension, an undefined depth, color, compression, filter or interlace value | `PNG_BAD_HEADER` |
| A dimension over `PNG_MAX_DIMENSION` (32768) | `PNG_TOO_LARGE` |
| `IHDR` not first or repeated, `PLTE` or `tRNS` repeated or after `IDAT`, `tRNS` before `PLTE`, split `IDAT`, no `IDAT`, non-empty `IEND`, bytes after `IEND` | `PNG_BAD_ORDER` |
| A critical chunk it does not know | `PNG_UNKNOWN_CRITICAL` |
| `PLTE` on gray, missing on a palette image, a size that is not 3 to 768 in threes, more entries than the depth allows | `PNG_BAD_PALETTE` |
| `tRNS` on an image with alpha, the wrong size, longer than the palette, or a color key over the depth | `PNG_BAD_TRANSPARENCY` |
| IDAT that fails to inflate, inflates to too little or too much, has bytes after its zlib stream, or is too small to ever expand to the claimed size | `PNG_BAD_DATA` |
| A row's filter type above 4 | `PNG_BAD_FILTER` |
| A pixel's palette index past the end of `PLTE` | `PNG_BAD_INDEX` |
| Buffers smaller than `png_probe` reports | `PNG_NO_ROOM` |

## How a caller uses it

```c
png_info_t info;
png_error_t error;
if (png_probe(file, length, &info, &error) != PNG_OK) { /* report error.message at error.offset */ }
/* Compare info.pixel_bytes and info.work_bytes with this tool's budget, then take both from its arena. */
if (png_decode(file, length, pixels, info.pixel_bytes, work, info.work_bytes, nullptr, &error) != PNG_OK) { /* same */ }
```

`png_probe` inflates nothing. It refuses a file whose IDAT could not expand to the claimed image, using deflate's limit of
1032 to 1, so a small file cannot make a caller reserve gigabytes. It still allows a dense file of up to 32768 by 32768, which
is 4 GiB of pixels, so the caller's own budget has the last word.
