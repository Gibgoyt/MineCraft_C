# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18: Release hardening

**Status:** not started   (updated 2026-11-01)

## Goal

The game survives sanitizers, fuzzing, soak and device failures, the docs match reality, and Ahmed has signed off.

**Wave:** W10.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-stage-1-sanitize-fuzz-soak.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-stage-2-failure-handling.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-stage-3-documentation-audit.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-stage-4-portability-seams.md`

## Todo

- [ ] Stage 18.1: Sanitizers, fuzz and soak
- [ ] Stage 18.2: Failure handling
- [ ] Stage 18.3: Documentation audit
- [ ] Stage 18.4: Portability seams
- [ ] Phase gate passed

## Verification

**Gate:** Ahmed plays a full session and signs off.

    cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/release/bin/minecraft_c

## Commits

Appended as they land:

## Issues found

None.
