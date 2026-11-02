# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 3: Evdev input

**Status:** in progress: written and passing on replay data, a fake kernel and real inotify on FIFOs; never run on a real device; awaits Ahmed's run below   (updated 2026-11-02)

## Goal

evdev discovery by capability, raw mouse and keyboard, and hot-plug exist. Proof: replay tests, plus Ahmed live.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/input/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/input/*`
- `tests/run/input/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `input` Owner tier: `run/src/input/index.c` with paired `input_init` and `input_deinit`, owning the regions and arenas its parts borrow
- [ ] Discover devices under `/dev/input/` by capability in `run/src/input/discovery/index.c` (written; passes on the fake kernel and on real calls in a temporary directory; what real devices answer is Ahmed's run)
- [ ] Read raw keyboard and mouse events in `run/src/input/events/index.c` (written; passes on replay data, through the fake and through real FIFOs; what real devices send is Ahmed's run)
- [ ] Handle hot-plug (written, in `run/src/input/discovery/index.c`; passes on the fake and on real inotify with FIFOs; a real plug and pull is Ahmed's run)
- [x] Add event-replay tests in `tests/run/input/`, registered as `run_input`
- [ ] Add the cursor square to `--test-pattern` (written; drawing and the loop pass on the fake card; seeing it move is Ahmed's run)
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_input$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 60 build/debug/bin/minecraft_c --test-pattern

## Results

Agent side, 2026-11-02, on the merged integration branch:

| Check | Result |
|---|---|
| `cmake --build --preset debug`, `release`, `sanitize` | All build |
| ctest `run_input`, debug | Passes: 48 tests, 2054 checks |
| ctest `run_input`, release and sanitize | Passes |
| ctest `run_display_mode`, debug and sanitize | Passes |
| ctest `standards_lint`, `clang_format`, debug | Pass |
| Whole debug ctest, on the integration branch as of this run | 36 of 36 pass |
| 34 hand mutations of `input/` and the cursor drawing, each built and run against `run_input`, before issues 7 and 8 | All 34 fail it. Three first failed to build, so they were rewritten until they built, and then failed the tests |
| The two mutations in issues 7 and 8 | Both fail it |
| `tests/run/input/fixtures/real_devices.h` against `/proc/bus/input/devices` | 21 rows each, 0 differences |
| `pgrep -af "$PWD/build"`, `pgrep -a minecraft_c` | Print nothing |

**Ahmed's run**, on a text console:

    tools/test-or-build.sh cmake --build --preset debug
    timeout 60 build/debug/bin/minecraft_c --test-pattern

| Look for | What it shows |
|---|---|
| `input: eventN: <name>, keyboard` and `, mouse` lines before the screen changes. With today's devices: one `, mouse` for the Razer, and `, keyboard` for the AT keyboard, the G710 twice, the Razer twice and the DELL keyboard | Discovery found your real devices, and not the power button |
| A white square with a black edge, starting mid-screen, that follows the mouse and stops at each edge | Raw mouse events |
| It turns red while any key or mouse button is held | Raw key events |
| Esc ends the run, and the console comes back | `pressed`, and the clean stop |
| Unplug and replug a USB mouse during the run, and it still moves the square | Hot-plug. The final line counts it, as `removed` or `lost` and `opened` |
| The last `input:` line has no `queue overflows` warning beside it | Nothing lost |

Keys typed during the run also reach the shell afterwards, until Stage 05.2's `K_OFF`: see issue 2.

## Commits

Appended as they land:

- `42e9ca5` Read keyboards and mice from evdev, found by capability and followed through hot-plug, and move a cursor square on the test pattern with them.
- `d3a8793` Fix the input code's sanitize build and its test's alignment, and record Stage 05.3's results.
- `b20a979` Ask the kernel for key state only after a read's events are taken, and test device classification on this machine's real capability bitmaps.

## Issues found

1. **Wiring outside the owned paths.**
   - `run/src/main/index.c`, `run/include/main/index.h`, `run/src/main/README.md`: `input_t` in `app_t` (320 bytes more, 1152 to 1472), `input_init` in `main_run_test_pattern` after `display_init` said OK, `input_deinit` in `app_deinit`, and the usage text.
   - `run/src/display/index.c`, `run/include/display/index.h`, `run/src/display/README.md`: `display_cursor_source_t` and `display_test_pattern_set_cursor`, and the loop asks the source each frame. `display_t` stays 704 bytes; its padding pays for the additions. A function pointer, so `display` does not include `input`.
   - `run/src/display/flip/index.c`, `run/include/display/flip/index.h`: the cursor's drawing, and two fields in `display_buffer_t` (48 to 56 bytes, so `display_flip_t` 224 to 240).
   - `tests/run/display/mode/index.c`: `fake_flip_session_t`'s padding, 32 to 16, to keep its 704, and one test that the loop asks for the cursor and that its source can stop it. The test sits there because it needs that file's fake card.
   - `plans/REPO_STRUCTURE.md`: the `input` row (`device keyboard mouse` became `ops discovery events`) and a row for `tests/run/input/`.
   - **`ops` is a part the stage did not name.** `discovery` and `events` both need the table of system calls, and the `display` system's way of letting a test replace all of them needs one home.

2. **Keys still reach the console during `--test-pattern`. A limit and not an oversight.**
   - **Expected:** a game reading raw keys wants them to stop going to the tty.
   - **Actual:** nothing here stops them. `EVIOCGRAB` is deliberately not used: the rule for agents forbids it, and a grab that outlived a crash would leave the machine with no keyboard. `K_OFF` is Stage 05.2's. So until that merges, keys typed in a run wait in the tty, and the shell gets them on exit, as with any program that does not read stdin. Ctrl+C still raises `SIGINT`, which restores the display.
   - **Done:** left to Stage 05.2, which also needs `input_keys_has` for its emergency combo once `K_OFF` means the tty hears nothing. Said so in `run/src/input/README.md`.

3. **Replay data is written by hand, and not captured from a device.**
   - **Expected:** a replay test replays what a device really sends.
   - **Actual:** capturing means reading a real device, which no agent may do. `tests/run/input/fixtures/replay.h` is written from the kernel's event codes as remembered, and the fake's answers are too. The evdev driver's source is not on this machine (`find /usr/lib/modules/ -maxdepth 6 -name 'evdev*'` prints nothing), though the UAPI headers are, and the code takes every code and ioctl number from them. So how a real keyboard orders `EV_MSC` around its keys, and how a real USB pull is reported, are not proven.
   - **Done:** nothing depends on that order: `EV_MSC` is ignored and each key event stands alone. Classifying is now tested on real answers: see issue 8. Ahmed's run is what covers the rest. If a device is misclassified or stays silent there, the `input:` lines say which one.

4. **Touchpads and tablets do not work.**
   - **Expected:** the stage says mouse and keyboard, by capability.
   - **Actual:** a touchpad reports absolute axes and no `REL_X`, so `input_discovery_classify` rejects it, and so does the rest of `input`. A mouse is required to move the cursor. A laptop with only a touchpad would get no cursor. This machine has one, `Synaptics TM3394-001` on `event16`, which stays rejected, and a mouse, the Razer on `event6`, whose bitmaps are classified as a mouse.
   - **Done:** left alone, as out of scope, and stated in `run/src/input/events/README.md`.

5. **Defects found by running this stage's own tests. All fixed.**
   - `replay_through_real_inotify_and_real_reads_covers_plugging_and_pulling` failed: `CHECK failed: input->discovery.watch_fd == -1: 5 != -1`, then `device_count == 1u: 0 != 1`. The test's fault, and worth knowing: it held the writing end of FIFOs it had unlinked, and an open file pins its dentry and so its parent, so the kernel sends `IN_DELETE_SELF` only once the last of those closes, not at the `rmdir`. The program holds its own devices open the same way, but closes one the moment a read or `IN_DELETE` says it went, so the record then comes. The test now closes its ends first, and says why.
   - The debug build was clean, but the sanitize build failed: `run/src/input/discovery/index.c:23:42: error: conversion to 'unsigned int' from 'int' may change the sign of the result`, because a `uint8_t` shifted is an `int` first. The debug preset did not report it, so the first commit, `42e9ca5`, does not build under sanitize. Fixed with a cast. Release and sanitize were then both built.
   - The sanitize run failed: `run/src/input/index.c:37:10: runtime error: member access within misaligned address ... for type 'struct input_t', which requires 64`. The test's fault: `test_alloc` aligns to 16 and `input_t` holds an `arena_t`. `run_display_mode` passes the same way only because of what it allocates first. The test now aligns its own.
   - The standards lint refused `input_events_stream_init` for having no `_deinit`. It starts a stream and owns nothing, so it was renamed `input_events_stream_start`.
   - `tests/run/display/mode/index.c` reached 1507 lines, over the standard's 1500-line mark, so the cursor's drawing tests moved to `tests/run/input/index.c`.

6. **One test program was run by hand without `timeout` or the lock.**
   - **Expected:** every process goes through `tools/test-or-build.sh` and `timeout`.
   - **Actual:** to read its check count, `build/debug/bin/tests/run_input <program>` was run bare once. It finished at once and `pgrep` found nothing left. It opened no device, as it never does.
   - **Done:** noted. Nothing further.

7. **A key could stay wrong after a queue overflow. Found by reading the code when this stage was picked up again. Fixed.**
   - **Expected:** after `SYN_DROPPED`, key state ends up as the kernel says.
   - **Actual:** `EVIOCGKEY` was asked in the middle of a batch, at the drop and at the report, and events already read after that point were then applied on top, though the answer was newer than them. With W held and `DROPPED, REPORT, W down, REPORT, W up, REPORT` in one read, W ended up not down, until its next press. From memory of the kernel's `evdev_handle_get_val`, not checked since the source is not here, `EVIOCGKEY` also removes queued key events, so no later event would have corrected it.
   - **Done:** `input_stream_t` gets `resync_owed` (its size stays 104), and the question is asked once the batch has been taken. `a_resync_comes_after_every_event_already_read_since_it_is_newer_than_them` covers it. With the old order put back in `events_take_syn`, that test failed, the only one of 48 to (`1 of 8 checks`). My first attempt at that check changed only the report branch and did not fail it, since the drop's owed question still came last. That is why both branches were changed.

8. **Real capability data was available all along, without opening a device.**
   - **Expected:** classifying is only proven by Ahmed's run (issue 3).
   - **Actual:** `/proc/bus/input/devices` is a text file anyone can read, and prints the capability bitmaps of every device. Reading it opens nothing under `/dev/input` and reads no keystrokes.
   - **Done:** its 21 devices are in `tests/run/input/fixtures/real_devices.h`, and `this_machines_own_devices_are_told_apart_correctly` runs them through the whole of discovery on the fake kernel. Kept: 7. They are the `AT` keyboard (`event3`), the G710's two interfaces (`event4`, `event5`), the Razer's mouse (`event6`) and its two keyboard interfaces (`event7`, `event9`), and the `DELL Technologies Keyboard` (`event10`). Rejected: 14, including the lid, power buttons, Video Bus, hotkeys and the touchpad. A one-off script compared the fixture with `/proc`: 21 and 21 rows, 0 differences. Removing the mouse's need for `REL_X` and `REL_Y` fails this test (`7 of 138 checks`). This does not tick the discovery box: the real `EVIOCGBIT` ioctl has still not been run on a real device, and event numbers change between boots, so the table is a record of one day, and tests the classifier only.
