# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7, stage 2: Sections and columns

**Status:** not started   (updated 2026-11-01)

## Goal

Palette storage, the uniform-section shortcut, coordinate helpers and range clamps exist.

**Wave:** W5. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-world-data.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/world/section/`
- `run/{src,include}/world/column/`
- `tests/run/world/section/`
- `tests/run/world/column/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `world` Owner tier: `run/src/world/index.c` with paired `world_init` and `world_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement `uint16_t` block state with palette and `uint8_t` sky/block light in `run/src/world/section/index.c`, with no array for uniform sections
- [ ] Implement 16x16 columns with 48 section slots covering `z` in `[-256, 512)` in `run/src/world/column/index.c`
- [ ] Clamp gameplay to `z = -250 ... +500`, rejecting `-251` and `501`
- [ ] Test against a brute-force array, registered as `run_world_section` and `run_world_column`
- [ ] Measure and record memory per section
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_section$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_column$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
