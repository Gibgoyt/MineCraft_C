# tests/run/display/headless

**Purpose:** ctest `run_display_headless`. Tests `run/src/display/headless/` and its parts with no display: the PNG encoder, the script language, the tick loop, the demo scene, and the built program from script to file.
**Tier:** test.
**Owns:** nothing outside the test arena, and a temporary directory under `/tmp` per test that writes files, removed at the end of that test.
**Lifetime:** One run.
**Threads:** main, and the child processes it waits for.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes the built `minecraft_c`, and links `compile/src/png`.
**Depends on:** `tests/harness/`, `run/src/display/`, `compile/src/png/`. `fixtures/tour.script` is the script the program tests replay.

**A PNG is checked two ways.** A strict reader written here walks the file byte by byte against the specification, with a CRC and an Adler-32 computed bit by bit and independently of the encoder. Then `compile/src/png`, written for untrusted texture packs by another stage, must decode it to the same pixels. Damaging a file must make that decoder refuse it, which shows the check can fail.

It opens no card, no console and no input device. The program always gets `/dev/null` for stdin.
