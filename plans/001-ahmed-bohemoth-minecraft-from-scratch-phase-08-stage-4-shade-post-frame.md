# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 4: Shading, post and frame

**Status:** not started   (updated 2026-11-01)

## Goal

Textures, face shade, tone map, readback, present and a free-fly camera exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/game/index.*` and `README.md` (the Owner tier)
- `cuda/{src,include}/shade/`
- `cuda/{src,include}/post/`
- `cuda/{src,include}/frame/`
- `run/{src,include}/render/frame/`
- `tests/run/render/frame/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `game` Owner tier: `run/src/game/index.c` owns the World, Tick and Frame arenas and runs the loop connecting `sim`, `gpu` and `main`; later stages add their tick and frame calls
- [ ] Implement texture lookup and face shade in `cuda/src/shade/index.cu`
- [ ] Implement the tone map to XRGB8888 in `cuda/src/post/index.cu`
- [ ] Implement frame kernels and readback in `cuda/src/frame/index.cu`
- [ ] Implement present and the free-fly camera through `run/src/render/frame/index.c`
- [ ] Add golden headless PNG tests, registered as `run_render_frame`, and inspect the images
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c --test-world

## Commits

Appended as they land:

## Issues found

None.
