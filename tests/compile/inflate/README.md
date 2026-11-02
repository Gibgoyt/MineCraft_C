# tests/compile/inflate

**Purpose:** ctest `compile_inflate`: proves `compile/src/inflate/` decodes real streams exactly, and refuses bad ones without touching memory outside its buffers.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_inflate`, on `tests/harness/`, with the fixtures directory from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/inflate/`, and the committed files in `fixtures/`. It needs no server, no display, and neither Python nor zlib.

## What it checks

- **Independent encoder.** 56 zlib and raw streams from real zlib, in `fixtures/`: stored, fixed and dynamic blocks, every zlib strategy, a small window, flushed blocks, and a match at distance 32768. Each decodes to the exact length and hash in `manifest.txt`, using the whole input. With one byte less of output, each says `INFLATE_NO_ROOM`.
- **Truncation.** Each fixture cut short at up to 2000 places and at its last byte says exactly `INFLATE_TRUNCATED`.
- **Lengths and distances.** Every length from 3 to 258, and the edges of every distance code, against an expected output built in the test. The test's codes come from the RFC's description rather than a table.
- **Hand-built bad streams** for every status: block type 3, `NLEN`, symbols 286-287 and distance 30-31, a match before the start of the output, over-subscribed and incomplete codes in all three alphabets, a missing end code, `HLIT` and `HDIST` too high, code 16 with nothing to repeat, and runs past the end of the lengths.
- **What must be accepted:** a single one-bit code, no distance codes when no match uses them, 15-bit codes through the slow path, a run of zeros across the two alphabets, and overlapping matches.
- **zlib:** each header field, each byte of the Adler-32, a short trailer, and bytes after it.
- **Hostile input, under guard pages.** The input and output end flush against a `PROT_NONE` page, so one byte read or written past either end kills the test. Every bit of each small fixture is flipped in turn, and 40000 rounds of random changes run against a small output. The second test also requires that the mutations reached the deep errors, so a mutator that stopped doing anything would fail.

`tests/compile/png/` covers what comes on top. The sanitize preset runs this same test under ASan and UBSan.

## The fixtures

`fixtures/generate.py` wrote them, and it is not part of the build. The streams are committed, so the test depends on neither zlib nor Python. Run it only to change the set, as its header says.
