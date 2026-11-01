# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 4: Standards lint and test harness

**Status:** not started   (updated 2026-11-01)

## Goal

The lint and the test harness exist. Proof: each banned construct has a failing fixture.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/standards_lint/`
- `tests/harness/`
- `tests/compile/standards_lint/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Check tiers by path depth, include direction from `plans/REPO_STRUCTURE.md` section 11, the `borrowed_` static allowlist, and `.hu`-only headers under `cuda/`
- [ ] Detect recursion from GCC `-fcallgraph-info` output and set `-Wframe-larger-than=`, per `plans/CODING_STANDARD.md` section 10
- [ ] Write the minimal test harness in `tests/harness/`
- [ ] Implement `compile/src/standards_lint/index.c`, checking the banned list in `plans/CODING_STANDARD.md`, the 2000-line limit, `index.*` naming, and README presence
- [ ] Run the lint before compile, so a breach fails the build
- [ ] Add one failing fixture per banned construct under `tests/compile/standards_lint/`, including a 2001-line file, `int`, and a missing README
- [ ] Add a padding fixture proving `-Werror=padded` rejects it
- [ ] Register ctest `standards_lint` for the repo and `standards_lint_fixtures` for the fixtures
- [ ] Add `compile/src/standards_lint/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint_fixtures$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
