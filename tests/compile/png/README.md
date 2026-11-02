# tests/compile/png

**Purpose:** ctest `compile_png`: proves `compile/src/png/` decodes valid files to exactly the right pixels, and refuses bad ones without touching memory outside its buffers.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_png`, on `tests/harness/`, with the fixtures directory from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/png/`, `compile/src/inflate/`, and the committed files in `fixtures/`. It needs no server, no display, and neither Python nor ImageMagick.

## What it checks

- **Independent pixels.** 79 PNG files in `fixtures/`: every color type at every depth, plain and Adam7, every filter on every row, `tRNS` of each kind, Adam7 at sizes with empty passes, split and empty IDAT, ancillary chunks, a suggested palette on RGBA, and files from Pillow and ImageMagick. Each decodes to the exact hash in `manifest.txt`, in buffers of exactly the size `png_probe` reports, against guard pages. One byte less of either buffer says `PNG_NO_ROOM`.
- **Truncation.** Each small fixture cut at every byte says exactly `PNG_TRUNCATED`.
- **Exact values, worked out by hand:** scaling from each depth, including the rounding edges at 16 bits; a palette with a short `tRNS`; a color key that tells apart two samples with the same 8 bits; each filter, Paeth included.
- **Hand-built bad files for every status**, each checked for the status, for the words in its message, and for zeroed pixels: signature, CRC on each kind of chunk, framing, every illegal depth and color pair, each method byte, chunk order, unknown critical chunks, `PLTE`, `tRNS`, palette index, too little and too much data, filter type, bytes after the zlib stream, and Adler-32.
- **Bombs.** A file of under a hundred bytes claiming 32768 by 32768 is refused by `png_probe` and by `png_decode` with no buffers at all. A real zlib stream that expands far past the image's size stops at the end of the buffer.
- **Hostile input, under guard pages.** The file, the pixels and the work memory end flush against a `PROT_NONE` page, so one byte read or written past any of them kills the test. Every bit of each small fixture is flipped: as it stands, where every flip must be refused, then again with the CRCs repaired, so the flip reaches the chunk rules and the inflater. Then 30000 rounds of random changes with repaired CRCs. Those stop at the Adler-32, so 6000 more rounds put random image data in a good stream, in all 15 formats and with random palettes and keys, which is what reaches the filters and the pixel code. Each test requires that it reached the deep layers, so a mutator that stopped doing anything would fail.

`tests/compile/inflate/` covers the decompression underneath. The sanitize preset runs this same test under ASan and UBSan.

## The fixtures

`fixtures/generate.py` wrote them, and it is not part of the build. It holds its own small encoder and works out the expected pixels from the samples it wrote, and it stops unless ImageMagick decodes every one of those files to the same pixels. The files from Pillow and ImageMagick use the expected pixels their own library gives. Run it only to change the set, as its header says.
