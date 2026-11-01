# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 3: Mining and placing

**Status:** not started   (updated 2026-11-01)

## Goal

Timed mining by hardness and tool, drops spawned from block tables, and block placing exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/interaction/`
- `tests/run/player/interaction/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement timed mining from table hardness and tool in `run/src/player/interaction/index.c`
- [ ] Spawn drops from block tables; dirt must drop dirt
- [ ] Implement block placing
- [ ] Test drop tables for every block, registered as `run_player_interaction`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_player_interaction$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
