# 007-ahmed-bohemoth-realistic-landforms — phase 2: Atlas contour lines

**Status:** built and tested; two todos open: the worst-case column top misses 20 µs, and labels wait for Ahmed (Q5)   (updated 2026-11-12)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md` (sections 0 "What the picture shows", 3.13, 6, 12 Q5), then plan 004's Atlas phase `plans/004-ahmed-bohemoth-climate-system-phase-09-the-atlas.md` ("Isobars and contours", the layer table) and `run/src/ui/atlas/README.md`. Where this file adds a number the master does not give, it says so and why.

## Goal

When Ahmed presses `M`, the **Terrain** layer shows contour lines by default:

- thin lines, and thicker **index lines** every 250, 500 or 1000 blocks;
- spacing chosen from the zoom, so a 1:1 slope never smears into a solid dark band;
- the lines **lie on the ground under the trees**, not on tree crowns.

Pressing `1` again gives the same map with no lines (`terrain_plain`, which replaces `terrain_lines`).

To read the ground under trees:

- each Atlas pixel now also holds a 5-bit **cover depth**: how far its top block stands above the ground;
- the tile file is version 2;
- a version-1 file still opens, with cover 0.

Hill shading and colours are unchanged. Goldens are re-made and looked at. `bench_atlas` still meets every budget. The map tools draw the same lines.

**No land changes.** This phase is independent of phases 03 to 05, and may be built before, between or beside them.

## Where this fits

**The vision.** The Atlas is where Ahmed watches the world (plan 004 phase 09). Plan 004's master 9.3 promised "Terrain with hill shading, height lines every 100". Ahmed asked, looking at `~/Pictures/2.png`: "let's please show contour lines". Every later phase of plan 007 is judged partly by eye on this map: plateaus show as wide gaps between bunched lines at an escarpment, and concave peaks as lines bunching towards the top. So it comes early.

**What reads it:**

| Reader | What |
|---|---|
| Phases 03 to 06 gates | Ahmed reads plateau edges, ranges and peaks from the lines |
| `bench_world_map_atlas` and `bench_landform` | The same spacing table, so pictures and the game agree |
| Later plans (settlements, roads) | `ui_atlas_pixel_ground_z`, the ground height under cover, for any map that needs the ground |

**What this phase must NOT build:**

- No change to land or to `run/src/terrain/`.
- No new layer beyond renaming `terrain_lines` to `terrain_plain`. `UI_ATLAS_LAYER_COUNT` stays 26.
- No change to hill shading or colours. Shading still reads the top (with trees), so the map looks as it does today apart from the lines.
- No labels unless Ahmed asks (master Q5). The optional step below stays unticked otherwise.

## Ahmed's words

- "When bringing up the map, look at ~/Pictures/2.png, let's please show contour lines"
- From plan 004's master 9.3: "Other: Terrain with hill shading, height lines every 100"

`~/Pictures/2.png` is the `terrain` layer at x -116, y 163, z 348: hill shading, tree crowns as dots, and no lines.

---

## Background, explained

### Contour lines

A contour joins places of equal height. The Atlas draws one where a pixel and its east or south neighbour fall in different bands of the interval $I$:

$$\text{line} \iff \lfloor z_{\text{here}}/I \rfloor \ne \lfloor z_{\text{east}}/I \rfloor \ \lor\ \lfloor z_{\text{here}}/I \rfloor \ne \lfloor z_{\text{south}}/I \rfloor$$

`layer_floor_div` already rounds towards minus infinity, so -1 and 0 are in different bands of 100 (`run/src/ui/atlas/layer/index.c:361-368`).

**Example:** $I = 50$, here 148, east 152. Then $\lfloor 148/50 \rfloor = 2$ and $\lfloor 152/50 \rfloor = 3$, so there is a line here.

### Why the spacing must follow the zoom

On a slope of gradient $g$, at $s$ blocks a pixel, lines of interval $I$ lie $I / (g\, s)$ pixels apart. Lines closer than about 3 pixels merge into a dark band. So for $g = 1$, $I \ge 3s$.

| $s$, blocks a pixel (zoom) | $3s$ | Minor $I$ | Index (2 px, darker) | Pixels apart at $g = 1$ |
|---|---|---|---|---|
| 0.25 to 1 (zoom -4 to 0) | ≤ 3 | 25 | 250 | 25 to 100 |
| 1.41 to 4 (1 to 4) | 4.2 to 12 | 50 | 500 | 12.5 to 35 |
| 5.66 to 16 (5 to 8) | 17 to 48 | 100 | 500 | 6.3 to 17.7 |
| 22.6 to 256 (9 to 16) | 68 to 768 | 250 | 1000 | 1 to 11 |

At the top zooms, steep faces still merge. That is accepted: the Atlas is then showing tens of km, where an escarpment drawn as a dark band is right. The table is this file's choice. It meets Ahmed's "thin every 50 or 100, thicker every 500" at the default zoom 2 (2 blocks a pixel) and zooms 1 to 8.

**Index lines.** A pixel is on an index line if its band of the index interval differs from **any** of its 4 neighbours (east, south, west, north). So the line is 2 pixels wide, one on each side of the crossing. Its shade is multiplied by 0.3; a minor line's by 0.5, as today.

### Cover depth: the ground under the trees

`ui_atlas_column_top` keeps the first block from the top that is not air, a cross plant, a crop, a torch or a door (`run/src/ui/atlas/tile/index.c:63-79`). **Leaves and logs count.** So over a forest, the height is the crowns', 5 to 30 blocks above the ground. A contour over a forest would wander round every tree.

**The pixel today** (`run/include/ui/atlas/tile/index.h:11-38`): bits 0 to 15 are RGB565 colour, and bits 16 to 31 are the height from sea level as `int16_t`, with `INT16_MIN` meaning not generated. Heights run from `COLUMN_Z_STORAGE_MIN - MC_WORLD_Z_SEA` = -256 to 1279: 1536 values, which needs 11 bits.

**The pixel after:**

| Bits | What |
|---|---|
| 0 to 15 | RGB565 colour of the top block (unchanged) |
| 16 to 26 | Height of the top + 256, 0 to 1535; **2047 = not generated** |
| 27 to 31 | Cover depth $d$, 0 to 31: the top's height minus the height of the first block below it, going down, that is not cover |

So the ground is $z - d$. A block is **cover** if its definition has the new flag `map_cover` (`config/blocks/*/index.yaml`, read by `compile/src/game_tables/blocks/`):

- every `*_leaves` and `*_log`;
- mushroom blocks;
- `snow` layers;
- cactus;
- bamboo.

A column whose cover runs deeper than 31 (the tallest jungle tree) stores 31, so its line sits up to the excess too high. That is rare, and accepted.

**Example:** an oak on grass at 72. The top is a leaf at 79, and the first non-cover block going down is grass at 72. So $z = 79$, $d = 7$, and the ground is 72. The contour at 75 ($I = 25$) is not drawn there, as it would be today.

**Mips** (`ui_atlas_tile_halve`) average $d$ like the height: `(sum + floor(n / 2)) / n` over generated children. So a zoomed-out forest's ground is about the mean ground.

### The tile file, version 2

The 64-byte header says `version 1` (`run/src/ui/atlas/tile/README.md`), and decode refuses any wrong field.

- Version 2 writes the new pixels.
- **Decode accepts 1 and 2.** A version-1 pixel is converted: height + 256 into bits 16 to 26, cover 0, and `INT16_MIN` to 2047.
- Tiles are rewritten as version 2 the next time they are dirty.
- A world explored before this phase shows its old lines on crowns until it is re-explored. That is said in the Atlas README.

---

## Design

### Directories and files

- `run/include/ui/atlas/tile/index.h`: the new pack, `ui_atlas_pixel_z` (the top, as today), `ui_atlas_pixel_cover`, `ui_atlas_pixel_ground_z`, `UI_ATLAS_HEIGHT_NONE_CODE 2047`; `ui_atlas_pixel_generated` unchanged in meaning.
- `run/src/ui/atlas/tile/index.c`: `ui_atlas_column_top` finds cover depth on the way down; `ui_atlas_tile_halve` averages it; encode writes version 2; decode reads 1 and 2.
- `run/src/ui/atlas/layer/index.c`: rows 0 and 1; `layer_band_terrain` draws by the table above when the row's value is contours; a helper `ui_atlas_contour_interval(zoom, &minor, &index)`.
- `run/include/ui/atlas/layer/index.h`: the helper's prototype; `UI_ATLAS_VALUE_CONTOURS` keeps its number.
- `config/blocks/*/index.yaml`, `compile/src/game_tables/blocks/`: the `map_cover` flag, its README line.
- `run/src/ui/atlas/README.md`, `tile/README.md`, `layer/README.md`.

### The layer rows

| # | Key | Label | Value |
|---|---|---|---|
| 0 | `terrain` | "Terrain, contours" | `UI_ATLAS_VALUE_CONTOURS` |
| 1 | `terrain_plain` | "Terrain" | `UI_ATLAS_VALUE_NONE` |

The default layer for a new world stays row 0, which now has lines.

### `ui_atlas_column_top` with cover

Walking down a column, it already stops at the first counting block, the top. It now also keeps walking while the block is cover, to the first non-cover counting block, at most 32 layers further, and stores the difference.

**Cost:** for the 256 columns of a chunk, at most 32 more layers each, reading voxels already in cache. The budget stays under 20 µs (12.8 µs today, `bench/atlas/README.md`). Measured.

### Optional: index labels (only if Ahmed says yes to master Q5)

On the overlay pass:

- for each cell of 400 by 400 pixels, find the index-line pixel nearest the cell's centre;
- draw its height (for example "500") in the font at scale 1, on a dark box, there;
- skip it if the box would cover the player's mark or the legend.

Not drawn over grey.

---

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/ui/atlas/tile/`, `run/{src,include}/ui/atlas/layer/`, `run/src/ui/atlas/README.md`
- `tests/run/ui/atlas/` (tests, `fixtures/goldens.txt`)
- `config/blocks/*/index.yaml` (the `map_cover` line only), `compile/src/game_tables/blocks/` (reading it), `config/blocks/README.md`
- `bench/atlas/README.md` (results), `bench/world_map/atlas/` (the layer key rename only)

