# tests/run/input

**Purpose:** ctest `run_input`. Tests all of Stage 05.3 without a device: `run/src/input/`, `ops/`, `discovery/` and `events/`.
Classifying by capability; replaying recorded-style event streams through the whole system; hot-plug in every order that was thought
of; the real `input_ops_t` on a temporary directory; and the built program refusing before it looks at any input.
**Tier:** test.
**Owns:** nothing outside the test arena, and a temporary directory it removes.
**Lifetime:** One run.
**Threads:** main, and the child processes it waits for.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes the built `minecraft_c`.
**Depends on:** `tests/harness/`, `run/src/input/`. `fixtures/` holds the replay data and the fake kernel; it is under `fixtures/`, so ctest does not
register it.

**It never opens a real device, and cannot.** No test names `/dev/input`. The fake kernel refuses and counts any path outside its
own fake directory. The real ops are only given a `mkdtemp` directory with regular files and FIFOs in it, and for those the ioctl is
either the real one, which answers `ENOTTY` on a regular file, or a stand-in. The real program only ever gets `/dev/null` for
stdin (the harness does that), which `display_device_check_console` refuses before `input_init` would run.

What this cannot show is what real devices send and answer. The replay data is written by hand from the kernel's event-code
documentation, not captured, because capturing means reading a device. Ahmed's run on a text console is what covers that.
