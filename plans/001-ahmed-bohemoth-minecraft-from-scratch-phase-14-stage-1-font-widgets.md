# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 1: Font and widgets

**Status:** done   (updated 2026-11-03)

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

- [x] Create the `ui` Owner tier: `run/src/ui/index.c` with paired `ui_init` and `ui_deinit`, owning the regions and arenas its parts borrow
- [x] Implement the bitmap font in `run/src/ui/font/index.c`
- [x] Implement widgets in `run/src/ui/widget/index.c`
- [x] Compose with the NASM blend kernel
- [x] Register test `run_ui_font`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_font$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

1. **A part the stage did not list.** Expected: `font` and `widget` only. Both have to draw, and an Owner's header includes its parts, so neither could take canvas and blit code from the Owner without a circle, and text cannot sit under widgets if widgets hold it. Done: added `run/{src,include}/ui/compose/`, outside the stage's owned paths, as the home of the todo "Compose with the NASM blend kernel". `plans/REPO_STRUCTURE.md` lists it, and its README allows its siblings to include it.

2. **Wiring outside the owned paths.** `run/include/main/index.h` and `run/src/main/index.c` hold `ui_t ui` in `app_t` (12288 to 12864 bytes) and call `ui_init` after `render_init`, and `ui_deinit` before `render_deinit`. `run/src/main/README.md` lists the field.

3. **`run_types` pins the program's region count.** Expected to pass; instead, with `ui_t` in `app_t`: `tests/run/types/index.c:914: result.output does not contain "heap at exit: 11 reserved, 11 released, 0 bytes live"`, and the same at line 922 for `heap: leak at exit: 11 regions`. A `ui_t` is one more region, correctly. Done: both numbers are now 12, in that file, also outside the owned paths.

4. **`textures/ui/slot` is one flat translucent colour to its edge.** Expected: slots that can be told apart. The first sheet showed a row of nine as one grey bar (`index.yaml`: `gui/gui_hb_bg.png`, 49 by 49, every pixel `(0, 0, 0, 43)`). Done: `ui_widget_slot` draws it one texture pixel in from each side of the rect.

5. **The game does not draw through `ui` yet.** `ui_init` and `ui_deinit` run in the program, but `game` does not call `ui_frame_begin_over`, so no UI reaches a screen in this stage. Left that way on purpose: nothing draws until Stage 14.2's HUD, which `run/src/render/frame/README.md` puts between `render_frame_take` and `render_frame_present`, and until then the copy would cost time for nothing. `run/src/ui/README.md` gives the exact calls.

6. **A test error of my own, for the record.** `a_slot_lights_up_and_says_what_the_pointer_did` first failed with `4284444286 != 4284707975`: the model began from a clean background while the sheet still held the slot of the frame before. Fixed in the test, with a fresh sheet. The widget was right.

## Evidence

- `run_ui_font`: 30 tests, 0 failed, on the debug and sanitize presets (ASan and UBSan both quiet).
- A mutation to show the model can fail: `remainder >= rect_width` changed to `>` in `ui_compose_row_stretch` failed 5 tests, including `random_images_match_a_nearest_pixel_model_anywhere_and_clipped`. Restored.
- The sheet's hash `0xa7dd20676cf0849d` was set after looking at its PNG, written by `UI_FONT_PNG`, on a gradient: text, buttons in all three states, checkboxes, slider, text field with caret, nine slots, bar, tooltip on top, pointer.
- Also passed after the change to `app_t`: `run_types`, `run_display_headless`, `run_display_mode`, `run_game`, `run_render_frame`, `clang_format`, `standards_lint`.
