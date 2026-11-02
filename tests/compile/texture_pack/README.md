# tests/compile/texture_pack

**Purpose:** ctest `compile_texture_pack`: proves `compile/src/texture_pack/` imports exactly what it should, refuses every bad pack without writing anything, and that the committed `textures/` is whole with no pack.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_texture_pack`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/texture_pack/` (run as a process, and `mctex` linked directly), and `mkdir`, `rm`, `find` and `diff` from the system. It needs no server, no display, no network, and not the real pack.

## What it checks

It writes a small pack of its own at run time, under `build/<preset>/test_work/texture_pack/`, with a stored-deflate PNG writer in the test. Each case gets a fresh pack.

- **Exact output.** Pixels are compared byte for byte, including an `over` composite worked out by hand in the test and frames in order, along with the contents of `index.yaml`, `README.md` and `ATTRIBUTION.md`.
- **Repeatable.** A second import changes nothing and keeps modification times, and an import to another path gives identical bytes.
- **All or nothing.** Each refusal below checks the exit status and message, and that the output directory was never created. The bad texture in many cases is the last in the manifest.
- **The manifest:** a short commit, NC and ND licences, `..` and absolute paths, bad names, duplicates, unknown keys, wrong frames.
- **The licence:** a changed or missing statement, and an unreviewed licence file anywhere under any spelling.
- **The pin:** another commit, loose and packed refs and a detached `HEAD`, a hostile ref name, a damaged ref, no `.git`, and `.git` as a link.
- **Hostile files:** flipped, cut short, not a PNG, empty, a tiny file claiming 32768 by 32768, too wide for `.mctex`, and over the file limit. A symbolic link to a file outside the pack, a link for a whole directory, a FIFO, a directory in place of a file, and an escape sequence in a file name, which must not reach the output.
- **`mctex`:** FNV-1a's published test vectors, every truncation, every field, and every bit of the pixels and hash.
- **`verify`, with the pack deleted:** every kind of damage it claims to catch, and that it accepts what Stage 06.3 will add.
- **The committed tree:** `verify` passes, a direct walk of the directories (not through the tool) finds both files in each, and a table of textures the renderer will want have the expected shape, so a wrong line in the manifest cannot pass quietly.

The real pack's pixels were compared with ImageMagick once, by hand, when it was imported: see the Stage 06.2 file. That needs the pack, so it is not part of this test.
