# run/src/display/headless/png

**Purpose:** turns XRGB8888 pixels into a valid PNG file in memory, with stored (uncompressed) DEFLATE.
**Tier:** Borrow. Allocates nothing: the caller gives it the pixels and the output buffer. The CRC table is on the stack.
**Owns:** nothing.
**Lifetime:** One call.
**Threads:** any; it has no state.
**Public functions:**

- `display_png_bytes_needed`: the exact file size for a width and height, or 0 for a size it refuses.
- `display_png_encode`: writes the file. The same pixels always give the same bytes, so a golden image can be compared as a file.
- `display_png_crc32`, `display_png_adler32`: the two checksums, public so a test can hold them to the published check values.

**May be called by:** `display/headless`.
**Depends on:** libc's `memcpy`.

This writes only. The bounds-checked decoder for untrusted files is `compile/src/png/`, which the test uses as its second opinion.