**Wiring outside them:** none expected. `run/src/game/` calls `ui_atlas_column_top` with the same arguments.

## Steps

1. **Read** `run/src/ui/atlas/tile/index.c` and `layer/index.c`, `tests/run/ui/atlas/index.c`'s goldens and tile tests, and `bench/atlas/`. *Why: the pixel is read in many places; every reader of bits 16 to 31 must move to the helpers.*
2. **`map_cover`** in the block tables, with a test that every `*_leaves` and `*_log` has it, and that stone, grass, dirt, sand, water and ice do not. *Why: cover must be data, not a list of names in C.*
3. **The new pixel and its helpers**; grep every `>> 16` and `ui_atlas_pixel_z` reader and move each to the right helper. *Why: a reader left on the old layout would read heights 32 times too large.*
4. **`ui_atlas_column_top` finds cover; `ui_atlas_tile_halve` averages it.** With tests.
5. **Tile file version 2, reading version 1.** With a version-1 fixture file made by today's code, committed to `tests/run/ui/atlas/fixtures/`. *Why: Ahmed's explored worlds must still open their maps.*
6. **The contour table and `layer_band_terrain`;** rows 0 and 1. *Why: the visible change.*
7. **Goldens:** re-made, every changed picture looked at, and a line per golden in the test README saying why it moved.
8. **`bench_atlas`** on release; record.
9. **Optional labels** if Ahmed said yes.
10. READMEs; tick each todo as it lands.

