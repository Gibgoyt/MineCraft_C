# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8: CUDA renderer core

**Status:** not started   (updated 2026-11-01)

## Goal

The CUDA ray caster draws a textured test world and presents it, and Ahmed can fly through it. This is the first picture.

**Wave:** W6.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-1-device-memory-bridge.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-2-section-encode-upload.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-3-traverse.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-4-shade-post-frame.md`

## Todo

- [ ] Stage 08.1: Device memory and C bridge
- [ ] Stage 08.2: Section encoding and upload
- [ ] Stage 08.3: Ray traversal
- [ ] Stage 08.4: Shading, post and frame
- [ ] Phase gate passed

## Verification

**Gate:** First picture: Ahmed flies a test world on KMS at R=32; golden headless PNGs match; FPS recorded.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_gpu|run_render|cuda_)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --test-world

## Commits

Appended as they land:

## Issues found

None.
