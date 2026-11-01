# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 1: Font and widgets

**Status:** not started   (updated 2026-11-01)

## Goal

Bitmap font, widgets and NASM-blended composition exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/ui/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/ui/font/`
- `run/{src,include}/ui/widget/`
- `tests/run/ui/font/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `ui` Owner tier: `run/src/ui/index.c` with paired `ui_init` and `ui_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement the bitmap font in `run/src/ui/font/index.c`
- [ ] Implement widgets in `run/src/ui/widget/index.c`
- [ ] Compose with the NASM blend kernel
- [ ] Register test `run_ui_font`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_font$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
