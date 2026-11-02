# tests/run/ui/font

**Purpose:** ctest `run_ui_font`. Tests `run/src/ui/` with `compose`, `font` and `widget`.
**Tier:** test.
**Owns:** the test arena, and a `ui_t`'s and a `texture_t`'s regions, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/`, `run/src/texture/`, `run/src/simd/reference/`, the built program, and a card for that program.

What it holds to:

- The font against `textures/ui/font/index.yaml` read by hand; text measured and drawn by hand; random text anywhere and clipped against a separate model that reads the atlas itself and blends a pixel at a time with `simd_reference_blend_over`; every byte of the guard band round the canvas unchanged.
- Fills and stretched images the same way, and nine parts by hand.
- Each widget on input frames made here: clicks that count and clicks that do not, a slider by hand to its ends, every key of a US keyboard, a field's focus and capacity, a bar's fill, where a tooltip lands.
- `ui_frame_begin_over` leaves the picture alone and does not blend twice. No heap and no table memory in 200 frames of every widget.
- `minecraft_c --headless --ticks 2` starts and ends with a `ui_t` in its `app_t`, with no leak.

**The sheet.** Test `the_whole_sheet_is_the_picture_that_was_looked_at` draws one of everything and compares a hash of its pixels with `SHEET_HASH`. The number was set after looking at that picture. To look again, whether after a change to the look or because the test fails:

    UI_FONT_PNG=/tmp/sheet.png TEST_FILTER=the_whole_sheet build/debug/bin/tests/run_ui_font

Run it from a build, with `tools/test-or-build.sh` in front. If the picture is right, the run prints the new hash to put in `SHEET_HASH`.
