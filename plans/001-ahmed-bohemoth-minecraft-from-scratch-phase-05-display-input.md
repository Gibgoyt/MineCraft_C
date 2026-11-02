# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5: Display and input

**Status:** in progress: everything an agent can run passes; Stages 05.1, 05.2 and 05.3 and the gate await Ahmed's run below   (updated 2026-11-02)

## Goal

The game can show pixels through hand-rolled KMS on a text console, read evdev input directly, always restore the console, and run headless for agents.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-1-device-mode-flip.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-2-vt-guard.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-3-evdev-input.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-4-headless-backend.md`

## Todo

- [ ] Stage 05.1: Device, mode and flip
- [ ] Stage 05.2: VT guard
- [ ] Stage 05.3: Evdev input
- [x] Stage 05.4: Headless backend
- [ ] Phase gate passed

## Verification

**Gate:** Ahmed sees the pattern, moves a cursor square, switches VT and back, and the console survives a forced crash. Running from inside X fails fast with a clear message.

    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 60 build/debug/bin/minecraft_c --test-pattern
    # Headless and agent-side:
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_(display|input)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## How Ahmed runs the gate

Build, then press Ctrl+Alt+F3 and log in. Run from the repo root, one at a time. Have SSH ready from a second machine for the first run. The way out at any time is Ctrl+Alt+Backspace. Stage 05.2's file has the table of what each one should show and which exit status it should give.

    tools/test-or-build.sh cmake --build --preset debug
    timeout 60 build/debug/bin/minecraft_c --test-pattern 2>&1 | tee /tmp/mc_c_gate1.txt; echo "exit=${PIPESTATUS[0]}"
    timeout 30 build/debug/bin/minecraft_c --test-pattern --crash-test 2>&1 | tee /tmp/mc_c_gate2.txt; echo "exit=${PIPESTATUS[0]}"
    pgrep -a minecraft_c    # must print nothing

During the first, which ends by itself after 20 seconds: see the bars and the red band; move the mouse so the square follows it, across the band too, which must stay whole; hold a key to turn it red; press Ctrl+Alt+F2, press Esc and move the mouse there, press Ctrl+Alt+F3 back, and the pattern must return with the square where it was. Run it once more and press Esc, which must end it. After each, type at the prompt to confirm the console works. Then, from a terminal inside X, check the refusal:

    timeout 10 build/debug/bin/minecraft_c --test-pattern; echo "exit=$?"    # must print the refusal, with exit 6

## Agent-side results

Gate check, 2026-11-02, on this branch, from `rm -rf build`:

| Check | Result |
|---|---|
| `cmake --build`, debug, sanitize, release | All build, once the defects below were fixed. Before that, none could: debug stopped on the merge's sizes |
| `ctest` on the debug, release and sanitize presets, everything | 39 of 39 on each |
| Gate: `ctest --preset debug -R '^run_(display|input)'` | 4 of 4: `run_display_headless` 32 tests, 828203 checks; `run_display_mode` 74, 999; `run_display_vt_guard` 48, 2665; `run_input` 49, 2068 |
| `DISPLAY=:0`, stdin `/dev/null`, `--test-pattern --card /nonexistent/...` | `refused: DISPLAY is set ...`, exit 6, no open of a card or input device. The same for `WAYLAND_DISPLAY`, and for neither set |
| 14 changes to the code, tried one at a time on debug, 5 of them to the new fixes and 9 to what the stages claim | All 14 failed the tests. 3 first failed to build (an unused name), so they were rewritten until they built |
| `pgrep -a minecraft_c`, `pgrep -af "$PWD/build"` | Print nothing |

**Not run, because no agent may:** a real card, a real console, a real keyboard or a real VT switch. Everything about them here has run only on fake kernels.

## Commits

Appended as they land:

## Issues found

Found by the gate check, 2026-11-02. Each is written up in full in the stage named:

1. **The integration branch did not build at all:** `static_assert(sizeof(display_headless_t) == 288)` failed after merge `c7411db`, yet the stage files reported passing suites. Each stage's results were true of its own branch, and nobody had built the merge. Stage 05.4 issue 5. Fixed.
2. **The release preset could not build `run_display_headless`.** Stage 05.4 issue 6. Fixed.
3. **The mouse cursor erased the emergency-exit notice.** Stage 05.2 issue 9. Fixed.
4. **Esc and mouse movement made on another VT would take effect on returning.** Stage 05.2 issue 9. Fixed.
5. **`run_display_mode` used a misaligned `display_t`,** known to two stages and left. Stage 05.1 issue 9. Fixed.
6. **Stale notes** in Stages 05.1 and 05.3 and `run/src/input/README.md`, and a missing row for `tests/run/display/headless/` in `plans/REPO_STRUCTURE.md`. Fixed.
7. **Looked for and not found:** stubs or TODOs (`grep` over the phase's source and tests), public functions nothing calls, `libc_double_t` as anything but a cast, `goto` outside `_init`, allocation below its tier, and a README missing a required field. The one standard question left open is `-errno` returns: Stage 05.1 issue 9.
