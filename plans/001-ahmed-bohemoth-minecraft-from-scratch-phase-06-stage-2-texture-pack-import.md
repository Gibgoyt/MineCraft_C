# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 2: Texture pack import

**Status:** not started   (updated 2026-11-01)

## Goal

Import works from a pinned open pack cloned to `/tmp/mc_c_assets/`, with a licence check and `ATTRIBUTION.md`. Outputs are committed, so builds need no network.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/texture_pack/`
- `textures/**`
- `tests/compile/texture_pack/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Clone Pixel Perfection at a pinned commit into `/tmp/mc_c_assets/pixel_perfection/`, and treat it as untrusted data
- [ ] Verify the licence file and per-pack coverage; record the commit and findings here; escalate to Ahmed if the licence is unclear
- [ ] Implement `compile/src/texture_pack/index.c`, translating the pack into `textures/<GROUP>/<dir>/` for groups `blocks`, `items`, `mobs`, `ui`
- [ ] Give each texture directory an `index.yaml` and `README.md`
- [ ] Write `textures/ATTRIBUTION.md`
- [ ] Register test `compile_texture_pack`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_texture_pack$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    grep -n 'CC-BY-SA-4.0' textures/ATTRIBUTION.md

## Commits

Appended as they land:

## Issues found

None.
