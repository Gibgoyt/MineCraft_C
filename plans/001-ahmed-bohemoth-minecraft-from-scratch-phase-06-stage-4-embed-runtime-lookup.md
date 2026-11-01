# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 4: Embedding and runtime lookup

**Status:** not started   (updated 2026-11-01)

## Goal

Mip chains exist, the blob is embedded through `incbin`, and runtime lookup works.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/texture/index.*` and `README.md` (the Owner tier)
- `asm/{src,include}/embed_assets/`
- `run/{src,include}/texture/*`
- `tests/run/texture/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `texture` Owner tier: `run/src/texture/index.c` with paired `texture_init` and `texture_deinit`, owning the regions and arenas its parts borrow
- [ ] Generate mip chains and the texture blob in `build/generated/` with `add_custom_command`
- [ ] Embed the blob with `incbin` in `asm/src/embed_assets/index.asm`
- [ ] Implement runtime lookup under `run/src/texture/`
- [ ] Register test `run_texture`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_texture$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
