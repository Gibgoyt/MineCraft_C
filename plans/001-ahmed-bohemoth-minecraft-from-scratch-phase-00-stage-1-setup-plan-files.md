# 001-ahmed-bohemoth-minecraft-from-scratch — phase 0, stage 1: Setup plan files

**Status:** done   (updated 2026-11-01)

## Goal

The approved plan is copied byte for byte into `plans/`, and every phase and stage file exists, created from the `plans/README.md` template. A dedicated agent does this while the main thread waits.

**Wave:** W0. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-documentation.md`.

**Owned paths** (edit only these, plus this file):

- `plans/001-ahmed-bohemoth-minecraft-from-scratch*.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Confirm `plans/` held no NNN-numbered files, so NNN is `001`
- [x] Copy `~/.claude/plans/pasted-content-id-a5ab-ahmed-admin-mine-serialized-fiddle.md` to `plans/001-ahmed-bohemoth-minecraft-from-scratch.md`, byte for byte
- [x] Create 19 phase files and 78 stage files from plan section 8
- [x] Log the inherited-conventions issue in this file's Issues found
- [x] Run the Verification commands below and confirm their results
- [x] Main thread commits with `git commit -F -`, checks the message has no AI trailer, and sets this file's status to done

## Verification

    cmp ~/.claude/plans/pasted-content-id-a5ab-ahmed-admin-mine-serialized-fiddle.md plans/001-ahmed-bohemoth-minecraft-from-scratch.md
    ls plans/ | grep -c '^001-ahmed-bohemoth-'    # must print 98
    git status --short    # only additions under plans/

## Commits

Appended as they land:

## Issues found

### `plans/README.md` carries conventions from another project

- **Expected:** every convention in `plans/README.md` applies to this repository.
- **What actually happened:** the README refers to things that do not exist here: `npm` scripts, migrations, `data/fastlane-db/README.md`, `.claude/rules/test-or-build-mutex.md` and `tools/test-or-build.sh`.
- **Evidence:** `plans/README.md` lines 129-133 (template Verification section) and lines 152-161 (inherited conventions). A `find` of the repo during planning listed only `plans/README.md`, `plans/PROMPT.md`, `research/001.md` and `research/002.md`.
- **What was done:** the README was left unedited, per plan section 0 point 5. The mutex arrives in Stage 02.1, implemented for CMake, because parallel waves need it. The `npm` and migration lines do not apply.
