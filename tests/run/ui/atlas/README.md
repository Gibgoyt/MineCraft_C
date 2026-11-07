# tests/run/ui/atlas

**Purpose:** ctest `run_ui_atlas`. Tests the Atlas (plan 004 phase 09): `run/src/ui/atlas/` and its `tile/`, `draw/` and
`layer/`, with what `run/src/game/` and `run/src/game/atlas/` do with it.
**Tier:** test.
**Owns:** the test arena, a `ui_t`'s and a `texture_t`'s regions (the Atlas's region is ui's), a climate's and a job
system's, temporary directories under `/tmp/mc_ui_atlas_*`, all given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the lock test starts a writer, a reader and a saver; the band test runs bands on 1 worker and 16; the
built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/`, `run/src/climate/`, `run/src/terrain/`, `run/src/display/headless/png/`,
`compile/src/png/` to read the program's screenshots, and the built program.

What it holds to:

- **Tiles:** pixels round-trip at every height from -250 to 1250; a column's top skips air, a flower and a torch and stops
  at water, against a hand-built column; 10 000 random columns land where the plan's arithmetic puts them, against a
  block-by-block model, and nowhere else; halving is the Background's worked example (25, 26, 5, height 70) and random
  children against a model of their own; files read back byte for byte, raw and runs, runs only when smaller, and every
  flipped header byte, a payload byte, another seed, level or place, a short file and runs short or long are refused.
- **The store:** a tile made before its file was read keeps its new columns and takes the rest from the file; 50 columns
  written, the world closed and opened again read back the same before anything streams, and another seed's world refuses
  the files; mips at levels 1 to 7 after a save are the halving by hand of level 0; for 5 s a writer, a reader of three
  levels and a saver race, and the longest wait of a column write stays under 2 ms (printed); with every slot dirty a write
  is dropped, counted and said once, and a save makes room.
- **Drawing:** the layer table is whole (26 rows, keys unique and safe in a file name, every group used, every field a
  scale or a list, only now layers over grey); hill shading, marching squares (both saddles) and the temperature scale are
  their worked examples; **golden pictures** of every layer at 1 and 8 blocks a pixel (52); over the ungenerated third every
  pixel is grey, or on an over-grey layer an outline's colour, and the hurricane's ring is at least 90% drawn there; the
  Atlas's row for every climate field, shading off and sampled every pixel, is `climate_layer_draw`'s bytes; 1, 7 and 16
  bands, and 16 bands on 1 worker and on 16, give the same bytes for every layer; a picture panned 13 and 7 pixels at 2.83
  blocks a pixel is the same where the two overlap, on every layer.
- **On screen:** a normals layer held still for 10 s of frames draws one picture and a day change one more; a now layer
  draws 20 in 10 s, give or take one; columns streaming at 100 a second redraw at most 20 times; the keys of the table (M,
  Esc, the wheel about the pointer, a drag at 4 blocks a pixel, 1 to 9 with cycling, [ and ] round with Shift, P); M does
  nothing with a screen up and the Atlas keeps E; the readout is `climate_normals_at`'s and `climate_air_at`'s numbers as
  formatted, and `not explored` off the explored land; 200 frames with keys, zooms, pans and 1000 columns take nothing from
  the heap.
- **The built program:** `--headless --no-sound --test-world --walk` with a script that presses `m`, waits 40 ticks and
  presses `escape`: the two Atlas screenshots differ from the world in over 90% of pixels and little from each other, and
  the last is the world again. It ends with no leak.

**Golden pictures:** 320 by 240, seed 2198175988018515243 round (1290, -2410), day 196 at t = 196.4, the right third
ungenerated, the ground from the terrain's height estimate coloured by a fixed palette, the weather systems a fixed list:
a small hurricane over the ungenerated third coming west, a low with its front, a high and three thunderstorm cells
(small, so they show at 1 block a pixel). The test holds each picture to an FNV-1a hash in `fixtures/goldens.txt`, not a
PNG: 52 PNGs would be 15 MB in git (Risk 7 of the phase file). To see them, and to make the hashes again after a
deliberate change, after looking at every picture:

    MC_ATLAS_PNG_DIR=/tmp/atlas MC_GOLDEN_WRITE=1 tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_ui_atlas$'
