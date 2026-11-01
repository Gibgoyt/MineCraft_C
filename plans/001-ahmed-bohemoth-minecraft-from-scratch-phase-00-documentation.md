# 001-ahmed-bohemoth-minecraft-from-scratch — phase 0: Documentation

**Status:** in progress   (updated 2026-11-01)

## Goal

The numbered plan files, `plans/REPO_STRUCTURE.md`, `plans/CODING_STANDARD.md` and `research/003-speed-research.md` all exist, and no game code has been written.

**Wave:** W0.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-stage-1-setup-plan-files.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-stage-2-repo-structure.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-stage-3-coding-standard.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-stage-4-speed-research.md`

## Todo

- [x] Stage 00.1: Setup plan files
- [x] Stage 00.2: Repo structure document
- [x] Stage 00.3: Coding standard document
- [x] Stage 00.4: Speed research write-up
- [ ] Phase gate passed

## Verification

**Gate:** Ahmed reads the docs and says go. Nothing below this phase starts without it.

    cmp ~/.claude/plans/pasted-content-id-a5ab-ahmed-admin-mine-serialized-fiddle.md plans/001-ahmed-bohemoth-minecraft-from-scratch.md
    ls plans/ | grep -c '^001-ahmed-bohemoth-'
    git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'    # must print nothing

## Commits

Appended as they land:

- `7e615ac` Document the full plan, repository structure, coding standard and speed research for the from-scratch game.
## Issues found

None.
