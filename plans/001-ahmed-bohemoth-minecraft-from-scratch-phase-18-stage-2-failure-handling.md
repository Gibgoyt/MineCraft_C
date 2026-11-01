# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 2: Failure handling

**Status:** not started   (updated 2026-11-01)

## Goal

Failure handling exists for GPU loss, device unplug, disk full, and VT switch during load.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/gpu_bridge/*`
- `run/{src,include}/input/*`
- `run/{src,include}/save/*`
- `run/{src,include}/display/vt_guard/`
- `tests/run/failure/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Handle GPU loss
- [ ] Handle device unplug
- [ ] Handle disk full
- [ ] Handle VT switch during load
- [ ] Add tests where the failure can be simulated, registered as `run_failure`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_failure$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c
    # Unplug a device, and switch VT during load.

## Commits

Appended as they land:

## Issues found

None.
