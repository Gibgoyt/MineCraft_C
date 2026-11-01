# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 2: VT guard

**Status:** not started   (updated 2026-11-01)

## Goal

KD_GRAPHICS, K_OFF and VT_PROCESS switching work, and the console is always restored on exit, crash signals and watchdog, with an emergency key combo. Proof: a deliberate crash returns to a working console.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/display/vt_guard/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement KD_GRAPHICS, K_OFF and VT_PROCESS switching in `run/src/display/vt_guard/index.c`
- [ ] Restore the console on normal exit, crash signals and watchdog expiry
- [ ] Add the emergency key combo
- [ ] Add a `--crash-test` mode that deliberately crashes
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 30 build/debug/bin/minecraft_c --test-pattern --crash-test
    # Confirm the console works afterwards, then repeat with a VT switch and back.

## Commits

Appended as they land:

## Issues found

None.
