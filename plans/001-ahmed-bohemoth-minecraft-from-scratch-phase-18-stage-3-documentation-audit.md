# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 3: Documentation audit

**Status:** not started   (updated 2026-11-01)

## Goal

Every README is accurate, REPO_STRUCTURE matches the tree, all todos are truthful, and licences are complete.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- All `README.md` files
- `plans/REPO_STRUCTURE.md`
- `textures/ATTRIBUTION.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Compare every `README.md` against its code
- [ ] Compare `plans/REPO_STRUCTURE.md` against the tree
- [ ] Check every phase and stage todo against what actually landed
- [ ] Check licences and attribution for all imported assets

## Verification

    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    find run compile cuda asm -type f -name 'index.*' | wc -l
    git status --short

## Commits

Appended as they land:

## Issues found

None.
