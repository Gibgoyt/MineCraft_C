# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7: World data

**Status:** not started   (updated 2026-11-01)

## Goal

Blocks are defined in readable YAML, sections and columns store them compactly, and the world map streams by distance within the `z = -250 ... +500` range.

**Wave:** W5.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-1-block-tables.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-2-section-column.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-3-map-edit.md`

## Todo

- [ ] Stage 07.1: Block tables
- [ ] Stage 07.2: Sections and columns
- [ ] Stage 07.3: World map and edits
- [ ] Phase gate passed

## Verification

**Gate:** Random-edit tests vs a brute-force array; `z=-251` and `z=501` rejected; memory per section measured.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(compile_game_tables|run_world)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
