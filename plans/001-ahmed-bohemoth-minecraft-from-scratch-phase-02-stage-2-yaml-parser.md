# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 2: YAML parser

**Status:** not started   (updated 2026-11-01)

## Goal

Our own YAML subset parser exists, with line-numbered errors. Proof: parser tests and bad-input tests pass.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/yaml/`
- `tests/compile/yaml/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement the parser in `compile/src/yaml/index.c` and `compile/include/yaml/index.h`, covering the subset `config/machine.yaml` uses, including inline maps
- [ ] Report errors with line numbers
- [ ] Add tests in `tests/compile/yaml/index.c`, registered as ctest `compile_yaml`, with valid and bad inputs
- [ ] Add `compile/src/yaml/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_yaml$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
