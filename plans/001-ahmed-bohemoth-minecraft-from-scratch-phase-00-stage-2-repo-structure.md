# 001-ahmed-bohemoth-minecraft-from-scratch — phase 0, stage 2: Repo structure document

**Status:** done   (updated 2026-11-01)

## Goal

`plans/REPO_STRUCTURE.md` documents every directory, tier, ownership rule and dependency rule, and explains how to add a module.

**Wave:** W0. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-documentation.md`.

**Owned paths** (edit only these, plus this file):

- `plans/REPO_STRUCTURE.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Write `plans/REPO_STRUCTURE.md` covering every path in plan section 5
- [x] Document the `index.{c,h}`, `index.{cu,hu}`, `index.{asm,inc}` and `README.md` rule for each directory
- [x] Document the Root, Owner, Arena and Borrow tiers from plan section 4, and how path depth shows them
- [x] Document the C to CUDA boundary at `run/include/gpu_bridge/*/index.h`
- [x] Add a how-to-add-a-module section

## Verification

    grep -E 'config/machine.yaml|compile/|run/|cuda/|asm/|textures/|tests/|bench/|cmake/|tools/test-or-build.sh' plans/REPO_STRUCTURE.md
    # Manually confirm every plan section 5 path appears.

## Commits

Appended as they land:

## Issues found

### Generated stage files left the Owner tier and the game loop without an owner

- **Expected:** every directory in the plan has a stage that owns it, and the memory law's depth 1 Owner tier exists for each system.
- **What actually happened:** stages owned only depth 2 parts such as `run/src/world/section/`, so no stage created `run/src/world/index.c`, the only place allowed to reserve that system's memory. No stage owned `run/src/game/` at all, though plan section 5 lists it.
- **Evidence:** the Owned paths lists of all stage files, extracted during this stage; `grep -l 'run/src/game\|run/{src,include}/game' plans/001-*-phase-*` printed 0 files.
- **What was done:** fixed in 17 stage files, adding the Owner-tier path and a todo to the first stage of each system. `game` goes to Stage 08.4, where the first frame loop is needed. The verbatim plan is unchanged.

### Stage files name test flags and tests that the plan does not define

- **Expected:** stage files use only names from the plan.
- **What actually happened:** the setup agent derived `--test-pattern`, `--crash-test`, `--test-world`, some sub-directory names, `ctest` names and `bench_*` names, so its files would be specific enough to execute.
- **Evidence:** `grep -lE -- '--test-pattern|--crash-test|--test-world' plans/001-*-phase-*` matches six phase and stage files.
- **What was done:** left in place as implementation detail, and the stage that builds each one may rename it, updating the files that mention it. `plans/REPO_STRUCTURE.md` notes that part names may be refined.
