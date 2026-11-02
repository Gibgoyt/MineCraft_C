# tests/run/display/vt_guard

**Purpose:** ctest `run_display_vt_guard`. Tests all of Stage 05.2 with no console and no keyboard: taking and giving back, VT switching, crash signals, `SIGKILL`, the watchdog, the emergency keys, and `--crash-test`.
**Tier:** test.
**Owns:** nothing outside the test arena, and the children it forks, all of which it waits for.
**Lifetime:** One run.
**Threads:** main, and the processes it forks.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes the built `minecraft_c`.
**Depends on:** `tests/harness/`, `run/src/display/`, and the fake kernel in `../mode/fixtures/fake_card.h` for the whole test pattern.

**It never calls a KD or VT ioctl on a real tty and never opens `/dev/input`.** Each test forks a child, which arms the real guard over a table of ops whose `ioctl_call` records to a pipe and answers from the test's script, and whose `open_keyboards` returns a pipe the test writes `input_event` structs into. The signals, the fork of the guardian, its pipe and its polling are all real, so a child that faults really dies of it and the guardian really outlives it. The descriptors 5 and 7 given to the guard are never used for anything but the fake's bookkeeping.

The real program only ever gets `/dev/null` for stdin (the harness does that), which `display_device_check_console` refuses before anything else.

What this cannot show is what the kernel does with these calls, and the fake's `VT_RELDISP` is modelled on memory of `vt_reldisp` in `drivers/tty/vt/vt_ioctl.c`, not on the source. That is what Ahmed's run on a text console is for.
