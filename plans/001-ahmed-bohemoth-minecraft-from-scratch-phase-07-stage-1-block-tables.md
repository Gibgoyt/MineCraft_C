# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7, stage 1: Block tables

**Status:** not started   (updated 2026-11-01)

## Goal

YAML compiles into block tables with hardness, tool, drops, shape, light and texture, for about 120 Overworld blocks.

**Wave:** W5. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-world-data.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/game_tables/`
- `config/blocks/*`
- `tests/compile/game_tables/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement `compile/src/game_tables/index.c`, generating C tables in `build/generated/`
- [ ] Write `config/blocks/<block>/index.yaml` for about 120 Overworld blocks; `config/blocks/dirt/index.yaml` must plainly show dirt drops dirt
- [ ] Reject unknown fields and missing references with line-numbered errors
- [ ] Register test `compile_game_tables`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_game_tables$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
