# compile/src/inflate

**Purpose:** our own DEFLATE (RFC 1951) and zlib (RFC 1950) decoder for the build-time tools, safe on untrusted input.
**Tier:** Owner (depth 1 of `compile/src`), though it allocates nothing.
**Owns:** nothing. The caller gives it the input and output buffers, and its tables are stack locals of about 5 KiB.
**Lifetime:** nothing outlives a call.
**Threads:** any thread. There is no shared state.
**Public functions** (`compile/include/inflate/index.h`):

- `inflate_raw`: decodes a raw DEFLATE stream into a buffer, leaving any bytes after it alone.
- `inflate_zlib`: the same for a zlib stream, checking the header and the Adler-32 and refusing trailing bytes.
- `inflate_status_text`: one sentence about a status, for messages.

**May be called by:** other `compile/` tools. Nothing in `run/`.
**Depends on:** libc's `memcpy` and `memset`.

## What it accepts and refuses

Every read is checked against the input length and every write against the capacity, so any file gets a status and no
crash. It refuses what zlib refuses by default:

| Refused | Status |
|---|---|
| Input ending inside the stream | `INFLATE_TRUNCATED` |
| Output past the capacity, which is the limit on a decompression bomb | `INFLATE_NO_ROOM` |
| Block type 3 | `INFLATE_BAD_BLOCK_TYPE` |
| A stored block whose `LEN` is not the complement of `NLEN` | `INFLATE_BAD_STORED_LENGTH` |
| `HLIT` over 286 or `HDIST` over 30, repeat code 16 with nothing to repeat, a repeat past the end | `INFLATE_BAD_CODE_LENGTHS` |
| An over-subscribed or incomplete Huffman set | `INFLATE_BAD_HUFFMAN_SET` |
| A dynamic block with no code for symbol 256 | `INFLATE_MISSING_END_CODE` |
| A bit pattern with no symbol, length symbols 286 and 287, distance symbols 30 and 31 | `INFLATE_BAD_SYMBOL` |
| A match reaching before the start of the output | `INFLATE_BAD_DISTANCE` |
| A zlib header that is not method 8, has a window over 32 KiB, fails its check bits, or sets `FDICT` | `INFLATE_BAD_ZLIB_HEADER` |
| An Adler-32 that does not match | `INFLATE_BAD_CHECKSUM` |
| Bytes after a zlib stream | `INFLATE_TRAILING_DATA` |

An incomplete set is accepted in one case, the same one zlib accepts: a literal/length or distance code with a single
code of one bit. A distance code with no codes at all is accepted too, and fails only if a match uses it.

A match is checked against the output written so far, not against the window the zlib header declares. zlib does the same
unless built with `INFLATE_STRICT`, and the memory safety comes from the first check either way.

## How it works

A 64-bit bit buffer, refilled one byte at a time with the bounds check in one place. Huffman codes use a 9-bit lookup
table, with a canonical walk for longer codes. No recursion, and no state outside the call.
