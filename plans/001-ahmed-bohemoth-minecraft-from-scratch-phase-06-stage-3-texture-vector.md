# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 3: Texture vector data and recipes

**Status:** not started   (updated 2026-11-01)

## Goal

Each texture has vector data (palette, mean color for LOD, derived height and normal, emissive and alpha class). Textures with no pack source have procedural recipes in `index.yaml`.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/texture_vector/`
- `textures/**/index.yaml` recipe fields
- `tests/compile/texture_vector/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement `compile/src/texture_vector/index.c`, computing palette, mean color, height, normal, emissive and alpha class
- [ ] Implement procedural recipes read from `index.yaml`
- [ ] Add recipes for textures the pack lacks, including any missing mob textures
- [ ] Register test `compile_texture_vector`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_texture_vector$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
