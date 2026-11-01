# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2: Build skeleton

**Status:** not started   (updated 2026-11-01)

## Goal

CMake builds all three languages on every preset, `config/machine.yaml` drives compile-time defines through our own YAML parser, and the standards lint fails the build on any breach.

**Wave:** W2.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-1-cmake-mutex-hello.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-2-yaml-parser.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-3-machine-config.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-4-standards-lint-harness.md`

## Todo

- [ ] Stage 02.1: CMake, mutex and hello binary
- [ ] Stage 02.2: YAML parser
- [ ] Stage 02.3: Machine config generator
- [ ] Stage 02.4: Standards lint and test harness
- [ ] Phase gate passed

## Verification

**Gate:** `ctest` is green; lint rejects a 2001-line file, `int`, padding, and a missing README.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug --output-on-failure

## Commits

Appended as they land:

## Issues found

None.
