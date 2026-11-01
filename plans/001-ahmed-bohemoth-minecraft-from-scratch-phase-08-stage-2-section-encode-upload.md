# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 2: Section encoding and upload

**Status:** not started   (updated 2026-11-01)

## Goal

Worker section encoding, a budgeted upload queue and a slab pool exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/render/index.*` and `README.md` (the Owner tier)
- `cuda/{src,include}/world_buffers/`
- `run/{src,include}/render/encode/`
- `run/{src,include}/render/upload/`
- `tests/run/render/encode/`
- `tests/run/render/upload/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `render` Owner tier: `run/src/render/index.c` with paired `render_init` and `render_deinit`, owning the regions and arenas its parts borrow
- [ ] Encode sections into the three-level `uint64_t` bitmask tree on workers, in `run/src/render/encode/index.c`
- [ ] Implement the budgeted upload queue in `run/src/render/upload/index.c`
- [ ] Implement the slab pool and toroidal column table with per-column max height in `cuda/src/world_buffers/index.cu`
- [ ] Register tests `run_render_encode` and `run_render_upload`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_encode$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_upload$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
