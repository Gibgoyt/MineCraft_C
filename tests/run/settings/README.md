# tests/run/settings

**Purpose:** ctest `run_settings`. Tests `run/src/settings/` with `persist`, `run/src/ui/screen_settings/` and `run/src/game/view/`, and proves in the built program that render distance is live and kept. `session/` is the same on the real threads.
**Tier:** test.
**Owns:** the test arena, and a directory of its own under `/tmp` for each test that wants one, removed at its end. The program's blocks on the card are well under 4 GiB.
**Lifetime:** One run.
**Threads:** main; real workers, and the built program.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/display/mode/fixtures/`, `compile/src/png` to read what the program writes, `run/src/settings/`, `run/src/ui/`, `run/src/game/`, the built program, and a card.

Compute only. No real display, console or input device is opened: the flips are on the fake kernel, and every run of the program is `--headless` or refused for its flags, with stdin from `/dev/null`.

What it holds to:

- **Kept:** the defaults, the file for them byte for byte, 20000 random sets of values written and read back, a file in any order with comments, every kind of wrong line and each setting past both ends, another version setting nothing, a save over what was there and one that cannot be done, all through `settings_load` and `settings_save` too.
- **The screen,** on input frames made here, clicking where the layout puts things, by numbers worked out by hand: each slider through every value by the wheel and to each end by a drag, VSync and all sixteen effects each flipping its one bit, Done, Back and Esc, the labels for what takes a new world or a new start, no heap in 200 frames, and every size.
- **By hand:** what a config comes to, field of view, the mouse with what is past a whole count kept, a small picture brought up, and how many workers run.
- **Flips,** on a fake kernel watched at the ioctl: with and without a wait, and a driver that says no.
- **The built program,** the gate: `fixtures/live.script` opens a world, then at Esc and Settings takes render distance from 8 to 2, back to 8, and to 4, with screenshots throughout. One world is opened in all that. Its log says what took, and how many columns came. The pictures of each differ by far more than noise, and 8 comes back. Then `fixtures/again.script` in a new program reads the file and opens that world at 4, the same picture as before it ended, with nothing changing on the way. And a new program started at 2 is the same picture as 2 set live: so what a change does in a running world is what it would have done before opening it.
- **Past full detail in the built program,** on request, and under ctest this one proves nothing and says so: `fixtures/far.script` is the same with 14 and 12, and a new program at 12. Settling a far field costs about 5 s of a worker for each coarse column at `-O0`, hundreds of them, so ctest's run under its 300 s stops at telling it is planned and building, and this is for a release build: `SETTINGS_FAR=1 build/release/bin/tests/run_settings build/release/bin/minecraft_c`, from the repository's root. Under ctest it passes by doing nothing.
- **Flags past a range:** `--render-distance 1 --full-detail 99` with `--menu` come to what a slider can show.
- `fixtures/title.script` does it from the title screen, with effects, into a new file, and with a directory where the file should go: it says so once, and leaves on the second Done.

If `SETTINGS_PNG_DIR` names a directory, the program's pictures stay there to be looked at.
