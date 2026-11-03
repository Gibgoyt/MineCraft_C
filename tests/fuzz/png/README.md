# tests/fuzz/png

**Purpose:** ctest `fuzz_png` and the program `fuzz_png`: fuzzes `compile/src/png/` and `compile/src/inflate/`, the only parts of the build that read files from outside.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** see `tests/fuzz/support/`.
**Public functions:** none; a program.
**May be called by:** ctest, and a person: see `tests/fuzz/README.md`.
**Depends on:** `tests/fuzz/support/`, `compile/src/png/`, `compile/src/inflate/`. Seeds are `tests/compile/png/fixtures/`, `tests/compile/inflate/fixtures/` and images made here.

## What an input is

The first byte says: a PNG file, with its chunks' CRCs put right 15 times in 16; the fields of a header, a palette, transparency and raw rows, made here into a file with stored deflate blocks, so that mutation reaches the filters, Adam7, depths and palettes that a change to a compressed stream never would; a raw DEFLATE stream; or a zlib stream.

## What it holds them to

Every buffer is exactly what `png_probe` said, with poison round it.

- A refused probe has zeroed `info`, and a status that says so.
- An accepted one says what can be: dimensions in range, sizes that add up. Past 16 MiB of pixels is a caller's limit, as the header says, so it stops there.
- A refused decode leaves every pixel zero. An accepted one agrees with the probe, and a second decode gives the same pixels from buffers full of something else, so nothing depends on memory it did not write.
- A pixel buffer or a work buffer one byte short gets `PNG_NO_ROOM`.
- A stream says it used no more than it had. One that decodes to N bytes decodes the same in exactly N, and gets `INFLATE_NO_ROOM` in N - 1.
