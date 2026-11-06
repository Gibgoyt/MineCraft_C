# run/src/ui/font

**Purpose:** the bitmap font: `textures/ui/font/`, our own printable ASCII in cells of 8 by 8, and a seventh row past ASCII (plan 004 phase 09) with the degree sign and the middle dot, read into a table of glyphs, measured and drawn at any whole scale.
**Tier:** Arena: the table comes from the arena its Owner gives.
**Owns:** nothing.
**Lifetime:** Process, as the arena it is given.
**Threads:** `main`. Measuring only reads, but drawing uses the canvas's one row scratch.
**Public functions** (`run/include/ui/font/index.h`):

- `ui_font_init`, `ui_font_deinit`: read the texture into a table; empty the handle.
- `ui_font_load`: the same from pixels, which is how a damaged atlas can be tested.
- `ui_font_glyph`: the glyph of a byte.
- `ui_font_measure`: how big text will be.
- `ui_font_draw`: draw it, in a colour, with a shadow if asked for.

**May be called by:** `ui` and its parts.
**Depends on:** `arena`, `log`, `texture`, and its sibling `ui/compose`, as that README allows.

## What it does with text

- A glyph is at most 5 by 7, at the top left of its cell. Init refuses an atlas with ink anywhere else, and one of any other size, so that a bad texture is found at start and not as a clipped letter.
- Glyphs are proportional: each advances by its own ink's width plus one empty column, and a space is 3 wide, plus that column. `'A'` advances 6, `'!'` 4. Lines are 9 apart.
- Text is NUL-terminated UTF-8. The degree sign (bytes 0xC2 0xB0) and the middle dot (0xC2 0xB7) are one character each, from the seventh row (`ui_font_glyph_extra`). Any other character past ASCII is one `'?'` however many bytes it has, as the atlas has no others, so a name in any language comes out the right length. So is a control character, and DEL, whose cell is empty. A tab is a space.
- Each scanline of a line is built whole in the canvas's row scratch, clear where there is no ink, and goes to the kernel in one call, which leaves clear pixels as it found them. A scanline with no ink makes no call.