## Todo

- [x] `map_cover` flag on leaves, logs, mushroom blocks, snow layer, cactus and bamboo, read by `compile/src/game_tables/blocks/`, with its README line and test
- [x] New pixel layout (colour 16, height 11, cover 5) and helpers in `run/include/ui/atlas/tile/index.h`; every reader moved
- [x] `ui_atlas_column_top` stores cover depth; `ui_atlas_tile_halve` averages it
- [x] Tile file version 2; decode reads version 1 with cover 0 (fixture from today's code)
- [x] `ui_atlas_contour_interval` and the zoom table; index lines 2 px at 0.3, minor 1 px at 0.5; contours read the ground (`height - cover`); shading reads the top as today
- [x] Row 0 `terrain` with contours (default); row 1 `terrain_plain` without; `bench/world_map/atlas/` key updated
- [x] Goldens re-made and looked at, with reasons; `run_ui_atlas` and `run_ui_atlas_game` pass
- [ ] `bench_atlas` on release: every layer under 60 ms, column top under 20 µs, recorded
- [ ] (Optional, only if Ahmed says yes) index labels
- [x] READMEs updated (`run/src/ui/atlas/`, `tile/`, `layer/`, `config/blocks/`)
- [x] Every `run_terrain_*` digest unchanged

## Tests

In `tests/run/ui/atlas/index.c` unless named. "Fails if" says what a wrong implementation looks like.

- **`a_pixel_packs_and_unpacks_height_and_cover`.** Every height from -256 to 1279 with cover 0, 7 and 31; and not generated. Each round-trips exactly. Fails on an off-by-one in the 256 offset, or a sign error.
- **`a_tree_s_cover_is_measured_down_to_the_ground`.** A synthetic column: grass at 72, a log from 73 to 77, leaves to 79. Top 79, cover 7, ground 72. A column of stone to 64 has cover 0. A jungle tree of 40 has cover 31.
- **`a_version_one_tile_still_opens`.** The committed version-1 fixture decodes. Every pixel's height and colour are as before, with cover 0. A flipped byte is still refused.
- **`mips_average_cover`.** Four children with cover 0, 4, 8 and (not generated): the parent's cover is 4 (`(12 + 1) / 3 = 4` floored).
- **`contour_spacing_follows_the_zoom`.** `ui_atlas_contour_interval` gives (25, 250) at zoom -4 and 0; (50, 500) at 1, 2 and 4; (100, 500) at 5 and 8; (250, 1000) at 9 and 16.
- **`lines_sit_where_the_plane_crosses_each_band`.** A synthetic ground through `ui_atlas_estimate_t`'s reader shape (a plane $z = x/10$, cover 0, flat in $y$), at zoom 0 ($s = 1$). Minor lines are in exactly the picture columns where $\lfloor z/25 \rfloor$ changes to the east: every 250 pixels. Index lines are 2 pixels wide where $\lfloor z/250 \rfloor$ changes: every 2500 pixels, outside a 1920-wide picture unless the origin is placed on one, so the test places it. Fails if lines move by a pixel, or index lines are 1 pixel.
- **`lines_follow_the_ground_under_trees`.** The same plane, with a band of synthetic trees (cover 6 to 20, top = ground + cover). The lines are in the same columns as without trees. Today's rule, on the top, would move them.
- **`a_one_to_one_cone_never_smears`.** A cone with $g = 1$ at every default zoom: no two minor lines closer than 3 pixels along a radius, for zoom ≤ 8.
- **`shading_and_colour_did_not_change`.** `terrain_plain` at both golden zooms gives the same bytes as `terrain` did before this phase (the old golden hash for `terrain`).
- **`golden_pictures_of_every_layer_at_two_zooms`** (existing). `terrain`'s two goldens re-made with lines. `terrain_plain`'s are the old `terrain` goldens. `terrain_lines`' entries are removed. Every layer drawn over the terrain band keeps its hash unless it reads the pixel's height (none should); a moved one is an Issue found.
- **`run_ui_atlas_game`** (existing). Passes.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_ui_atlas.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_game_tables.*$' --output-on-failure
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map_atlas /tmp/mc_c_atlas 2198175988018515243 --layer terrain --zoom 4
    pgrep -af "$PWD/build"    # must print nothing

If the block tables' test has another name in this repo, use that name and record it here. `run_game_tables.*` is this file's guess.

## Gate

Ahmed opens his own world, presses `M` and sees contour lines on the Terrain layer:

- thin lines every 50 at the default zoom, and thicker ones every 500;
- following the ground through forest;
- fewer lines as he zooms out.

`1` again shows the plain map. He says whether he wants labels (master Q5).

## Risks and open questions

| Risk or question | Default |
|---|---|
| Some reader of the pixel still shifts by 16 and reads a 16-bit height | Step 3 greps every reader. The pack test, plus every golden, catch a wrong one |
| Cover depth at the column job's worker costs more than 20 µs | Measure. The extra walk stops at the first non-cover block; most columns have cover 0 |
| Snow layers counted as cover make a snowy field's ground one block lower than its top | That is correct: the snow lies on the ground |
| Hill shading on the top (trees) and contours on the ground may look odd together | Shading unchanged by design (the look Ahmed knows). If he prefers shading on the ground too, it is one line: master Q5 follow-up |
| Old worlds' maps keep lines on crowns until re-explored | Said in the README; acceptable |
| Labels clutter | Off by default (master Q5) |

## Commits

Appended as they land:

- `66ab8109` Draw contour lines on the Atlas Terrain layer by default, spaced by the zoom with index lines, on the ground under the trees from a new 5-bit cover depth in each pixel, with tile files at version 2 still reading version 1.
- `7c4e7bc4` Add the map_cover block flag on leaves, logs, vines, snow layers and cactus, so the Atlas can find the ground under them.

## Issues found

- **`map_cover` blocks (step 2).** The repo has no mushroom blocks (`brown_mushroom` and `red_mushroom` are cross plants, which the column top already skips) and no bamboo, so neither has the flag. `vine` has it too, which this file does not list: a vine hangs outside a canopy and is a thin block that counts, so without the flag a column whose top is a vine would read its ground at the vine. The flags are every `*_leaves` (8), every `*_log` (8), `snow_layer`, `cactus` and `vine`. The block tables' test is `compile_game_tables` (not `run_game_tables.*`, this file's guess); the new test is `trees_snow_and_cactus_are_map_cover_and_the_ground_is_not`.
- **Every pixel reader (step 3).** `ui_atlas_pixel_z` keeps its meaning (the top) and its `int16_t` type, so the readout in `run/src/ui/atlas/index.c` and the store needed no change; `ui_atlas_pixel_generated` and `UI_ATLAS_PIXEL_NONE` (now `0x07FF0000`) carry the rest. The only direct `>> 16` readers were in `tests/run/ui/atlas/index.c` (the mip model), now on the helpers. `ui_atlas_pixel_pack` takes an `int32_t` and holds it to -256 to 1279 (the estimate reader already clamped to ±32000, which would have wrapped in 11 bits). `UI_ATLAS_HEIGHT_NONE` (`INT16_MIN`) is gone.
- **Tests in a new program.** `tests/run/ui/atlas/index.c` was at 1491 lines, so the new tests are `tests/run/ui/atlas/contour/` (ctest `run_ui_atlas_contour`, matched by the Verification's `^run_ui_atlas.*$`). `shading_and_colour_did_not_change` is a check inside `golden_pictures_of_every_layer_at_two_zooms` (the gold world lives there): `terrain_plain_*` must equal the old `terrain_*` hashes, `0x994D128267DEFE6A` and `0x494E54C56AFDACA9`.
- **The version-1 fixture** `tests/run/ui/atlas/fixtures/tile_v1_L0_-3_5.tile` (32 832 bytes, runs) was written by a throwaway test against the Atlas code of `7c4e7bc4` (version 1), then the writer was removed; its pixels are `v1_pixel(k)` in the contour test, heights -256 to 1279 and every 17th run not generated.
- **Index lines.** The master (3.13) says "the crossing pixel and its south-east neighbour"; this file says one pixel each side of the crossing, from any of the 4 neighbours. Built as this file says. The master also says contours *and shading* read the ground; this file keeps shading on the top. Built as this file says (shading unchanged, the plain goldens prove it).
- **No lines over sea.** In the game the sea's top is flat water at 0, so no line is drawn there and nothing extra was needed; a line marks the coast only where water at 0 meets land in another band (never at 25: both are band 0). The golden pictures' ground is the terrain estimate, whose sea is the sea floor, so the goldens draw bathymetry; that is the test ground, not the game. Lines are not cut below 0, so phase 05's dry basins will get them.
- **`bench/world_map/atlas/`** names no layer key (it takes `--layer <key|all>` and loops over the table), so there was nothing to rename; it draws through `ui_atlas_draw_picture` and makes its pixels with `ui_atlas_column_top`, so it shows the same lines, on the ground.
- **Goldens (step 7)**, looked at in PNG: `terrain_z1` shows thin lines every 25 and an index line at 250 crossing the hill; `terrain_z8` thin lines every 100 and a dark index line at 500 round the mountain. `terrain_plain_*` are the old `terrain_*`; `terrain_lines_*` removed; no other layer's hash moved. Reasons in `tests/run/ui/atlas/README.md`.
- **Formatting.** `7c4e7bc4` left `compile/src/game_tables/blocks/index.c` and `tests/compile/game_tables/index.c` not clang-formatted (`clang_format` would have failed on that commit alone); the next commit formats them.
- **`bench_atlas` (step 8), todo left open.** Every layer is under 60 ms by the median (`terrain` with lines 23.8 ms against `terrain_plain` 11.1 ms; worst `monsoon` 41.7 ms), opening 23.4 ms, main's frame 0.725 ms, all on a busy machine (load 82 to 21); full table in `bench/atlas/README.md`. The **column top missed 20 µs** at first: 31.5 µs. The cause is partly a bench bug: its one column was filled with block 1, `acacia_leaves`, while its comment said stone, so with leaves now cover every pixel walked the whole 31 layers, the worst case there is. Two changes: `ui_atlas_column_top` now looks each voxel up in a per-call table of what the block is (counts, cover) instead of a shape switch and a flag test (31.5 to 26.5 µs on that column); and `bench/atlas/index.c` (outside this phase's owned paths, a small diff) times three named columns: the real terrain's kind 15.4 µs, a forest with an oak canopy on every pixel 17.2 µs (both met), and leaves from the floor to 64, 29.3 µs, **MISSED**. No real column is solid leaves 320 deep, so the game's columns meet the budget, but the todo asks for the column top under 20 µs and the worst case does not, so it stays unticked for Ahmed or the orchestrator to judge. Going further would mean walking cover by whole 64-pixel words of bits rather than voxel by voxel.
- **The map tool**, `bench_world_map_atlas /tmp/mc_c_atlas 2198175988018515243 --layer terrain --zoom 4`, wrote its picture in 6.1 ms. Looked at: lines every 50 and index lines every 500 on the snowy mountain at the south-west, and smooth lines running straight through the speckled forest on the south-east, not round the trees.
- **Labels (optional step 9)** not built: master Q5's default is no labels, and Ahmed has not said otherwise.
- **Verification** as run: debug build; `run_ui_atlas`, `run_ui_atlas_contour` and `run_ui_atlas_game` pass (the last on the card, with Ahmed's game not running, card idle at 300 MHz and 66 °C before it); `compile_game_tables` passes; all 10 `run_terrain_*` pass; `standards_lint` and `clang_format` pass; release build; `bench_atlas` and `bench_world_map_atlas` as above. `pgrep -af "$PWD/build"` prints nothing.
