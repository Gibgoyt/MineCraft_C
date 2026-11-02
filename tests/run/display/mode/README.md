# tests/run/display/mode

**Purpose:** ctest `run_display_mode`. Tests all of Stage 05.1 without a card: `run/src/display/`, `device/`, `mode/` and `flip/`.
Mode picking on fixture lists; the refusals; enumeration, buffers, flips, events and the restore order against a fake kernel
behind `display_ops_t`; the whole `--test-pattern` loop, including the ways it can fail; the real `display_ops_t` on pipes and a
temporary file; and the built program refusing.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and the child processes it waits for.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes the built `minecraft_c`.
**Depends on:** `tests/harness/`, `run/src/display/`. `fixtures/fake_card.h` is the fake kernel, kept apart to hold `index.c`
under the standard's 1500-line mark. It is under `fixtures/`, so ctest does not register it.

**It never opens a card or a console, and cannot.** The fake kernel answers everything up to the last two sections. The real ops
are only given pipes, a temporary file and `/dev/null`. The real program only ever gets `/dev/null` for stdin (the harness does
that), which `display_device_check_console` refuses before it would open anything, and a `--card` path that does not exist in case
that ever stops being true.

What this cannot show is what `nvidia-drm` does with these calls. The fake's answers come from reading the kernel's DRM UAPI, not
from watching a driver. That is what Ahmed's run on a text console is for.
