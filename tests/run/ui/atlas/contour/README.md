# tests/run/ui/atlas/contour

**Purpose:** ctest `run_ui_atlas_contour`. Tests plan 007 phase 02's Atlas contour lines: the pixel with its cover depth
(`run/include/ui/atlas/tile/`), the column top that measures it, mips that average it, the tile file's version 2 reading
version 1, the zoom table and the lines `run/src/ui/atlas/layer/` draws on the Terrain layer. Beside `run_ui_atlas` (its
parent directory), which was near the 1500-line split.
**Tier:** test.
**Owns:** the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/atlas/`, `run/src/file/`, the block tables.

What it holds to:

- **The pixel:** every height from -256 to 1279 with cover 0, 7 and 31 round-trips exactly, with its colour and its ground
  (`z - cover`); not generated is `0x07FF0000`; heights and cover past their ends are held, never wrapped.
- **The column top:** an oak (grass at 72, log to 77, leaves to 79) is top 79, cover 7, ground 72; stone is cover 0; a
  jungle log 40 tall, and leaves 51 over the ground, are cover 31; a canopy over air and a flower is cover 7; a snow layer
  is cover 1; leaves over the sea measure down to the water; leaves with nothing under them are cover 31.
- **Mips:** cover 0, 4, 8 and one not generated give 4; a halving of a whole tile is each parent's rounded mean.
- **Version 1:** `fixtures/tile_v1_L0_-3_5.tile` was written by the Atlas before this phase (version 1, runs, seed 42,
  level 0, tile (-3, 5); pixel k is `v1_pixel(k)` in the test). It decodes with every height and colour as written and
  cover 0, writes back as version 2, and a flipped header or payload byte, or version 3, is still refused.
- **Spacing:** `ui_atlas_contour_interval` gives (25, 250) to zoom 0, (50, 500) to 4, (100, 500) to 8, (250, 1000) beyond,
  and at every zoom to 8 a slope of 1 keeps minor lines 3 pixels apart.
- **Lines**, on a white made-up ground with shading off, read back from each pixel's darkening (0.5 minor, 0.3 index): on
  the plane z = floor(x / 10) at zoom 0, minor lines in exactly the columns x = 249 mod 250 and an index line 2 pixels wide
  at x 2499 and 2500; trees 6 to 20 tall over 400 columns move no line, where the same trees read as the ground would; on a
  cone of slope 1, at every zoom from -4 to 8, lines along a radius start at least 3 pixels apart.
