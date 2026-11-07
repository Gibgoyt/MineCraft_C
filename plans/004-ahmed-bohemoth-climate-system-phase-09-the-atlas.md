# 004-ahmed-bohemoth-climate-system — phase 9: The Atlas

**Status:** in progress: built, gate-checked and agent-verified (Issues found 24 to 28: a crash fixed, the phase 05 layers brought under 60 ms); left: Ahmed's gate, and his word on the two bench targets still missed, temperature now and a column's top (Issues found 27)   (updated 2026-11-07)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 2.4 and 9.3 of the master plan, `plans/004-ahmed-bohemoth-climate-system.md`. The master plan is the source of every number here. Where this file and the master plan disagree, the master plan wins, and the disagreement goes in **Issues found**.

## Goal

Pressing `M` while playing opens the Atlas: a full-screen map of every column the world has generated, with ungenerated land drawn grey. The game keeps running behind it. Keys `1` to `9` choose among 26 layers: terrain, temperature, rain, wind, ocean currents, pressure, weather systems, climate class, and this week's snow, soil water and floods. Normals layers have a day slider. "Now" layers move as the weather moves. A storm over grey land is drawn as an outline, so it can be seen coming. The cell under the mouse is read out. What the Atlas knows about the ground is written as columns generate, kept in tiles with zoomed-out levels, and saved with the world. The headless map tool draws every layer with the same code. Golden pictures of every layer at two zooms pass, and the Atlas opens over 100 km² of generated land in under 100 ms.

You can tell it is done without the todo list: open a world, walk a while, press `M`, and watch a hurricane cross the grey edge onto land you have explored.

## Where this fits

The vision file says it plainly: the game is a stack, worked out top to bottom, from the seed to terrain, climate, and later geology, vegetation, settlements, nations, the economy and the player. Every later layer reads the climate. **The Atlas is where Ahmed watches the world.** In this plan it shows the climate: what phases 03 to 07 work out. In later plans it will also show nations, settlements, trade routes and caravans moving along them (vision file, Part 2). So the Atlas is built as a **layer registry**: a table, one row per layer, each with its own draw function, legend and time mode. A later plan adds a layer by adding a row and a function. Nothing else in the Atlas changes shape for that.

What this phase must **not** build:

- No nation, settlement, trade-route or caravan layers. No borders. No settlement names on the map. The registry leaves room for them, and that is all.
- No vegetation or biome layer that follows climate (master plan 2.6). The climate class layer shows what the climate says; the ground colour shows what the terrain made. The mismatch is meant to be visible.
- No geology: no plateau or basin layers.
- No climate drawn over ungenerated land (master plan 2.4). Only system outlines cross the grey.
- No minimap on the HUD, no waypoints, no clicking to travel, no editing anything from the map.
- No new weather maths. Every value the Atlas draws comes from functions phases 02 to 07 already made. If a layer needs a number nobody computes, that is an issue in that phase, written in **Issues found** here.

The HUD's top corner (phase 10) will later show the settlement name above the date line. That is phase 10's concern and a later plan's. This phase does not touch the HUD.

## Ahmed's words

- "create winds please! And a map that i hit "M" to bring up map, it shows all generated chunks, and it shows winds, ocean current, etc... and I can filter temperature"
- "the game keeps running! and yes! atlas only shows generated blocks! other blocks are grayed out"
- "the user must be able to change filter to see wind current, ocean current, pressure systems like cyclones and anti-cyclones please, as well as rain pressure cells etc... to view incoming thunderstorms"
- "3 different ocean currents/temperatures would be nice"
- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"

## Background, explained

This section explains every idea the design uses. Read it if any word in the design is unfamiliar.

### Pixels, pictures and the UI layer

A picture on screen is a grid of **pixels**. Each pixel here is 4 bytes: alpha (how opaque), red, green and blue, one byte each, written `0xAARRGGBB`. A 1920 by 1080 picture is

$$1920 \times 1080 \times 4 = 8\,294\,400 \text{ bytes} \approx 7.9 \text{ MiB}.$$

The game's 3D world is drawn by the graphics card. Everything flat on top of it, the HUD, the inventory, the menus, is drawn by the CPU into a copy of that picture. That is the **UI layer**: `run/src/ui/`. It draws through `ui_canvas_t` (`run/include/ui/compose/index.h`), which knows where the pixels are, how wide a row is in bytes (the **pitch**), and a **clip** rectangle outside which nothing is touched. `ui_compose_fill` fills a rectangle with one colour. `ui_compose_image` copies an image onto it, blending by alpha. `ui_font_draw` writes text.

The Atlas draws its map into its **own** picture, a block of memory it owns, writing pixels directly. Then each frame, it copies that picture onto the canvas with `ui_compose_image_whole`. Copying 8 MiB takes about 1 to 2 ms. Drawing the map from scratch takes much longer (tens of milliseconds), because every pixel needs a lookup, a shading sum and a colour. So the map is drawn **once per change** and kept. That is **caching a picture**: keep the answer until something it depends on changes.

What the picture depends on is written down as a **view**: the layer, the zoom, where the middle is, the picture's size, the day (for normals layers), the moment (for "now" layers), the census week (for this week's layers) and a count of ground changes. If the view main wants is byte-for-byte the view the kept picture was drawn from, nothing is drawn. Otherwise a new picture is drawn, on a worker thread, while main keeps showing the old one.

### Map tiles

The Atlas needs to know, for every generated column, the colour and height of its top. A world can be explored over hundreds of km², so this is kept in **tiles**: squares of 512 by 512 pixels. At the finest level a pixel is one block, so a tile covers 512 by 512 blocks:

$$512 \times 512 = 262\,144 \text{ pixels} \times 4 \text{ bytes} = 1\,048\,576 \text{ bytes} = 1 \text{ MiB}.$$

$$512 \text{ blocks} \times 512 \text{ blocks} = 0.262 \text{ km}^2 \text{ (a block is 1 m).}$$

A column (a chunk) is 16 by 16 blocks (`COLUMN_SIZE`, `run/include/world/column/index.h:20`), so a tile holds $32 \times 32 = 1024$ columns.

### Mip levels: zooming out by halving

To show 10 km across on a 1920-pixel screen, each pixel must stand for about 5 blocks. Reading 25 level-0 pixels for each screen pixel would be slow, and would need every level-0 tile in memory. So the Atlas keeps **zoomed-out copies**, called **mip levels**. Level 1 is level 0 halved each way: each level-1 pixel is made from a 2 by 2 square of level-0 pixels, so a level-1 tile, still 512 by 512 pixels, covers 1024 by 1024 blocks. Each level halves again.

| Level | Blocks per pixel | A tile covers (blocks) | A tile covers |
|---|---|---|---|
| 0 | 1 | 512 | 0.26 km² |
| 1 | 2 | 1024 | 1.05 km² |
| 2 | 4 | 2048 | 4.19 km² |
| 3 | 8 | 4096 | 16.8 km² |
| 4 | 16 | 8192 | 67.1 km² |
| 5 | 32 | 16 384 | 268 km² |
| 6 | 64 | 32 768 | 1074 km² |
| 7 | 128 | 65 536 | 4295 km² |

Eight levels, 0 to 7. Level 7 is nearly half the 153 km latitude pattern on one tile.

**Memory, worked out for 100 km² explored as a 10 by 10 km square.** Level 0 needs $\lceil 10\,000 / 512 \rceil = 20$ tiles each way, $20^2 = 400$ tiles, 400 MiB. Each level up needs a quarter as many: 100, 25, 9, 4, then about 1 each for levels 5 to 7. Total about 541 tiles, 541 MiB on disk before packing. In memory only a working set is kept (160 tiles, 160 MiB: see Design).

Opening the Atlas over all 100 km² on a 1920 by 1080 screen needs about 11.3 blocks per pixel, which is level 3. The 10 km square is about 3 level-3 tiles each way, so about 9 to 24 tiles, 9 to 24 MiB, are read. That is why the 100 ms target is reachable.

**How a mip pixel is made, worked by hand.** Four level-0 pixels: three generated, one not.

| Child | Red (5 bits) | Green (6 bits) | Blue (5 bits) | Height |
|---|---|---|---|---|
| A | 25 | 25 | 5 | 70 |
| B | 22 | 30 | 7 | 74 |
| C | not generated | | | |
| D | 27 | 22 | 2 | 66 |

The parent is generated, since at least one child is. Each channel is the rounded mean of the generated children, with integer rounding $(\text{sum} + \lfloor n/2 \rfloor) / n$:

$$R = \frac{25 + 22 + 27 + 1}{3} = 25, \quad G = \frac{25 + 30 + 22 + 1}{3} = 26, \quad B = \frac{5 + 7 + 2 + 1}{3} = 5, \quad z = \frac{70 + 74 + 66 + 1}{3} = 70.$$

Only when all four children are ungenerated is the parent ungenerated.

### Hill shading

A flat colour map hides mountains. **Hill shading** makes slopes facing a light brighter and slopes facing away darker, from height differences alone.

At a pixel, with $s$ blocks per pixel and heights $h$ of the four neighbouring pixels (east, west, north, south):

$$\frac{\partial z}{\partial x} = \frac{h_E - h_W}{2s}, \qquad \frac{\partial z}{\partial y} = \frac{h_N - h_S}{2s}.$$

The surface's **normal**, the unit vector straight out of the ground, is

$$\vec n = \frac{\left(-k\,\frac{\partial z}{\partial x},\; -k\,\frac{\partial z}{\partial y},\; 1\right)}{\left\lVert \left(-k\,\frac{\partial z}{\partial x},\; -k\,\frac{\partial z}{\partial y},\; 1\right) \right\rVert}$$

where $k$ is `UI_ATLAS_SHADE_EXAGGERATION`, a vertical stretch (default 1, for Ahmed to tune). The light comes from the north-west, 45° above the horizon. With $x$ east and $y$ north, that direction is

$$\vec L = (-\cos 45° \cos 45°,\; \cos 45° \cos 45°,\; \sin 45°) = (-0.5,\; 0.5,\; 0.707).$$

The **dot product** $\vec n \cdot \vec L = n_x L_x + n_y L_y + n_z L_z$ is the cosine of the angle between them: 1 when the slope faces the light, 0 when it is edge-on, negative when it faces away. Flat ground has $\vec n = (0, 0, 1)$ and $\vec n \cdot \vec L = 0.707$. The brightness factor divides by that, so flat ground is unchanged:

$$b = \operatorname{clamp}\!\left(\frac{\max(0,\ \vec n \cdot \vec L)}{0.707},\ 0.4,\ 1.3\right).$$

Each colour channel is multiplied by $b$ and clamped to 255.

**Worked example.** Level 0, $s = 1$, $k = 1$. West neighbour 70, east 74, north 72, south 72. Then $\partial z / \partial x = (74 - 70)/2 = 2$ and $\partial z / \partial y = 0$. The un-normalised normal is $(-2, 0, 1)$, of length $\sqrt{5} = 2.236$, so $\vec n = (-0.894, 0, 0.447)$.

$$\vec n \cdot \vec L = (-0.894)(-0.5) + 0 + (0.447)(0.707) = 0.447 + 0.316 = 0.763, \qquad b = 0.763 / 0.707 = 1.079.$$

Ground rising to the east faces west, toward the light: 7.9% brighter. The mirror image, rising to the west, gives $\vec n = (0.894, 0, 0.447)$, $\vec n \cdot \vec L = -0.447 + 0.316 = -0.131$, so $b$ is clamped to 0.4: 60% darker.

Ungenerated pixels are not shaded. A neighbour that is ungenerated counts as the same height as the pixel itself.

### Colour scales

A number becomes a colour by a **colour scale**: a short table of stops, each a value and a colour, with straight-line mixing between neighbouring stops. Below the first stop is the first colour; above the last, the last.

**Worked example, temperature.** Two of the stops are 20 °C = (254, 224, 139) and 30 °C = (244, 109, 67). For 25 °C, the mix fraction is $t = (25 - 20)/(30 - 20) = 0.5$, and each channel is $c = c_0 + t\,(c_1 - c_0)$, rounded:

$$R = 254 + 0.5 \times (244 - 254) = 249, \quad G = 224 + 0.5 \times (109 - 224) = 166.5 \to 167, \quad B = 139 + 0.5 \times (67 - 139) = 103.$$

Then hill shading multiplies by $b$. Rounding is "half away from zero", written once in the draw part, so every picture rounds the same.

### Sampling climate on a grid

Climate is worked out per 64-block cell, and read between cell centres with a bilinear blend after a warp of up to 32 blocks (master plan 6.9). Asking the climate functions for every one of 2 million pixels would take far too long. So the Atlas asks on a **sample grid**: every 4 screen pixels, or once per cell when a cell is smaller than 4 pixels. Between samples it blends bilinearly in screen space.

The grid is anchored to **world** pixel positions, not to the picture's corner: sample points sit at multiples of 4 in "world pixels" (block coordinate divided by blocks per pixel). So a picture panned by any amount samples the same points where the two overlap, and the same place always looks the same.

At 1920 by 1080, a 4-pixel grid is $(1920/4 + 1) \times (1080/4 + 1) = 481 \times 271 = 130\,351$ samples.

### Isobars and contours, by marching squares

An **isobar** is a line joining places of equal pressure. A **contour** is the same for height. Both are drawn by **marching squares**: look at each square of four neighbouring samples. Each corner is either above the line's value or not, which makes 4 bits and 16 cases. The case says which edges of the square the line crosses. Where it crosses an edge, the exact point is found by straight-line interpolation between that edge's two corners.

**Worked example.** One square of the 4-pixel sample grid. Corners, in hPa: top-left 1010, top-right 1013, bottom-right 1015, bottom-left 1011. Draw the 1012 isobar. Above 1012: top-right and bottom-right. So the line crosses the top edge and the bottom edge.

- Top edge, from top-left (1010) to top-right (1013): $t = (1012 - 1010)/(1013 - 1010) = 0.667$ of the way, so 2.67 pixels from the left.
- Bottom edge, from bottom-left (1011) to bottom-right (1015): $t = (1012 - 1011)/(1015 - 1011) = 0.25$, so 1 pixel from the left.

A line is drawn from $(2.67, 0)$ to $(1, 4)$ inside that square. Doing every square gives the whole isobar. Two cases (corners alternating above and below) are ambiguous. They are settled by the mean of the four corners: if the mean is above the value, the two "above" corners are joined. That rule is written once and tested.

Isobars are drawn every 4 hPa (1000, 1004, 1008, ...): a sample-grid value $p$ is compared against every multiple of 4 between the square's lowest and highest corner. Height lines are every 100 blocks.

### Arrow fields

Wind and currents are drawn as **arrows** on a grid every 32 screen pixels, anchored to world pixels like the sample grid. An arrow starts at its grid point and points the way the air or water moves (downwind, downstream). Its length shows speed. Phase 04 already set the lengths in `climate/layer`, and the Atlas keeps them:

$$\ell_{\text{wind}} = \min(2 \times \text{speed},\ 30) \text{ pixels, speed in blocks a second}, \qquad \ell_{\text{current}} = 4 + 12 \times \frac{\text{strength}}{255} \text{ pixels.}$$

The head is two short strokes either side of the tip, as phase 04 draws them.

**Worked example.** Trade wind at 6 blocks a second, direction $(-1, -0.4)$, which normalises to $(-0.928, -0.371)$. Length $2 \times 6 = 12$ pixels. Screen rows grow downward, so north is up and the screen step is $(-0.928 \times 12,\ +0.371 \times 12) = (-11.1,\ +4.5)$ pixels: left and a little down, toward the west-south-west.

Lines are drawn with Bresenham's algorithm: step one pixel at a time along the longer axis, and move one pixel on the shorter axis whenever the error passes half a pixel. No anti-aliasing.

### Forecast cones

A hurricane's future track is not certain. The outlook (master plan section 8) gives the forecast centre after $t$ days and a spread $\sigma(t) = 200 + 400\,t$ blocks. A **forecast cone** is the area where the storm is more likely than not to reach. From the outlook's formula,

$$p_i(t) = \exp\!\left(-\frac{d^2}{2(\sigma(t)^2 + R_i^2)}\right) \ge \tfrac12 \iff d \le \sqrt{2 \ln 2}\,\sqrt{\sigma(t)^2 + R_i^2} = 1.177\,\sqrt{\sigma(t)^2 + R_i^2}.$$

So the cone is the union of circles round each quarter-day forecast centre, each of that radius. It is the same 50% the HUD's storm warning uses (phase 10).

**Worked example, Hurricane Tavela** (master plan section 8), radius $R = 2500$ blocks. At $t = 2$ days, $\sigma = 1000$: $\sqrt{1000^2 + 2500^2} = 2693$, times 1.177 is 3170 blocks. At 16 blocks per pixel that circle has a radius of 198 pixels.

### Drawing on several workers, with the same bytes

The repo holds terrain and climate to one rule: the same input gives the same bytes on 1 worker or 16. The Atlas holds to it too. A picture is cut into **bands** of rows. Each band is drawn by one job: ground, shading and field colours. Every pixel's value depends only on the view and the world, never on which band it is in, so any cut gives the same bytes. Lines, arrows, marks and names cross bands, so they are drawn after the bands, by one job, in a fixed order.

### Golden-picture tests

A **golden picture** is a PNG kept in the repo of what a drawing must look like, checked once by a person. The test draws the same thing again and compares every pixel. The UI tests already do this, exact to the byte (`tests/run/ui/hud/README.md`): the blend kernel is integer arithmetic and nothing depends on the card. The Atlas's maths uses floating point, but always in the same order, so it is exact too, as terrain is. To make goldens again after a deliberate change:

    MC_GOLDEN_WRITE=1 tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_ui_atlas$'

and then someone looks at each one before committing.

### What a bench measures

A **bench** is a program under `bench/` that times something, built but not run by ctest (`cmake/targets.cmake:13`). It runs the thing many times on a release build, with nothing else using the CPU (that is why it goes through `tools/test-or-build.sh`), and prints the fastest, the median and the slowest, against a target. A miss is printed as `MISSED` and does not fail the run, as `bench/world/README.md` explains.

## Design

### Where the code goes

| Path | Tier | What |
|---|---|---|
| `run/src/ui/atlas/` (NEW) | Arena (depth 2) | `ui_atlas_t`: the store's slots and lock, the view, input, the drawing and saving jobs, and each frame's fixed parts. Gets its memory from the region `ui_t` owns |
| `run/src/ui/atlas/tile/` (NEW) | Borrow (depth 3) | Pixel packing, a column's top, writing a column into a tile, halving into a mip, and the tile file's encode and decode. Pure functions on buffers it is given |
| `run/src/ui/atlas/layer/` (NEW) | Borrow | The layer registry and every layer's draw function |
| `run/src/ui/atlas/draw/` (NEW) | Borrow | The drawing tools layers share: hill shading, colour scales, the sample grid, marching squares, lines, arrows, circles, cones, outlines, legends |

Headers mirror them under `run/include/ui/atlas/`. Each directory has one `index.c` and a `README.md` (`plans/CODING_STANDARD.md` section 5).

**The climate layers already have code.** Phase 03 made `run/src/climate/layer/`, with `climate_layer_draw(climate, layer, west_x, north_y, blocks_per_pixel, width, height, day, pixels, pitch)` and the enum `climate_layer_t`, and said "The Atlas (phase 09) and the map tools draw with this same code". Phases 04 to 07 add their layers there (phase 04: `CLIMATE_LAYER_CURRENTS`, `_PRESSURE`, `_WIND`, with their colours and arrows). So the Atlas does **not** draw those fields again. Its registry rows for climate fields call `climate_layer_draw` for their band's rectangle, then the Atlas adds what is its own: hill shading from its tiles, grey over ungenerated land, outlines over grey, and the legend. `ui/atlas/draw/` holds only what `climate/layer` does not: hill shading, marching squares if `climate/layer` has none, cones, the "outline only over grey" test, legends. Where a phase already wrote a tool here (Bresenham lines in phase 04), the Atlas calls that one, and does not write a second.

`climate_layer_draw` takes whole blocks per pixel. The Atlas's zoom steps by $\sqrt 2$ and goes below 1 block a pixel, so this phase adds `climate_layer_draw_scaled` beside it in `run/src/climate/layer/`, taking `float64_t` blocks per pixel and a world-anchored sample origin (Background, "Sampling climate on a grid"). `climate_layer_draw` becomes a call to it, and its pictures must stay the same bytes: phase 03's and 04's layer hashes hold that.

`ui` may include `climate` and `calendar`. The lint's rank table (`compile/src/standards_lint/rules_include/index.c:64`) has neither today. Phase 03 adds `{"climate", 8, 0}`, between `gpu_bridge` (7) and `terrain` (9); phase 01 adds `calendar`. If either is missing, add it here at rank 8, and say so in **Issues found**.

### The pixel

One `uint32_t` per pixel, at every level:

| Bits | What |
|---|---|
| 0 to 15 | Colour, RGB565: red in bits 11 to 15, green 5 to 10, blue 0 to 4 |
| 16 to 31 | Height of the top, as `int16_t`: z from sea level, as the HUD shows it (`ui_hud_coordinates_draw`) |

`UI_ATLAS_HEIGHT_NONE = INT16_MIN` (-32768) means **not generated**. `UI_ATLAS_PIXEL_NONE = 0x80000000u` is that height with colour 0, and every new tile is filled with it. World heights are -250 to +1250 after 003 phase 11, well inside `int16_t`.

The master plan says "colour and height". Storing the colour, not the block id, means mip pixels and level-0 pixels have one format, and the draw code has one path.

```c
/* run/include/ui/atlas/tile/index.h (NEW) */
enum : uint32_t {
    UI_ATLAS_TILE_SIDE = 512u,                                        /* pixels each way, at every level */
    UI_ATLAS_TILE_PIXELS = UI_ATLAS_TILE_SIDE * UI_ATLAS_TILE_SIDE,  /* 262144 */
    UI_ATLAS_TILE_BYTES = UI_ATLAS_TILE_PIXELS * 4u,                  /* 1 MiB */
    UI_ATLAS_LEVELS = 8u,                                            /* 0 is a block a pixel; 7 is 128 blocks */
    UI_ATLAS_COLUMN_PIXELS = 256u,                                   /* a column's 16 by 16 */
    UI_ATLAS_PIXEL_NONE = 0x80000000u,                               /* not generated */
};
static_assert(UI_ATLAS_TILE_SIDE % 16u == 0u); /* a column never straddles two tiles */

static inline uint32_t ui_atlas_pixel_pack(uint16_t rgb565, int16_t z) {
    return (uint32_t)rgb565 | ((uint32_t)(uint16_t)z << 16);
}
static inline int16_t ui_atlas_pixel_z(uint32_t pixel) { return (int16_t)(uint16_t)(pixel >> 16); }
static inline uint16_t ui_atlas_pixel_rgb565(uint32_t pixel) { return (uint16_t)(pixel & 0xFFFFu); }
static inline bool ui_atlas_pixel_generated(uint32_t pixel) { return ui_atlas_pixel_z(pixel) != INT16_MIN; }
```

**Where a block lands.** At level $L$, a tile covers $S = 512 \cdot 2^L$ blocks. Tile `(tx, ty)` covers $x \in [tx \cdot S,\ tx \cdot S + S)$ and $y \in [ty \cdot S,\ ty \cdot S + S)$, with floor division for negatives. Pixel column $i = (x - tx \cdot S) / 2^L$. Pixel row $j = (ty \cdot S + S - 1 - y) / 2^L$, so **row 0 is the north edge**, as in a picture, and north is up on screen.

### A column's top, worked out on the worker

The column job (`game_column_job_run`, `run/src/game/index.c:704`) already has the whole column's states in `borrowed_states`, sections lowest first, after the save's changes are laid over. After that, it calls the NEW `ui_atlas_column_top(colours, states, out)`, which for each of the 256 `(x, y)` walks down from the top section to the first voxel that counts, and writes its packed pixel.

A voxel **counts** if its block's shape is not `BLOCK_SHAPE_NONE` (air), `BLOCK_SHAPE_CROSS`, `BLOCK_SHAPE_CROP` or `BLOCK_SHAPE_TORCH` (the generated `tables/blocks/index.h`; names from `compile/src/game_tables/blocks/index.c:47`). So flowers, grass tufts and torches never hide the ground. Water and lava count, so the sea is the sea.

Its height is $z = 16 \cdot \text{section} + z_{\text{local}} + \texttt{COLUMN\_Z\_STORAGE\_MIN} - \texttt{MC\_WORLD\_Z\_SEA}$, the same `z` the HUD prints.

Its colour comes from `colours`, a table of `uint16_t` RGB565 by block id, built once at `ui_init` from each block's top face (`block_defs[b].faces[BLOCK_FACE_TOP]`, an index into `block_texture_names`) and that texture's `mean_rgba` (`texture_info_get`, `run/include/texture/index.h:73`; `texture_blob_entry_t.mean_rgba`, `run/include/types/texture_blob/index.h:99`). It is read-only after init, so workers may read it.

The job writes the 256 pixels into a NEW per-job array, `borrowed_atlas_pixels`, taken from `arena_world` at `game_open` beside `borrowed_opened` (`run/src/game/index.c:462`). `GAME_WORLD_ARENA_BYTES` grows by `GAME_STREAM_JOBS * UI_ATLAS_COLUMN_PIXELS * 4` (64 KiB).

```c
/* run/include/game/index.h: game_column_job_t, from _pad1 */
    uint64_t saved_sections;          /* 64: which sections the save gave at all */
    uint32_t *borrowed_atlas_pixels;  /* 72: NEW: 256, the column's top as the Atlas keeps it, row 0 north */
    uint8_t _pad1[48];                /* 80 */
} game_column_job_t;
static_assert(sizeof(game_column_job_t) == 2u * MC_CPU_CACHE_LINE_BYTES); /* unchanged, 128 */
```

### Writing it into a tile, on the sim thread

`game_column_fill` (`run/src/game/index.c:727`) runs on the sim thread when a finished column goes into the world. After it succeeds, just before `game->columns_made++`, it calls the NEW `ui_atlas_column_write(atlas, chunk_x, chunk_y, job->borrowed_atlas_pixels)`. That takes the store's lock, finds or makes the level-0 slot, copies 16 rows of 16 pixels, marks the slot dirty and its parents stale, adds one to `revision`, and lets go. About 2 µs.

So the order is: **worker** works out the top; **sim** writes it into the tile; **main** never writes tiles. A column that loads again is written again, so edits saved with the world show up the next time their column loads. Live edits to the top block are not followed in this phase (Risks).

A run with no `ui_t` (`--test-world` with no UI, benches) passes a null atlas, and the call does nothing.

### The store

```c
/* run/include/ui/atlas/index.h (NEW) */
enum : uint32_t {
    UI_ATLAS_SLOTS = 160u,        /* tiles kept in memory: 160 MiB */
    UI_ATLAS_STALE_MAX = 256u,    /* mips waiting to be made again; past this every cached mip is */
    UI_ATLAS_ZOOM_MIN = -4,       /* 2^(-4/2) = 0.25 blocks a pixel: a block is 4 pixels */
    UI_ATLAS_ZOOM_MAX = 16,       /* 2^(16/2) = 256 blocks a pixel */
    UI_ATLAS_ZOOM_DEFAULT = 2,    /* 2 blocks a pixel */
    UI_ATLAS_PICTURE_MAX_WIDTH = 3840u,
    UI_ATLAS_PICTURE_MAX_HEIGHT = 2160u,
    UI_ATLAS_BANDS = 16u,         /* a picture's rows in this many jobs */
    UI_ATLAS_SAMPLE_PIXELS = 4u,  /* the climate sample grid */
    UI_ATLAS_ARROW_PIXELS = 32u,  /* the arrow grid */
    UI_ATLAS_NOW_REDRAW_MS = 500u,      /* ms of world time between pictures of a now layer: twice a second, 9.3 */
    UI_ATLAS_GROUND_SAMPLE_MS = 500u,   /* ms between looks at the store's revision while streaming */
    UI_ATLAS_SAVE_INTERVAL_MS = 30000u, /* ms of world time between saves of dirty tiles, as save/autosave */
};
#define UI_ATLAS_SHADE_EXAGGERATION 1.0f32 /* ratio: vertical stretch for hill shading, for Ahmed to tune by eye */

typedef enum ui_atlas_slot_state_t : uint8_t {
    UI_ATLAS_SLOT_EMPTY = 0,
    UI_ATLAS_SLOT_LIVE = 1,
} ui_atlas_slot_state_t;

/* One tile in memory. Every field and every pixel only under ui_atlas_store_t.mutex. */
typedef struct ui_atlas_slot_t {
    uint32_t *borrowed_pixels; /* 0: UI_ATLAS_TILE_PIXELS, from the atlas region */
    int32_t tile_x;            /* 8 */
    int32_t tile_y;            /* 12 */
    uint64_t last_used;        /* 16: the store's use_clock when last read or written */
    uint64_t revision;         /* 24: the store's revision at its last write */
    uint8_t level;             /* 32: 0 to UI_ATLAS_LEVELS - 1 */
    uint8_t state;             /* 33: ui_atlas_slot_state_t */
    uint8_t dirty;             /* 34: changed since its file was written */
    uint8_t merge;             /* 35: made before its file was read: read it, keep what is generated here, take the rest from the file */
    uint8_t parents_marked;    /* 36: its parents are in the stale list since its last write's save */
    uint8_t _pad0[3];          /* 37 */
} ui_atlas_slot_t;
static_assert(sizeof(ui_atlas_slot_t) == 40);

/* A mip that must be made again from its children. */
typedef struct ui_atlas_tile_key_t {
    int32_t tile_x;   /* 0 */
    int32_t tile_y;   /* 4 */
    uint8_t level;    /* 8 */
    uint8_t _pad0[3]; /* 9 */
} ui_atlas_tile_key_t;
static_assert(sizeof(ui_atlas_tile_key_t) == 12);

/* What the Atlas knows of the ground: written by the sim, read by the drawing job, saved by the saving job. */
typedef struct ui_atlas_store_t {
    pthread_mutex_t mutex;               /* 0: over everything below and every slot's pixels */
    ui_atlas_slot_t *borrowed_slots;     /* 40: UI_ATLAS_SLOTS */
    ui_atlas_tile_key_t *borrowed_stale; /* 48: UI_ATLAS_STALE_MAX */
    uint32_t *borrowed_scratch;          /* 56: one tile, for the saving job, used outside the lock */
    const uint16_t *borrowed_colours;    /* 64: RGB565 by block id, from ui_t's tables */
    char *borrowed_directory;            /* 72: <world>/atlas, or nullptr: nothing is kept on disk */
    uint64_t seed;                       /* 80: the world's; a file of another seed is not read */
    uint64_t revision;                   /* 88: every column written */
    uint64_t use_clock;                  /* 96 */
    uint64_t columns_lost;               /* 104: writes dropped with every slot dirty; logged once */
    uint32_t stale_count;                /* 112 */
    uint8_t stale_overflow;              /* 116: the list filled: every cached mip is stale */
    uint8_t save_failed_said;            /* 117 */
    uint8_t _pad0[2];                    /* 118 */
} ui_atlas_store_t;
static_assert(sizeof(ui_atlas_store_t) == 120);
```

`pthread_mutex_t` is 40 bytes on this machine's glibc, as `save_world_t` already relies on (`run/include/save/world/index.h:245`). If that ever changes, the `static_assert` says so.

**Finding a slot** is a linear search over 160 entries for `(level, tile_x, tile_y)`: 160 compares, far under a microsecond. Not worth a hash.

**Making a slot** when the tile is not in memory: take an empty slot, or else the least recently used slot that is not dirty. Fill it with `UI_ATLAS_PIXEL_NONE`. If the store has a directory, set `merge`, because a file may hold older pixels. If every slot is dirty (the saving job is far behind), the write is dropped, `columns_lost` goes up, and it is logged once. The column writes itself again next time it loads.

**Lock hold times.** Writing a column: about 2 µs. Copying a tile out for drawing or saving: about 0.1 to 0.3 ms. Making one mip quarter from one child: about 0.3 ms. The sim never waits more than about 1 ms, inside its 4 ms commit budget (`GAME_COMMIT_BUDGET_NS`, `run/src/game/index.c:34`). A test measures the longest hold.

### Mips

When a level-0 slot is written and `parents_marked` is 0, its seven ancestors (level $L$ tile = floor of the child tile over $2^L$) go into the stale list, and `parents_marked` is set. If the list is full, `stale_overflow` is set.

A mip is made again, from its four children, by whoever needs it first:

- the **drawing job**, before it reads a stale mip;
- the **saving job**, after it writes level-0 tiles, bottom-up, for every stale entry.

Making one: lock, for each child that is in memory or on disk (loaded into a slot, merging if needed), halve its 512 by 512 into the parent's matching 256 by 256 quarter (the rule in Background), unlock between children. A child that exists nowhere leaves its quarter `UI_ATLAS_PIXEL_NONE`. The parent becomes dirty, and is written by the saving job like any tile.

### Saving: the file

One file per tile: `<world>/atlas/L<level>_<tile_x>_<tile_y>.tile`, signed decimal, for example `atlas/L3_-1_2.tile`. The world opens fine with no `atlas/` directory: the Atlas simply starts empty and fills as columns load. So no `SAVE_WORLD_VERSION` bump is needed, and old worlds keep working. All little-endian, as `save/world` files are.

```c
/* run/include/ui/atlas/tile/index.h (NEW) */
enum : uint32_t {
    UI_ATLAS_FILE_VERSION = 1u,
    UI_ATLAS_ENCODING_RAW = 0u,  /* UI_ATLAS_TILE_PIXELS uint32_t, row 0 first */
    UI_ATLAS_ENCODING_RUNS = 1u, /* ui_atlas_run_t, until all UI_ATLAS_TILE_PIXELS are covered */
};

/* At the start of every .tile file. */
typedef struct ui_atlas_file_header_t {
    uint8_t magic[8];          /* 0: "MCATLAST" */
    uint64_t seed;             /* 8: the world's */
    uint64_t payload_checksum; /* 16: texture_blob_hash (FNV-1a 64) of what follows the header */
    int32_t tile_x;            /* 24 */
    int32_t tile_y;            /* 28 */
    uint32_t version;          /* 32: UI_ATLAS_FILE_VERSION */
    uint32_t header_bytes;     /* 36: sizeof this, 64 */
    uint32_t payload_bytes;    /* 40 */
    uint8_t level;             /* 44 */
    uint8_t encoding;          /* 45: UI_ATLAS_ENCODING_* */
    uint8_t _pad0[2];          /* 46 */
    uint64_t checksum;         /* 48: texture_blob_hash of the 48 bytes before this */
    uint8_t _pad1[8];          /* 56 */
} ui_atlas_file_header_t;
static_assert(sizeof(ui_atlas_file_header_t) == 64);
static_assert(alignof(ui_atlas_file_header_t) == 8);
static_assert(offsetof(ui_atlas_file_header_t, magic) == 0);
static_assert(offsetof(ui_atlas_file_header_t, seed) == 8);
static_assert(offsetof(ui_atlas_file_header_t, payload_checksum) == 16);
static_assert(offsetof(ui_atlas_file_header_t, tile_x) == 24);
static_assert(offsetof(ui_atlas_file_header_t, tile_y) == 28);
static_assert(offsetof(ui_atlas_file_header_t, version) == 32);
static_assert(offsetof(ui_atlas_file_header_t, header_bytes) == 36);
static_assert(offsetof(ui_atlas_file_header_t, payload_bytes) == 40);
static_assert(offsetof(ui_atlas_file_header_t, level) == 44);
static_assert(offsetof(ui_atlas_file_header_t, encoding) == 45);
static_assert(offsetof(ui_atlas_file_header_t, _pad0) == 46);
static_assert(offsetof(ui_atlas_file_header_t, checksum) == 48);
static_assert(offsetof(ui_atlas_file_header_t, _pad1) == 56);

/* A run of one pixel value, in row order. */
typedef struct ui_atlas_run_t {
    uint32_t pixel; /* 0 */
    uint32_t count; /* 4: at least 1 */
} ui_atlas_run_t;
static_assert(sizeof(ui_atlas_run_t) == 8);
```

The encoder writes runs if they are smaller than raw, else raw. Sea and ungenerated areas are long runs; varied land is not. `texture_blob_hash` is FNV-1a 64, public in `run/include/texture/blob/index.h:35`, so no new hash is written.

A file is **ignored** (and logged once per open) if its magic, version, header size, seed, level, tile position, checksums or run total are wrong. The tile is then treated as never saved. It is never deleted: a person may want to look at it.

**When files are written.** The sim's step calls the NEW `ui_atlas_save_step(atlas, job, world_ns)`. Every 30 s of world time, the same interval as `save/autosave`, if no saving job is running, it submits one. That job, on a worker: for each dirty slot, lock, merge it from its file if `merge` is set, copy it to `borrowed_scratch`, clear `dirty` and `parents_marked`, unlock, encode, and `file_write_atomic` (`run/include/file/index.h:69`). Then it makes every stale mip and writes those the same way. A failed write sets `dirty` again, is logged once (`save_failed_said`), and is tried at the next interval. `ui_atlas_world_close` waits for both jobs and writes everything left, on the calling thread.

### Memory

`ui_t` gets two fields from its `_pad0[48]` (`run/include/ui/index.h:35`), and stays 1152 bytes:

```c
    ui_screen_settings_t settings_screen; /* 1008 */
    ui_atlas_t *borrowed_atlas;           /* 1104: NEW: from arena_tables; the Atlas, whose tiles live in owned_atlas */
    heap_region_t owned_atlas;            /* 1112: NEW: reserved when a world opens, released when it closes */
    uint8_t _pad0[24];                    /* 1128 */
} ui_t;
static_assert(sizeof(ui_t) == 1152);
```

`ui_atlas_t` itself (below) comes from `arena_tables` at `ui_init`, like `borrowed_screens`. `ui_init` also builds the colour table there: `BLOCK_COUNT` `uint16_t`.

`owned_atlas` is reserved by the NEW `ui_atlas_world_open`, written in `run/src/ui/index.c` (the Owner, depth 1), with `heap_reserve`. It puts an `arena_t` over the region and hands that to the part's NEW `ui_atlas_store_start` in `run/src/ui/atlas/`, which takes the slots' pixels, the pictures and the scratch from it. `ui_atlas_world_close` in `run/src/ui/index.c` stops the part and releases the region. So the tier rule holds: only depth 1 touches the heap. Its size:

| Part | Bytes |
|---|---|
| 160 tiles | 160 MiB |
| 160 slots, 256 stale keys | 6.4 KiB + 3 KiB |
| Saving scratch, one tile | 1 MiB |
| Drawing: one tile copy per band, 16 | 16 MiB |
| Two pictures at the canvas size, at most 3840 by 2160 | at most 63.3 MiB (15.8 MiB at 1920 by 1080) |
| Sample grid and band scratch | under 4 MiB |

So about 200 MiB at 1920 by 1080, inside `memory.process_budget_mib` (16384, `config/machine.yaml`). A canvas bigger than 3840 by 2160 does not open the Atlas; a notice says "The Atlas needs a screen of 3840 by 2160 or less". Nothing is taken from the heap after `ui_atlas_world_open`: a test holds that.

### The Atlas itself

```c
/* What one picture is drawn from. Two views that are the same bytes give the same picture. */
typedef struct ui_atlas_view_t {
    float64_t center[2];      /* 0: block x and y at the picture's middle */
    int32_t zoom;             /* 16: blocks a pixel is 2^(zoom/2) */
    uint32_t width;           /* 20 */
    uint32_t height;          /* 24 */
    uint16_t day;             /* 28: 0 to 364; only normals layers read it, others keep 0 */
    uint8_t layer;            /* 30: a row of ui_atlas_layers */
    uint8_t _pad0[1];         /* 31 */
    uint64_t now_bucket;      /* 32: now layers: which half second of world time; others 0 */
    uint64_t week;            /* 40: this week's layers: the census week; others 0 */
    uint64_t ground_revision; /* 48: the store's revision, sampled at most twice a second */
} ui_atlas_view_t;
static_assert(sizeof(ui_atlas_view_t) == 56);

/* The main thread's, but for what each field says. */
typedef struct ui_atlas_t {
    job_group_t draw_group;           /* 0: the drawing job and its bands */
    job_group_t save_group;           /* 64: the saving job */
    ui_atlas_store_t store;           /* 128 */
    uint32_t *borrowed_pictures[2];   /* 248: front (shown) and back (being drawn) */
    ui_atlas_view_t shown;            /* 264: what the front picture was drawn from */
    ui_atlas_view_t wanted;           /* 320: what main wants now */
    ui_atlas_view_t drawing;          /* 376: what the job under way draws; the job's until draw_done */
    _Atomic uint32_t draw_done;       /* 432: worker to main, release; main reads with acquire */
    _Atomic uint32_t save_done;       /* 436: the same, for the saving job */
    uint32_t picture_width;           /* 440 */
    uint32_t picture_height;          /* 444 */
    int64_t ground_sampled_ns;        /* 448: main's: when ground_revision was last taken */
    int64_t save_due_ns;              /* 456: the sim's: world time of the next save */
    int32_t drag_from[2];             /* 464: the pointer when the button went down */
    float64_t drag_center[2];         /* 472: the centre then */
    uint8_t up;                       /* 488: on the screen */
    uint8_t drawing_busy;             /* 489: a drawing job is out */
    uint8_t saving_busy;              /* 490: a saving job is out */
    uint8_t dragging;                 /* 491 */
    uint8_t has_shown;                /* 492: the front picture holds something */
    uint8_t opened_once;              /* 493: this world has opened it, so the view is remembered */
    uint8_t _pad0[18];                /* 494 */
} ui_atlas_t;
static_assert(sizeof(ui_atlas_t) == 512);
static_assert(alignof(ui_atlas_t) == 64);
```

The offsets are what the `static_assert`s must prove. If a type from an earlier phase changes a size, recompute them and keep the asserts.

### The layer registry

```c
/* run/include/ui/atlas/layer/index.h (NEW) */
typedef enum ui_atlas_time_t : uint8_t {
    UI_ATLAS_TIME_FIXED = 0,   /* changes only with the ground: terrain, yearly means, classes */
    UI_ATLAS_TIME_NORMALS = 1, /* a day of the year, from the slider and [ ] */
    UI_ATLAS_TIME_NOW = 2,     /* the world's time: drawn again twice a second */
    UI_ATLAS_TIME_WEEK = 3,    /* the last census's week: drawn again when a census runs */
} ui_atlas_time_t;

typedef struct ui_atlas_draw_input_t ui_atlas_draw_input_t; /* below */

/* Draws rows [band.y, band.y + band.height) of picture, every column. The same pixels whatever the band. */
typedef void ui_atlas_band_draw_t(const ui_atlas_draw_input_t *input, uint32_t *picture, ui_rect_t band, void *scratch);

/* Draws lines, arrows, marks and names over the whole picture, after every band. One job, a fixed order. */
typedef void ui_atlas_overlay_draw_t(const ui_atlas_draw_input_t *input, ui_canvas_t *picture);

/* A colour key: values and colours, straight-line mixing between. */
typedef struct ui_atlas_scale_t {
    const float32_t *values;  /* 0: rising */
    const uint32_t *colours;  /* 8: 0xFFRRGGBB */
    uint32_t count;           /* 16 */
    uint8_t _pad0[4];         /* 20 */
} ui_atlas_scale_t;
static_assert(sizeof(ui_atlas_scale_t) == 24);

/* One row of a list legend, such as a climate class. */
typedef struct ui_atlas_key_entry_t {
    const char *label; /* 0 */
    uint32_t colour;   /* 8 */
    uint8_t _pad0[4];  /* 12 */
} ui_atlas_key_entry_t;
static_assert(sizeof(ui_atlas_key_entry_t) == 16);

/* One layer. A later plan adds a layer by adding a row and its functions, and changes nothing else. */
typedef struct ui_atlas_layer_t {
    const char *key;                     /* 0: "temperature_day": stable; tests, golden names and the map tool's flags use it */
    const char *label;                   /* 8: the legend's title */
    const char *unit;                    /* 16: "°C", or "" */
    ui_atlas_band_draw_t *band;          /* 24: ground and field colours */
    ui_atlas_overlay_draw_t *overlay;    /* 32: or nullptr */
    const ui_atlas_scale_t *scale;       /* 40: or nullptr for a list legend or none */
    const ui_atlas_key_entry_t *entries; /* 48: or nullptr */
    uint8_t time;                        /* 56: ui_atlas_time_t */
    uint8_t group;                       /* 57: its number key, 1 to 9 */
    uint8_t over_grey;                   /* 58: 1: drawn as outlines over ungenerated land; 0: not drawn there */
    uint8_t entry_count;                 /* 59 */
    uint8_t _pad0[4];                    /* 60 */
} ui_atlas_layer_t;
static_assert(sizeof(ui_atlas_layer_t) == 64);

extern const ui_atlas_layer_t ui_atlas_layers[UI_ATLAS_LAYER_COUNT];
```

The master plan says "`1` to `9` choose layers", and lists more than nine. So each number key is a **group**. Pressing a group's key shows its first layer; pressing it again shows the next in that group, round. The table's order is the cycling order.

| # | Key | Group | Time | Over grey | What it draws | Source |
|---|---|---|---|---|---|---|
| 0 | `terrain` | 1 | fixed | no | Top colour with hill shading | Atlas tiles |
| 1 | `terrain_lines` | 1 | fixed | no | The same, with height lines every 100 blocks | Atlas tiles |
| 2 | `temperature_day` | 2 | normals | no | Normal daily mean, °C | 6.6, phase 05 |
| 3 | `temperature_mean` | 2 | fixed | no | Yearly mean, °C | 6.6 |
| 4 | `temperature_warmest` | 2 | fixed | no | Warmest month mean, °C | 6.10 |
| 5 | `temperature_coldest` | 2 | fixed | no | Coldest month mean, °C | 6.10 |
| 6 | `temperature_now` | 2 | now | no | Today's temperature with weather, °C | 7.4, phase 07 |
| 7 | `rain_day` | 3 | normals | no | Normal rain for that week, mm | 6.7, 6.9 |
| 8 | `rain_year` | 3 | fixed | no | Yearly rain, mm | 6.7 |
| 9 | `rain_radar` | 3 | now | yes | Rain rate now, mm an hour | 7.3, 7.4 |
| 10 | `rain_last_week` | 3 | week | no | Last week's rain and snow, mm of water | 7.5 |
| 11 | `wind_day` | 4 | normals | no | Normal wind: arrows over a speed colour | 6.5 |
| 12 | `wind_now` | 4 | now | no | Wind now, with systems: arrows over speed | 6.5, 7.3 |
| 13 | `currents` | 5 | fixed | no | Ocean currents: arrows, red warm, blue cold, grey neutral | 6.3 |
| 14 | `pressure_day` | 6 | normals | no | Belts and heat lows: colour and isobars every 4 hPa | 6.4 |
| 15 | `pressure_now` | 6 | now | yes | Pressure now: isobars every 4 hPa, H and L | 6.4, 7.3 |
| 16 | `systems` | 7 | now | yes | Hurricanes with names, tracks and cones; lows with cold fronts; highs | 7.2, 7.3, 8 |
| 17 | `thunderstorm_cells` | 7 | now | yes | Thunderstorm cells, with lightning marks | 7.2.1 |
| 18 | `hurricane_season` | 7 | normals | no | $s_{\text{hur}}$ on that day, 0 to 1 | 6.8 |
| 19 | `monsoon` | 7 | normals | no | Monsoon cells, stronger while in season | 6.8 |
| 20 | `climate_class` | 8 | fixed | no | Climate class, 11 colours | 6.10 |
| 21 | `snow_balance` | 8 | fixed | no | Snow balance height; ground above it white | 6.9 |
| 22 | `latitude_lines` | 8 | fixed | no | Equator, tropics, polar circles, and every 10° | 5.1 |
| 23 | `snow` | 9 | week | no | Snow on the ground, mm of water | 7.5 |
| 24 | `soil_water` | 9 | week | no | Soil water, 0 to 150 mm | 7.5 |
| 25 | `flood` | 9 | week | no | Flood index; over 1 is over the banks | 7.5 |

`UI_ATLAS_LAYER_COUNT = 26`. `currents` is fixed, not normals: 6.3 does not change with the day, so redrawing it per day would draw the same picture. The default layer for a new world is `terrain`.

**How a field layer looks.** The field's colour is multiplied by the hill-shading factor $b$ of the ground under it, so mountains still show through. Land-only fields (soil water, snow, flood, snow balance) draw the plain terrain over sea. `rain_radar` draws terrain where rain is under 0.1 mm an hour.

**Colour scales.** A layer `climate/layer` already draws keeps the colours its phase chose, which its layer hashes pin: for example phase 03's sea distance, continentality and water, and phase 04's currents (red `0xE03030`, blue `0x3060E0`, grey `0x909090`), pressure (990 hPa `0x3050C0`, 1013 `0xF0F0F0`, 1030 `0xC04030`) and wind (0 `0x202040` to 15 blocks a second `0xF0E040`). The Atlas reads those scales for its legend, so phase 03 to 07 layers must expose their stops (a `ui_atlas_scale_t`-shaped table in `climate/layer`, added there by this phase if missing). The scales below are used **only** for a layer no earlier phase drew. Each lives in `run/src/ui/atlas/layer/` as a `ui_atlas_scale_t`, with its unit in a comment:

| Scale | Stops |
|---|---|
| Temperature, °C | -40 (59, 15, 112), -20 (44, 123, 182), -5 (171, 217, 233), 0 (247, 247, 247), 10 (166, 217, 106), 20 (254, 224, 139), 30 (244, 109, 67), 40 (165, 0, 38), 50 (90, 0, 20) |
| Yearly rain, mm | 0 (140, 81, 10), 250 (216, 179, 101), 500 (246, 232, 195), 1000 (199, 234, 229), 2000 (90, 180, 172), 3000 (1, 102, 94) |
| Weekly rain, mm | the yearly stops divided by 52 |
| Rain rate, mm an hour | 0.1 (190, 230, 255), 1 (90, 200, 90), 4 (250, 230, 60), 10 (250, 150, 30), 20 (220, 30, 30), 40 (200, 0, 200) |
| Wind speed, blocks a second | 0 (240, 240, 250), 5 (180, 200, 240), 10 (110, 140, 220), 20 (90, 60, 180), 40 (50, 0, 90) |
| Pressure, hPa | 980 (49, 54, 149), 1000 (116, 173, 209), 1013 (247, 247, 247), 1025 (244, 165, 130), 1040 (165, 0, 38) |
| $s_{\text{hur}}$, 0 to 1 | 0 (255, 255, 255), 0.25 (254, 217, 118), 1 (189, 0, 38) |
| Snow, mm of water | 0 (255, 255, 255, terrain shows), 20 (220, 235, 255), 100 (150, 190, 240), 400 (60, 90, 200) |
| Soil water, mm | 0 (166, 97, 26), 75 (223, 194, 125), 150 (1, 133, 113) |
| Flood index | 0 (terrain), 0.5 (254, 224, 139), 1 (244, 109, 67), 2 (165, 0, 38) |
| Snow balance height, blocks | 0 (255, 255, 204), 400 (161, 218, 180), 800 (65, 182, 196), 1250 (37, 52, 148) |

Climate classes, in 6.10's order: Ice (240, 240, 255), Tundra (180, 200, 210), Desert (245, 215, 140), Steppe (215, 180, 100), Rainforest (0, 100, 30), Monsoon (40, 140, 60), Savanna (170, 200, 80), Mediterranean (230, 230, 60), Temperate (100, 190, 100), Continental (90, 150, 200), Boreal (60, 90, 140).

Currents: phase 04's colours, above. Ungenerated land: `UI_ATLAS_COLOUR_UNGENERATED = 0xFF808080`, unshaded: the same grey phase 03's `climate_layer_draw` uses where a tile is not built, so the two never look different.

**The systems layer, in detail.** Over the hill-shaded terrain:

- **Hurricanes:** the footprint radius $R_i$ as a circle outline, the eye as a filled dark circle of the eye radius, and a spiral symbol of two arcs. The name beside it, as phase 06 makes it ("Hurricane Tavela", "Tropical storm Orin"). The past track, positions every quarter day since it formed, as a dotted line. The forecast cone ahead, from phase 07's forecast track, to the end of its life or 7 days, whichever is first, as an outline and a 25% red fill.
- **Lows:** "L" in red at the centre, scale 2. The cold front as a 2-pixel blue line along phase 06's front, with a small triangle every 24 pixels on the side it moves toward.
- **Highs:** "H" in blue at the centre, scale 2, and a faint outline at $R_i$.

**Over grey land, outlines only** (master plan 2.4). For a layer with `over_grey = 1`, a pixel over ungenerated land is drawn only if it is on an outline: a footprint circle, a track line, a cone's edge, a rain-radar area's edge (the 1 mm an hour contour), a cell's circle, or a name. Fills, field colours and arrows are never drawn over grey. Names are drawn over grey, so an incoming storm can be read before it arrives.

**Thunderstorm cells** are worked out only where someone looks (master plan 7.2.1). They are drawn only at 16 blocks a pixel or closer; further out the legend says "Zoom in to see cells". Each cell is a filled circle of its radius, coloured by its rain rate on the rain-rate scale, with a 3-segment yellow zig-zag lightning mark at its centre.

**Systems for the view.** The drawing job asks phase 06 for the systems that touch the picture's rectangle. If phase 06 only gives "the systems near a point", the job asks at points every 32 km across the rectangle plus 35 km (the longest track, 7.3), and keeps each system once, by its id. At 256 blocks a pixel on 1920 by 1080 that is a 491 by 276 km view, about 20 by 12 points: still well under a millisecond each.

### Drawing a picture

`ui_atlas_draw_input_t` holds everything one picture reads, as plain values and borrowed handles:

```c
typedef struct ui_atlas_ground_t {
    void *borrowed_context;
    /* Copies level's pixels for the rectangle of its pixels from (px, py), width by height, into out, UI_ATLAS_PIXEL_NONE where
     * nothing is known. Row 0 north. Any thread; locks what it needs. */
    void (*read)(void *borrowed_context, uint32_t level, int64_t px, int64_t py, uint32_t width, uint32_t height, uint32_t *out);
} ui_atlas_ground_t;
static_assert(sizeof(ui_atlas_ground_t) == 16);

struct ui_atlas_draw_input_t {
    ui_atlas_view_t view;               /* 0 */
    ui_atlas_ground_t ground;           /* 56: the store in the game, terrain in the map tool and tests */
    const climate_t *borrowed_climate;  /* 72: phase 03's handle (use its real name) */
    const climate_week_t *borrowed_week_state; /* 80: phase 07's saved weekly state lookup, or nullptr (use its real accessor) */
    const ui_font_t *borrowed_font;     /* 88 */
    uint64_t seed;                      /* 96 */
    int64_t world_ticks;                /* 104: for now layers */
    uint32_t level;                     /* 112: the mip level the zoom reads */
    uint8_t _pad0[4];                   /* 116 */
};
static_assert(sizeof(ui_atlas_draw_input_t) == 120);
```

The climate types named here (`climate_t`, `climate_week_t`) are from phases 03 and 07: use the names those phases gave. Everything the layers ask of the climate is through public pure functions of those phases. This phase adds none.

**Zoom.** Blocks a pixel $s = 2^{\text{zoom}/2}$, so each wheel detent is a factor of $\sqrt 2$. Level $L = \operatorname{clamp}(\lfloor \log_2 s \rfloor, 0, 7)$. Each picture pixel takes the level-$L$ pixel under its centre (nearest). Zoomed in past 1 block a pixel, level 0 is magnified.

**The world pixel of a picture pixel.** With centre $(c_x, c_y)$ in blocks, picture size $W \times H$, picture pixel $(i, j)$ is at block

$$x = c_x + (i + 0.5 - W/2)\,s, \qquad y = c_y - (j + 0.5 - H/2)\,s.$$

**Steps of one picture**, on workers:

1. The drawing job (one job, submitted by main with `job_submit`, as main is not a worker) spawns `UI_ATLAS_BANDS` band jobs with `job_spawn` into `draw_group`.
2. Each band reads the ground it needs, one row more each side for shading, through `ground.read`, into its scratch. It works out the sample grid points that fall in its rows, plus one sample row each side, so blending at band edges uses the same samples as the next band. It writes each pixel: grey if ungenerated, else the layer's colour times $b$.
3. When the bands are done, the drawing job runs the layer's overlay over the whole picture, through a `ui_canvas_t` started on the back picture (`ui_canvas_start`), so text and clipping work as everywhere else.
4. It stores `draw_done = 1` with release. Main sees it with acquire, swaps front and back, and copies `drawing` into `shown`.

### Redraw policy

Main works out `wanted` each frame. If no job is out and `wanted` differs from `shown`, main copies `wanted` into `drawing` and submits the job.

| Change | Effect |
|---|---|
| Layer, zoom, picture size | `wanted` changes: one new picture |
| Pan (drag, `P`) | `wanted.center` changes. While a job is out, the front picture is shown shifted by the pan since it was drawn, with the uncovered edge dark (`0xFF101010`) until the new picture lands |
| Day, on a normals layer | `wanted.day` changes. Other layers keep `day = 0`, so the day does not redraw them |
| World time, on a now layer | `wanted.now_bucket = world_ns / 500 ms`: twice a second |
| Census, on a week layer | `wanted.week` changes once a week |
| Ground written | `wanted.ground_revision` takes the store's `revision` at most every 500 ms (`ground_sampled_ns`), so streaming redraws at most twice a second |

So a still view of a normals layer is drawn **once**, and then only copied. A now layer is drawn twice a second.

### Cost budget

| Where | What | Budget |
|---|---|---|
| Main, each frame, Atlas up | Swap check, copying the picture onto the canvas, title, legend, slider, readout, player mark | 2 ms at 1920 by 1080 |
| Main, each frame, Atlas closed | Nothing but `ui_atlas_save_step`'s time check on the sim | 0 |
| Sim, each column | `ui_atlas_column_write` | 5 µs |
| Worker, column job | `ui_atlas_column_top` | 20 µs |
| Workers, one picture | Bands and overlay, worst layer, 1920 by 1080, warm climate tiles | 60 ms wall on 16 workers |
| Workers, now layers | One picture every 500 ms | the same |
| Opening over 100 km² | `M` to the first picture complete, files in the page cache | 100 ms |

`bench_atlas` measures each of these.

### Input and keys

The Atlas is like a game screen: while it is up, it has the input, and the body stands. The world goes on: the sim keeps ticking, mobs move, storms move, columns stream in. `game_t.paused` is never set by the Atlas.

`ui_atlas_input(atlas, frame, width, height)` returns true if the Atlas is up after this frame. It runs in `game_loop_input_frame` (`run/src/game/input/index.c:92`) and in the headless scene's tick (`run/src/game/index.c:1066`), **before** `game_screen_input`, and only if no game screen is up. If it returns true, the body is sent no movement, exactly as with a screen up.

| Key | While playing, nothing else up | While the Atlas is up |
|---|---|---|
| `M` | Opens it | Closes it |
| `Esc` | (the game's own: pause screen, or end a test run) | Closes it |
| Wheel | (hotbar) | Zooms about the pointer: the block under the pointer stays under it. Away from the player zooms in, `zoom -= detents`, held to `UI_ATLAS_ZOOM_MIN` to `_MAX` |
| Left button, drag | (mine) | Pans: the map follows the pointer exactly, `center = drag_center - (pointer - drag_from) * s`, with y flipped |
| `1` to `9` | (hotbar) | Chooses that group's first layer, or the next in the group if already in it |
| `[` and `]` | | Day -1 and +1, round from 0 to 364 and back, on normals layers. With Shift, -30 and +30 |
| `P` | | Centres on the player |
| Day slider | | `ui_widget_slider` along the bottom, on normals layers: drag to any day |
| `F3` | Overlay | Overlay, drawn over the Atlas |
| Anything else | | Nothing |

The first open in a world centres on the player at `UI_ATLAS_ZOOM_DEFAULT`, on `terrain`, on today's day of the year. After that the view is remembered until the world closes. Opening when `M` is pressed while the inventory or a block's screen is up does nothing. `E` does nothing while the Atlas is up.

**Esc routing.** Today Esc ends a test-world run when no screen is up (`game_loop_source`, `run/src/game/index.c:1368`), and opens the pause screen in a played world unless a screen is up (`run/src/game/session/index.c:442` and `:554`). Both ask `game_screen_up`. So `game_screen_up` (`run/src/game/index.c:1357`) must return true while the Atlas is up, and Esc then reaches the Atlas, which closes.

**The census screen** (phases 01 and 07) closes the Atlas when it opens. The view is remembered; `M` opens it again.

### Each frame's fixed parts

Drawn by main over the copied picture, every frame, through the canvas, at the debug scale the coordinates use (`ui_hud_debug_scale`):

- **Title**, top left on a dark panel: `Atlas: Temperature, day 214`, or `Atlas: Rain radar, now`, or `Atlas: Snow, last week`. Then `1-9 layers, [ ] day, P you, M close`.
- **Legend**, bottom right: the layer's label and unit, its colour scale as a bar with 5 values written under it, or its list of entries. For arrow layers, an arrow of 10 blocks a second for length.
- **Day slider**, bottom middle, normals layers only: `Day 214 of 365`.
- **Readout**, bottom left, for the block under the pointer: `45.0°N  x 1234 y 19149  z 72` then `11.2 °C  812 mm a year  wind 6.1 from SW  1013 hPa  current warm  Temperate`. Values follow the layer's time mode: normals for the slider's day, now values on now layers. Over grey land: `45.0°N  x 1234 y 19149  not explored`. Latitude is $\varphi(y)$ (phase 02). Height is the level-0 pixel, read through the store under its lock (one pixel).
- **The player**: a white triangle with a dark edge at the player's block, pointing where they look, drawn every frame so it moves smoothly.

Readout values are recomputed only when the pointer's block or the view changes: a few climate calls, well under 0.1 ms.

### The degree sign

The plan writes °C and 23.5°, and phase 10's HUD line writes "Year 2 · Day 214". The font has only printable ASCII: anything past it is drawn as `?` (`run/include/ui/font/index.h:63`, `run/src/ui/font/README.md`). Its atlas is 128 by 48, 16 cells by 6 rows, and only the last cell (DEL) is empty, which is room for one extra character, and two are needed.

So this phase adds a **seventh row** to `textures/ui/font/` (128 by 56, `UI_FONT_ATLAS_HEIGHT = 56u`) for characters past ASCII. Its first two cells are `°` (U+00B0, bytes `0xC2 0xB0`: a 3 by 3 ring at the top of the cell) and `·` (U+00B7, bytes `0xC2 0xB7`: one dot at mid height). The other 14 cells stay empty for later. `ui_font_load` accepts the new size, `ui_font_glyph` stays for ASCII, and `ui_font_measure` and `ui_font_draw` map those two byte pairs to their cells as one character each. Every other character is unchanged, so every existing golden stays the same, and a test says so. If phase 01 or another earlier phase of this plan already did this, skip it and say so here.

### The headless map tool

The master plan names 003 phase 8 stage 1's `bench/world_map/` as the tool every phase draws with. On 2026-11-04 it did not exist, so phases 03 and 04 draw their layers with `bench_climate --pictures <directory> <seed>` (`bench/climate/`), through `climate_layer_draw`. This phase makes **whichever of the two exists** (both, if both do) draw every Atlas layer through `ui_atlas_draw_picture`, with a `ui_atlas_ground_t` that reads the terrain: everything counts as generated, so the tool's pictures have no grey. It takes `--layer <key>` (any registry key, or `all`) and `--zoom <n>`. Phase 03 to 07 layers drawn this way must come out the same bytes as their own `--pictures` did before, at a whole number of blocks a pixel and with hill shading off (`--flat`): that is what proves the Atlas draws the same code.

## Owned paths

Edit only these, and this file:

- `run/src/ui/atlas/`, `run/src/ui/atlas/tile/`, `run/src/ui/atlas/layer/`, `run/src/ui/atlas/draw/` (all NEW), and `run/include/ui/atlas/` with the matching headers
- `tests/run/ui/atlas/` (NEW: ctest `run_ui_atlas`), with `fixtures/` for its golden pictures and scripts
- `bench/atlas/` (NEW: `bench_atlas`)
- `bench/world_map/` (if it exists) and `bench/climate/`: only to draw every layer through the registry, with `--layer`, `--zoom` and `--flat`
- `run/src/climate/layer/`, `run/include/climate/layer/`, `tests/run/climate/layer/`: only `climate_layer_draw_scaled`, `climate_layer_draw` built on it with unchanged bytes, and each layer's scale stops made readable for the legend
- Wiring, each listed in **Issues found** when done, as 001 phase 14 stage 2 did:
  - `run/include/ui/index.h`, `run/src/ui/index.c`, `run/src/ui/README.md`: the two `ui_t` fields, init of the Atlas and colour table, `ui_atlas_world_open` and `_close`
  - `run/include/game/index.h`, `run/src/game/index.c`, `run/src/game/input/index.c`, `run/src/game/README.md`: the column job's pixels, the column write, input routing, `game_screen_up`, drawing the Atlas in `game_hud_compose`, the save step
  - `run/src/game/session/index.c`: opening and closing the Atlas's store with the world, with its directory
  - `textures/ui/font/index.yaml`, `textures/ui/font/README.md`, `run/include/ui/font/index.h`, `run/src/ui/font/index.c`, `run/src/ui/font/README.md`, `tests/run/ui/font/`: the seventh font row with `°` and `·`
  - `compile/src/standards_lint/rules_include/index.c`: only if `climate` and `calendar` have no rank yet
  - `plans/REPO_STRUCTURE.md`: the UI table, the tests table and `bench/`

## Steps

Build in this order. Each step names its files and why.

1. **Read first.** The vision file, master plan 2.4 and 9.3, `run/src/ui/README.md`, `run/src/ui/hud/README.md`, `run/src/game/README.md`, `tests/run/ui/hud/README.md`, and the phase files of phases 03 to 07 for the exact names of their public functions and types. Write the names you will call in this file's **Issues found** if any differ from what this file assumed.

2. **The degree sign and the middle dot.** `textures/ui/font/index.yaml`: a seventh row, 128 by 56, with `°` and `·` in its first two cells. `run/include/ui/font/index.h`: `UI_FONT_ATLAS_HEIGHT = 56u`, and `UI_FONT_EXTRA = 2u` for the characters past ASCII. `run/src/ui/font/index.c`: accept the size, map `0xC2 0xB0` and `0xC2 0xB7` in `ui_font_measure` and `ui_font_draw`. A new test in `tests/run/ui/font/` draws every ASCII character before and after and finds them the same, and draws the two new ones by hand. Why first: legends and the readout need `°`, phase 10 needs `·`, and if this breaks a font or texture test, that is found before anything else is built. Run `run_ui_font`, `run_texture`, `compile_texture_vector` and `run_ui_hud` (whose goldens must not change).

3. **Pixels and a column's top.** `run/include/ui/atlas/tile/index.h` and `run/src/ui/atlas/tile/index.c`: the constants, pack and unpack, `ui_atlas_column_top`, and `ui_atlas_tile_column_put` (copy a column's 256 pixels into a 512-square tile at its place). Pure functions, so they are tested on their own first.

4. **Halving.** In `tile/`: `ui_atlas_tile_halve(child, quarter, parent)`, by the rule in Background. Test it on the worked example.

5. **Tile files.** In `tile/`: `ui_atlas_tile_encode` and `ui_atlas_tile_decode`, with the header and both encodings, refusing every bad field. No disk here: buffers in, buffers out.

6. **The store.** `run/include/ui/atlas/index.h`, `run/src/ui/atlas/index.c`: `ui_atlas_store_t`, slots, the lock, `ui_atlas_column_write`, slot finding and making, merge, the stale list, and `ui_atlas_store_read` (the game's `ui_atlas_ground_t.read`). Then `ui_atlas_save_step`, the saving job, and making mips, with `file_write_atomic` and `file_read_whole`.

7. **Memory and lifetime.** `run/include/ui/index.h`, `run/src/ui/index.c`: the two fields, `ui_atlas_t` and the colour table from `arena_tables` at `ui_init`, and `ui_atlas_world_open(ui, width, height, directory, seed)` and `ui_atlas_world_close(ui)`, which reserve and release `owned_atlas`. `ui_deinit` closes it if still open.

8. **Hook into the game.** `run/include/game/index.h`, `run/src/game/index.c`: `borrowed_atlas_pixels` per job; call `ui_atlas_column_top` at the end of `game_column_job_run`; call `ui_atlas_column_write` in `game_column_fill`; call `ui_atlas_save_step` in `game_sim_step`. `run/src/game/session/index.c`: open the store with `<world>/atlas` after the world opens, close it before the world closes. `--test-world` opens it with no directory. Run `run_game`, `run_save_world`, `run_ui_hud` here: nothing they test may change.

9. **Drawing tools.** First, in `run/src/climate/layer/`: `climate_layer_draw_scaled`, and `climate_layer_draw` rebuilt on it. Run `run_climate_layer`: every layer hash phases 03 to 07 pinned must hold. Then `run/include/ui/atlas/draw/index.h`, `run/src/ui/atlas/draw/index.c`, with only what `climate/layer` does not already have: hill shading, scale lookup with the rounding rule (for the Atlas's own scales), marching squares with the saddle rule (if `climate/layer` has none), dotted lines, circles (outline and filled), cones, the "over grey, outline only" test, and legends. Lines and arrows are `climate/layer`'s if it has them. Each is tested on its worked example before any layer uses it.

10. **The registry and the terrain layers.** `run/include/ui/atlas/layer/index.h`, `run/src/ui/atlas/layer/index.c`: the table with all 26 rows (rows whose functions are not written yet point at a function that draws terrain, so the table is whole from the start), then `terrain` and `terrain_lines`, and `ui_atlas_draw_picture` (bands, then overlay, given a job system or none). With no job system, it draws the bands one after another: that is what tests and the map tool use to compare with the parallel draw.

11. **Every other layer.** In registry order, each with its scale, legend and, where it has one, overlay. A field `climate/layer` already draws is a row that calls `climate_layer_draw_scaled` and adds shading and grey. Each calls only phase 02 to 07 functions. Add each layer's golden pictures (step 15) as it lands, not at the end.

12. **The Atlas on screen.** In `run/src/ui/atlas/index.c`: `ui_atlas_input`, the view, the redraw policy, submitting and finishing the drawing job, and `ui_atlas_frame_draw` (copy the picture, then title, legend, slider, readout, player). In `run/src/game/index.c`: call `ui_atlas_input` before `game_screen_input` in both the loop and the headless tick; make `game_screen_up` include the Atlas; in `game_hud_compose`, if the Atlas is up, draw it in place of the HUD (the F3 overlay still on top). Close it when the census screen opens.

13. **The map tool.** `bench/climate/` (and `bench/world_map/`, if it exists): draw through `ui_atlas_draw_picture` with a terrain ground source, `--layer`, `--zoom` and `--flat`. Check that phase 03 to 07 layers with `--flat` at a whole number of blocks a pixel are the same bytes as `bench_climate --pictures` gave before this phase.

14. **The bench.** `bench/atlas/index.c` and `README.md`: build a store over 100 km², save it to a temporary directory, close it, open it again, and time `M` to the first complete picture; also each layer's full draw at 1920 by 1080, the per-frame main cost, and the column write and top.

15. **Tests.** `tests/run/ui/atlas/index.c`, `test.cmake` (copy `tests/run/ui/hud/test.cmake`'s PNG-decoder lines), `README.md`, `fixtures/`. All the tests in **Tests**.

16. **Documents.** A `README.md` in each new directory with purpose, tier, owns, lifetime, threads, public functions, may be called by, depends on. `run/src/ui/README.md` and `run/src/game/README.md` for the wiring. `plans/REPO_STRUCTURE.md`: the four Atlas parts in the UI table, `tests/run/ui/atlas/`, `bench/atlas/`.

17. **Verify and hand over.** Run **Verification**. Tick each todo as it lands. Write every deviation in **Issues found**.

## Todo

- [x] Read the vision file, master plan 2.4 and 9.3, and phases 03 to 07 for the names this phase calls
- [x] `°` and `·` in a seventh font row, in `textures/ui/font/` and `run/src/ui/font/`, with `run_ui_font`, `run_texture` and `run_ui_hud` passing unchanged
- [x] `run/src/ui/atlas/tile/`: pixel packing, `ui_atlas_column_top`, column put, halving, file encode and decode
- [x] `run/src/ui/atlas/`: the store, its lock, slots, merge, stale mips, saving job, `ui_atlas_store_read`
- [x] `ui_t` fields and `ui_atlas_world_open` and `_close` in `run/src/ui/index.c`, reserving `owned_atlas` (no new `ui_t` field: the Atlas is at a fixed place in ui's region, Issues found 2)
- [x] Atlas tiles written as columns generate: top on the worker, write on the sim, in `run/src/game/index.c`
- [x] Tiles saved with the world under `<world>/atlas/`, and read back on reopen, with zoomed-out levels made again when their children change
- [x] `run/src/ui/atlas/draw/`: hill shading, scales, sample grid, marching squares, lines, arrows, circles, cones, outlines
- [x] `run/src/ui/atlas/layer/`: the registry, all 26 layers, `ui_atlas_draw_picture`
- [x] Ungenerated land grey; systems, cells, radar and pressure-now over it as outlines only
- [x] `run/src/ui/atlas/`: keys `M`, wheel, drag, `1` to `9`, `[` `]`, `P`, `Esc`; the day slider; the readout; the player mark
- [x] Redraw once per change of layer, zoom, pan or day; now layers twice a second; drawn on workers, front and back pictures
- [x] `game_screen_up` includes the Atlas, so Esc closes it; the game keeps running while it is up (`run_ui_atlas_game`, Issues found 17 and 18)
- [x] `climate_layer_draw_scaled` in `run/src/climate/layer/`, with every pinned layer hash holding
- [x] `bench/climate/` (and `bench/world_map/` if it exists) draws every layer through the same code, and phase 03 to 07 layers come out the same bytes as before (`bench/climate`, and `bench/world_map/atlas/` over real land, Issues found 20)
- [x] Golden pictures of each layer at two zooms, in `tests/run/ui/atlas/fixtures/`
- [x] `bench/atlas/`: opening the Atlas over 100 km² of generated land, under 100 ms to the first picture (16.2 ms median; every layer but temperature now under 60 ms since Issues found 26; temperature now and the column top still missed, Issues found 27)
- [x] Every `tests/run/ui/atlas/` test in **Tests** passes on debug and sanitize, and the Atlas parts on tsan (`run_ui_atlas` and `run_ui_atlas_game`, 29 tests, Issues found 17 and 22)
- [x] READMEs for each new directory, `run/src/ui/README.md`, `run/src/game/README.md`, `plans/REPO_STRUCTURE.md`
- [ ] Gate: Ahmed watches a storm come in on the Atlas and meets it

## Tests

All in ctest `run_ui_atlas` (`tests/run/ui/atlas/index.c`), unless named otherwise. Each says how it fails.

**Tiles and the store**

- `a_pixel_packs_colour_and_height_and_none_means_not_generated`: every `int16_t` height from -250 to 1250 and 1000 random colours round-trip; `UI_ATLAS_PIXEL_NONE` is not generated; height 0 with colour 0 is generated. Fails on any mismatch.
- `a_columns_top_skips_air_and_plants_and_stops_at_water`: synthetic column states, by hand: grass block under a flower at z 64 gives the grass block's colour at 64; sea at z 0 over sand at -12 gives water at 0; a torch on stone gives stone. Fails if any of the 256 pixels differs from the hand-worked one.
- `a_column_lands_in_its_tile_at_its_place_and_nowhere_else`: 10 000 random columns, including negative coordinates and tile edges, against a brute-force model of a dictionary from block to pixel. Fails on any pixel that differs, and on any pixel outside the column that changed.
- `a_mip_is_the_halving_of_its_children_as_worked_by_hand`: the Background example exactly (25, 26, 5, height 70), all-ungenerated gives ungenerated, and random children against a separate model. Fails on any channel off by one.
- `tile_files_read_back_the_same_and_a_damaged_or_foreign_file_is_ignored`: random tiles, raw and runs, round-trip byte for byte; runs are chosen only when smaller. Then each header field changed, a payload byte flipped, a file of another seed, version, level or position, and a run total short or long: each is refused, and the tile reads as never saved. Fails if any is accepted.
- `a_tile_made_before_its_file_was_read_keeps_what_is_newer`: write a file, write some columns into a fresh slot with `merge` set, merge: the new columns win, the file fills the rest. Fails on any pixel that came from the wrong side.
- `tiles_written_while_playing_are_there_after_a_reopen`: a real game, one thread, with a save directory: walk until 50 columns have generated, close, reopen with no columns yet streamed, and read the store: every one of those columns' pixels is as it was. Fails on any difference or missing file.
- `mips_follow_their_children_after_a_save`: write columns into four level-0 tiles, run the saving job, and check levels 1 to 7 against halving by hand from level 0. Fails on any pixel.
- `the_sim_never_waits_long_on_the_lock`: one thread writes columns as fast as it can while another draws pictures and a third saves, for 5 s; the longest wait for the lock in `ui_atlas_column_write` stays under 2 ms. Fails above that, with the time printed.
- `with_every_slot_dirty_a_write_is_dropped_counted_and_said_once`. Fails if it blocks, crashes, or logs more than once.

**Drawing**

- `the_layer_table_is_whole`: 26 rows; every key unique, lower-case and safe in a file name; every group 1 to 9 has at least one layer; every field layer has a scale or entries; every `over_grey` layer has time `now`. Fails naming the row.
- `hill_shading_is_the_worked_example`: flat gives $b = 1.000$; rising east (70, 74) gives 1.079; rising west gives 0.4. Fails beyond 0.001.
- `marching_squares_is_the_worked_example`: the square (1010, 1013, 1015, 1011) at 1012 gives the segment (2.67, 0) to (1, 4); both saddle cases follow the mean rule. Fails beyond 0.01 pixel.
- `colour_scales_hit_their_stops_and_mix_between`: 25 °C is (249, 167, 103); every stop gives its own colour; below the first and above the last are held. Fails on any channel.
- `golden_pictures_of_every_layer_at_two_zooms`: for each of the 26 layers, a 320 by 240 picture at zoom 0 (1 block a pixel, level 0) and zoom 6 (8 blocks a pixel, level 3), compared byte for byte with `fixtures/<key>_z1.png` and `fixtures/<key>_z8.png`: 52 goldens. The world is seed `2198175988018515243`, at a place phase 05's gate pictures showed has coast, mountains and a warm current (record the place in the test). The ground source reads `terrain_height_estimate` (phase 03) for height and colours it by a fixed test palette (sea below 0, sand to 3, green to 300, rock to 700, snow above), so the goldens test the Atlas's drawing, not the terrain. The right third of each picture is ungenerated. The `systems` and now layers use a fixed list of systems built in the test, with Hurricane "Tavela" crossing from the grey third onto generated land, a low with a cold front and a high. Fails naming the file and the count of pixels that differ. Every golden is looked at by a person when made.
- `ungenerated_land_is_grey_and_shows_only_system_outlines`: on every layer's z1 picture, every pixel in the ungenerated third is `UI_ATLAS_COLOUR_UNGENERATED` unless the layer has `over_grey`. For those, every non-grey pixel there lies on an outline the test works out on its own (circle rings within 1 pixel, track and cone edges, the 1 mm an hour contour, name text boxes). And the hurricane's outline **is** there: at least 90% of its ring's pixels in the grey third are drawn. Fails naming the layer and the first wrong pixel.
- `climate_layers_are_the_same_code_as_the_atlas` (in `run_climate_layer`): for every phase 03 to 07 layer, `climate_layer_draw` and `climate_layer_draw_scaled` at the same whole blocks a pixel give the same bytes, and the Atlas row with shading off and nothing ungenerated gives those bytes too. Fails naming the layer.
- `any_split_into_bands_gives_the_same_bytes`: every layer drawn with 1 band, 7 bands and 16 bands, and on 1 worker and 16: all identical. Fails naming the layer.
- `a_pan_samples_the_same_world_points`: a picture panned by 13 pixels right and 7 down equals the original shifted, where they overlap, on every field layer. Fails on any pixel.

**The Atlas on screen**

- `a_picture_is_drawn_once_per_change_and_now_layers_twice_a_second`: with a fake clock and a counting job hook: a normals layer held still for 10 s of frames draws 1 picture; a day change draws 1 more; a now layer over 10 s draws 20, plus or minus 1; streaming columns at 100 a second redraws at most 20 times in 10 s. Fails with the counts.
- `keys_do_what_the_table_says`: `M` opens and closes; `Esc` closes; a wheel detent zooms by $\sqrt 2$ and the block under the pointer stays within half a pixel; a drag of (100, -40) pixels at 4 blocks a pixel moves the centre by (-400, -160) blocks; `1` to `9` choose groups, a repeat cycles, and wraps; `]` on day 364 gives 0 and `[` on 0 gives 364; Shift adds 30; `P` centres on the player. Fails naming the key.
- `m_does_nothing_with_a_screen_up_and_e_does_nothing_with_the_atlas_up`.
- `while_the_atlas_is_up_the_game_keeps_running_and_the_body_stands`: a real game, one thread, walking forward: open the Atlas for 100 ticks. `world_ticks` rises by 100, `game_t.paused` stays 0, columns still stream in, and the body's position does not change. Fails on any.
- `esc_closes_the_atlas_and_not_the_world`: in a played world, Esc with the Atlas up closes it and does not open the pause screen; a second Esc does. In a test world, Esc with the Atlas up does not end the run. Fails on either.
- `the_reading_is_what_the_climate_functions_say`: at 20 pointer positions, the readout's numbers equal phase 02 to 05's functions for that block, as formatted. Over grey land it says `not explored`. Fails naming the field.
- `no_heap_after_the_world_opens`: 200 frames with the Atlas up, panning, zooming and changing layers, and 1000 columns written: the heap's call count does not move. Fails with the count.
- `the_program_opens_the_atlas_with_m_and_closes_it_with_esc`: the built program, `--headless`, a script that opens a new world, presses `m`, takes a screenshot, waits 40 ticks, takes another, presses `escape`, and takes a third. The first two differ from the world picture in more than 90% of pixels; the second differs from the first only where a now layer or the player mark is (or not at all on `terrain`); the third is the world with the HUD again. Its log shows ticks going on throughout. Fails on any.

**Bench, not ctest:** `bench_atlas` prints `MISSED` if opening over 100 km² takes 100 ms or more to the first complete picture, or a full picture of any layer at 1920 by 1080 takes 60 ms or more on 16 workers, or the main thread's frame with the Atlas up takes 2 ms or more.

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_ui_atlas|run_climate_layer|run_ui_font|run_texture|compile_texture_vector|run_ui_hud|run_ui_screens|run_game|run_save_world|run_settings|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --preset sanitize
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 900 ctest --preset sanitize -R '^run_ui_atlas$' --output-on-failure
    tools/test-or-build.sh cmake --preset tsan
    tools/test-or-build.sh cmake --build --preset tsan
    tools/test-or-build.sh timeout 900 ctest --preset tsan -R '^run_ui_atlas$' --output-on-failure
    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate --pictures /tmp/mc_c_maps 2198175988018515243 --layer all --zoom 6
    tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Look at every golden picture in `tests/run/ui/atlas/fixtures/` and the map tool's PNGs in `/tmp/mc_c_maps/` (and run the same through `bench_world_map` if 003 phase 8 stage 1 has made it) before calling this done.

## Gate

Ahmed builds the release preset (above), then:

    ./build/release/bin/minecraft_c --windowed

1. Create a new world, or open one. Walk and fly for a few minutes so some land is generated.
2. Press `M`. Generated land shows with hill shading; everything else is grey. Mobs and the clock keep going (listen, or watch the player mark if something pushes you).
3. Scroll to zoom out and in; drag to pan; `P` to come back.
4. Press `2`, and `2` again, through the temperature layers. On a normals layer, press `[` and `]`, or drag the slider, and watch the seasons move.
5. Press `7` for systems. Find a hurricane, ideally one over grey land as an outline, with its name and cone. Then `6` for pressure now: its isobars and an `L`.
6. Close with `M` or `Esc`, travel toward the storm, open the Atlas now and then to watch it come, and meet it.
7. Quit, start the game again, open the same world and press `M`: the land you explored is still there, before you walk anywhere.

## Risks and open questions

| # | Question or risk | Default |
|---|---|---|
| 1 | The master plan says "`1` to `9` choose layers" but lists 26. | Groups: a key chooses a group, pressing it again cycles. Table above |
| 2 | Disk: about 541 MiB raw for 100 km² explored. | Runs where smaller; no compression library (none in `run/`). A later phase can add one behind the same header with a new encoding value |
| 3 | Live edits to the top block (digging a hole, building a tower) do not change the Atlas until the column loads again. | Accepted for now. Following `world_edit` changes is a small later addition |
| 4 | The card keeps drawing the world behind the full-screen Atlas, which nobody sees. | Accepted: the game keeps running, as decided, and the frame rate is unchanged. Skipping those pictures is a later saving |
| 5 | Mobs can hurt the player while the Atlas is up. | That is what "the game keeps running" means. Ahmed may want the Atlas to close when the player takes damage: one line, not done unless asked |
| 6 | Top-face mean colours may look odd for some blocks (a texture made to be tinted, or very dark). | Look at the `terrain` goldens and a real world. Override single colours in a small table in `layer/` if needed, and record which |
| 7 | 52 golden PNGs at 320 by 240 are about 230 KB each (the PNG writer stores uncompressed, `run/src/display/headless/png/README.md`), about 12 MB in git. | Accept, as the HUD's goldens did. If too big, keep hashes in the test and write PNGs only under `MC_GOLDEN_WRITE` |
| 8 | Phase 06 may give systems "near a point" only. | Ask on a 32 km grid over the view plus 35 km, keep each id once (Design) |
| 9 | Climate tiles for generated land may have been evicted from phase 03's cache when the Atlas opens. | The first picture shows terrain and whatever climate is ready, marked to redraw in 500 ms. The bench reports cold and warm numbers |
| 10 | Shade exaggeration: hills may look flat at far zooms. | `UI_ATLAS_SHADE_EXAGGERATION = 1`, one constant, for Ahmed to tune by eye |
| 11 | The census screen closing the Atlas. | Yes, view remembered |
| 12 | A screen larger than 3840 by 2160. | The Atlas does not open, and says why |

## Commits

Appended as they land:

- `956be1ac` Add a seventh font row with the degree sign and the middle dot, each drawn as one character, for the Atlas and the HUD.
- `e522dcdf` Add the Atlas on M: column tops kept in saved tiles with zoomed-out levels, 26 layers drawn on workers with ungenerated land grey, and its keys, readout and legend over the running game.
- `222a414d` Test the Atlas: tiles, files, the store and its lock, every layer's golden hash at two zooms, bands, pans, keys, the readout and the built program.
- `1b25545a` Add bench_atlas, and let bench_climate draw every Atlas layer through the Atlas's code with --layer, --zoom and --flat.
- `34979d23` List the Atlas's directories, its test and its bench in the repository structure.
- `1830670f` Draw no row above a picture's first in the Atlas's climate bands, as climate_layer_draw does not, and work out a column's top layer by layer so it reads memory in order.
- `b2ff4d1c` Keep the font test's bit sum in eight bits for the sanitize build, and widen the Atlas lock test's bound under the sanitizers, which slow every tile copy.
- `1ef70f36` Give back every arena the Atlas tests lay over the test arena, so the sanitize build's poisoning does not outlive the test.
- `5dd45898` Do not hold the Atlas program test's two open screenshots to differing, as the first picture may land before either is taken.
- `f94c15ba` Record plan 004 phase 09 in its phase file: the todos done, the commits, the verification runs, the bench and the issues found.
- `ffb1c843` Merge the first agent's Atlas work for plan 004 phase 09 onto main, moving its input hooks and the eye's steps into game/input.
- `7df7b788` Test the Atlas in real games, the body standing and the world ticking and streaming while it is up and Esc closing it and not the world, and keep a flying world's eye still and its player untouched with the Atlas up.
- `3b4be08d` List the Atlas's real-game test directory in the repository structure.
- `2ea5d249` Add bench_world_map_atlas, which draws every Atlas layer over a seed's real column tops through the Atlas's own code and checks the climate fields with --flat.
- `e3a3ba96` List bench_world_map_atlas in the repository structure.
- `11ce91c8` Show now values in the Atlas's readout on now layers, the weather at the explored ground at the world's moment, kept fresh every half second.
- `0e9c9c2c` Set threads_running before the sim and gpu threads start, so the gpu thread's draw reads it after it is written, as tsan found in the Atlas's threaded test.
- `7e75fd20` Hold bench_world_map_atlas to zoom 5, as at zoom 6 its store forgot a band of land before the zoomed-out tile was made.
- `48228455` Give bench_world_map_atlas's zoom range as 0 to 5 in the repository structure.
- `5f0ca70e` Record the second agent's work on plan 004 phase 09 in its phase file: the real-game tests, the map tool over real land, the readout's now values, two fixes and the runs.
- `3714e8f6` List the phase file commit of plan 004 phase 09 in its phase file.
- `5756a425` Draw only the ground for a climate layer in a world with no climate, where the Atlas's workers crashed, keep the legend's numbers from writing over each other, and move the readout above the legend where they would meet.
- `b3c7ded1` Describe the Atlas's no-climate program test in its test directory's README.
- `85686290` Work out the wettest day of climate_normals_at from a table of the same cosines and sines, bit for bit, so the Atlas's phase 05 layers draw in 36 ms and not 66.
- `f8676108` Name climate_harmonic_max2 in climate/harmonic's README.
- `56861f9d` Record the gate check of plan 004 phase 09 in its phase file: the no-climate crash and the layout faults fixed, the phase 05 layers under 60 ms, the two targets still missed, and the runs.

## Issues found

**The font cannot draw the degree sign or the middle dot the plan writes.** Expected: "°C" and "23.5°" in legends, the readout and latitude labels (master plan sections 5, 6, 9), and "Year 2 · Day 214" on the HUD (9.1). Actual: the font has printable ASCII only, and anything past it is drawn as `?`. Evidence: `run/include/ui/font/index.h:63-64` ("any other character the atlas does not have, including every one past ASCII, is one '?'"), and `run/src/ui/font/README.md` ("A character past ASCII is one '?'"). The atlas is 128 by 48 (`UI_FONT_ATLAS_WIDTH`, `UI_FONT_ATLAS_HEIGHT`), and only its DEL cell is empty (`UI_FONT_GLYPHS = 96u`, "the last cell is DEL's, and empty", `run/include/ui/font/index.h:18`): room for one character, and two are needed. Done: step 2 adds a seventh row with both, unless phase 01 already did. Phase 10 relies on it.

Done (2026-11-06): the seventh row is in `textures/ui/font/index.yaml` (128 by 56) with `°` and `·` (cells 0 and 1), `index.mctex` repainted by `texture_vector recipes`, `UI_FONT_ATLAS_HEIGHT = 56u`, `UI_FONT_EXTRA = 2u`, `ui_font_glyph_extra`, and `ui_font_next` maps `0xC2 0xB0` and `0xC2 0xB7` to one character each. New test `the_degree_sign_and_the_middle_dot_are_one_character_each_and_ascii_is_unchanged` in `run_ui_font`. `run_ui_font`, `run_texture`, `compile_texture_vector`, `run_ui_hud` (its goldens unchanged) and `run_climate_layer` pass. No earlier phase had done it. Commit `956be1ac`.

**1. Step 1, the names this phase calls.** Phase 02: `climate_latitude_deg`. Phase 03: `climate_t`, `climate_is_open`, `climate_sample_at`, `climate_cell_get`, `climate_layer_draw`, `climate_layer_t`, `CLIMATE_LAYER_GREY`. Phase 04: `climate_air_at`, `climate_current_flow` (through climate/layer), `climate_layer_pressure_colour`, `climate_layer_wind_colour`. Phase 05: `climate_normals_at`, `climate_normals_at_t` (the phase 05 file's `climate_normals_t`), `CLIMATE_LAYER_TEMPERATURE_*`, `CLIMATE_LAYER_CLASS_RGB`. Phase 06: `climate_systems_day_build`, `climate_systems_day_t`, `climate_system_state_at`, `climate_systems_front`, `climate_systems_thunder_at`, `climate_systems_pressure_at`, `climate_systems_wind_at`, `climate_systems_track`, `climate_systems_mask_start`, `climate_system_title`, `climate_systems_day_of_year`, `climate_layer_rain_now`, `climate_layer_radar_colour`. Phase 07: `climate_weather_time_of_clock`, `climate_weather_at`, `climate_week_store`, `climate_week_store_cell`, `climate_week_t` (the plan's `climate_week_t` is right; the store is `climate_week_store_t`), `climate_outlook_sigma`, `climate_outlook_error`. Phase 01: `calendar_date_of`, `calendar_week_of`, `CALENDAR_CLOCK_DAY`. The lint's rank table already had `climate` (8) and `calendar` (11): no change there.

**2. `ui_t` has no room, and `app_t` is at main's frame limit.** Expected: two fields from `ui_t`'s `_pad0[48]`. Actual: `ui_t` has no `_pad0` (phase 07's `census_screen` took the last 16 bytes; `run/include/ui/index.h`, `static_assert(sizeof(ui_t) == 1152)`), and `app_t` is 16128 bytes on main's stack, which the release link holds under 16 KiB (`run/include/main/index.h`). Growing `ui_t` to 1216 would push `app_t` toward the limit. Done: no field is added. `ui_init` reserves the Atlas with ui's own region: the 2 MiB of arenas, then `ui_atlas_t` at a fixed offset, its colour table, and its region (`UI_ATLAS_REGION_BYTES`, about 500 MiB of address space, touched only as used); `ui_atlas(ui)` finds it. So the region is reserved at `ui_init`, not at world open, and `ui_atlas_world_open`/`_close` start and stop the store over it. The tier rule holds: only `run/src/ui/index.c` calls the heap. `no_heap_after_the_world_opens` passes.

**3. A pthread mutex let a reader starve the sim.** Expected: the longest wait of `ui_atlas_column_write` under 2 ms with a writer, a reader and a saver racing. Actual, with the plan's `pthread_mutex_t`: `83 writes, 28142 reads, 1 saves; the longest wait 1557.998 ms` (`the_sim_never_waits_long_on_the_lock`): a reader in a loop takes the lock back before the woken writer runs, as glibc's mutex is not fair. Done: the store's lock is a ticket lock (two atomics, acquire and release, `sched_yield` while waiting), first come first served, in the same 40 bytes. After: `101737 writes, 25432 reads, 10 saves; the longest wait 0.645 ms`. The 16 band jobs of a picture read the store at once in the game, so this matters there too.

**4. `layer/` is the Borrow tier, so it cannot allocate.** Expected (plan): band functions with a `scratch` they allocate from, and `ui_atlas_draw_picture` in `layer/`. Actual: the lint refuses any `arena_alloc` at depth 3 (`[tier_borrow_alloc]`, 9 breaches on the first build). Done: a band is given its memory as a buffer (`ui_atlas_scratch_t`, sized by `ui_atlas_band_scratch_bytes`) and carves it; `ui_atlas_ground_t.read` is lent an `arena_t` over a piece of it; `ui_atlas_draw_picture` lives in `run/src/ui/atlas/` (depth 2), which takes the bands' memory from the worker's job arena or a given arena. `ui_atlas_draw_input_t` carries `t` (the systems' time) and the day list instead of `world_ticks`; `ui_atlas_target_t` holds the picture, its grey mask, a per-pixel field the overlay's isobars read, and the overlay's scratch.

**5. climate/layer's lines and arrows are `static`.** Expected: the Atlas calls phase 04's Bresenham and arrows. Actual: `layer_line`, `layer_arrow` and the rest are file-local in `run/src/climate/layer/index.c`. Done: the currents' and wind's arrows and the latitude lines are drawn by climate/layer itself, through the new marks part of `climate_layer_draw_scaled` on the whole picture; `draw/` has its own Bresenham, circles and arrows (the same algorithm and barbs) for the Atlas's own marks, with the grey modes climate/layer has not.

**6. The sample grid inside `climate_layer_draw_scaled`.** Expected: `climate_layer_draw` rebuilt on `climate_layer_draw_scaled` with unchanged bytes. Done: `climate_layer_frame_t` (west, north, blocks a pixel, world pixel of column and row 0, sample spacing, parts); `climate_layer_draw` is the scaled call with sample 1 and indices 0. Evidence: main's `run_climate_layer` built from `git archive main` printed 36 layer hashes, and this branch's printed the same 36 (diff empty), phase 04's 9 also matching the values recorded in its phase file; the new `climate_layers_are_the_same_code_as_the_atlas` test holds draw and draw_scaled to the same bytes for all 17 layers and a 13 by 7 pan of a 4-pixel sampled picture to the same pixels. With sample n the four grid values are blended (the pressure layer's hPa, others' colours), the nearest taken where one is grey.

**7. Temperature now is costly.** `climate_weather_at` builds a day's weather lists for every call, so the Atlas samples it every 32 pixels, not every 4, and blends between. It also needs every climate tile within 6 km: where one is not built the ground shows. The goldens prepare 14 km round their place for it.

**8. The golden pictures are hashes, not PNGs.** Risk 7's own fallback: 52 PNGs at 320 by 240 are 15.6 MB as the PNG writer stores them, and the disk is short (27 GB free). `fixtures/goldens.txt` holds an FNV-1a hash of each picture's RGB; `MC_ATLAS_PNG_DIR=<dir>` writes the PNGs to look at, and `MC_GOLDEN_WRITE=1` writes the hashes again. Looked at on 2026-11-06 (the agent read the PNGs): terrain with coast, sea, lakes and mountains at 8 blocks a pixel, rock and grass at 1; the grey third; the hurricane's red ring and "Tropical storm Zoomi" over grey, its dotted track and cone (at 1 block a pixel the 25% red cone covers the picture, as σ = 200 blocks at t = 0 is wider than 320 pixels); a low's L and blue front, a high's H and ring; temperature, rain, classes, currents' red arrows, wind arrows, radar's blue, cells with their zig-zags. The hand-made systems are small (a 60-block hurricane) so they show at 1 block a pixel: real ones are 1500 to 4000.

**9. The readout reads normals on every layer.** Expected: now values on now layers. Actual: the now values need the weather systems' day list, which belongs to the drawing job and is rebuilt under it. Done: the readout is `climate_normals_at` and `climate_air_at` for the slider's day on normals layers and today otherwise, the current kind from `climate_cell_get`, and the class; `the_reading_is_what_the_climate_functions_say` holds it to those functions. Now values in the readout are left for later.

**10. `game/index.c` is past the split line.** It was 1498 lines; the plan's wiring there (job pixels, the top, the write, the save step, input, `game_screen_up`, the HUD's branch, open and close) brings it to 1518. Done: everything that could go elsewhere is in a new part, `run/src/game/atlas/`; the calls left in `game/index.c` are one or three lines each. Splitting `game/index.c` further is not this phase's.

**11. With no directory a slot is never dirty.** A `--test-world` keeps nothing on disk; were its slots dirty they could never be saved, and after 160 tiles every write would drop. So without a directory a written tile is clean and the least recently used is taken for a new one: its land is forgotten, at every level, as there is nowhere to keep it.

**12. Not done here, or only in part.** `bench/world_map/` exists but was not given `--layer`; `bench/climate` was. The two real-game tests (`while_the_atlas_is_up_the_game_keeps_running_and_the_body_stands`, `esc_closes_the_atlas_and_not_the_world`) are not written: the built program's test opens and closes the Atlas in a real world with a script, and `game_screen_up` and `game_screen_input` route Esc as the plan says, but the session's pause screen and the body's position are not checked by a test. `tiles_written_while_playing_are_there_after_a_reopen` drives the store as the game does, not a game walking 50 columns. `snow_balance` keeps phase 05's sea colouring (the plan wanted plain ground over sea for it). Risk 6, the top faces' mean colours in a real world (grass tops may be grey before tinting), waits for Ahmed's look.

**13. The bench, on release (2026-11-07, 16 workers, other agents' builds sharing the machine).** `bench_atlas`: built 400 tiles (105 km²) in 4.5 s with no column lost; **opening the Atlas over 100 km², `M` to the first picture: median 16.4 ms (fastest 15.9, slowest 17.5), met** against 100 ms; main's frame with the Atlas up 0.70 ms, met (2 ms); a column write 0.08 µs, met (5 µs). **Missed:** every phase 05 field at 1920 by 1080 (temperature on the day, mean, warmest, coldest; rain by day and year; hurricane season, monsoon, class, snow balance) 66 to 69 ms median against 60, and the rain radar 63 ms: each is 130 000 `climate_normals_at` calls on the 4-pixel grid; **temperature now 438 ms**, `climate_weather_at` at every 32 pixels (Issues found 7); **a column's top 108 µs** against 20, after making the walk read memory in order (it was 618 µs): a column is 96 sections, and above the ground every voxel of every section is read. Terrain, the week layers, wind, currents, pressure and the systems are 10 to 36 ms. Not fixed here: phase 11 (tuning and cost) is where these go.

**14. The map tool's same bytes.** `bench_climate --pictures /tmp/mc_c_maps_flat 2198175988018515243 --layer all --zoom 8 --flat`: "13 of 13 climate fields the same bytes as climate_layer_draw", exit 0. The first run found 1 pixel differing on `pressure_day`: the Atlas's first band drew a row above the picture so its lines' top row looked north, where `climate_layer_draw` has nothing north of row 0. Fixed (`1830670f`): no row above the picture's first. `--zoom 6` without `--flat` wrote all 26 `atlas_<key>.png`; the agent looked at `atlas_systems.png` (hill-shaded coast and mountains; no system round (0, 0) on day 196 of this seed, as phase 06's Issues found 21 says of real seeds).

**15. Sanitize and tsan.** tsan: `run_ui_atlas` and `run_ui_font` pass with no race reported, after the lock test's bound was widened to 20 ms under the sanitizers (tsan measured 7.4 ms: every 1 MiB tile copy under the lock is instrumented; on debug and release builds the bound stays 2 ms). Sanitize, first run: `run_ui_font` did not build (`-Wconversion` at `-O1` on the new test's bit sum, fixed `b2ff4d1c`) and `run_ui_atlas` stopped with ASan's use-after-poison in the harness: the Atlas tests laid arenas over the test arena and never gave them back, so their poison outlived each test (fixed `1ef70f36`).

**16. The last runs (2026-11-07).** After `5dd45898`: debug `run_ui_atlas` (25 tests), `clang_format` and `standards_lint` pass; sanitize `run_ui_atlas` and `run_ui_font` pass with no sanitizer report. Before it, on `b2ff4d1c`: tsan `run_ui_atlas` and `run_ui_font` pass, no race; the Verification's debug list (`run_ui_atlas`, `run_climate_layer`, `run_ui_font`, `run_texture`, `compile_texture_vector`, `run_ui_hud`, `run_ui_screens`, `run_game`, `run_save_world`, `run_settings`, `standards_lint`, `clang_format`) passes; **the whole debug suite, 143 of 143, in 2291 s**; the release build (`-O3 -flto`, `-Wframe-larger-than`) links clean, so main's frame is unchanged under 16 KiB (no `app_t` member grew). The program's screenshots were looked at: the world, the Atlas over it (title, the explored land round the player with the grey round it, the player's mark, the readout), and the world again after Esc. Every build directory but debug was removed after use. `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c` printed nothing after the runs. Ahmed's gate (watching a storm come in on a played world, and the explored land there after a restart) is his.

**17. The second agent's work (2026-11-07): the tests in real games.** The two tests Issues found 12 left out are written, in a new directory `tests/run/ui/atlas/game/` (ctest `run_ui_atlas_game`), as they need the card and `tests/run/ui/atlas/index.c` was past 1500 lines (1567); the built program's test moved there with them, so `run_ui_atlas` holds 25 tests, `run_ui_atlas_game` 4. A played world is made through the menus in the session's headless scene (the census test's way, seed 7, saves in a directory of its own).
- `while_the_atlas_is_up_the_game_keeps_running_and_the_body_stands`: W held for the 101 ticks the Atlas is up: game ticks and world/tick's rise by 101, `paused` stays 0, the session stays playing, the body stays at (8.5, 8.5, 3.0); closed, the same W walks it 6.2 blocks. Expected also: columns stream in. Actual with one thread: 0, because each headless tick settles everything the eye wants and the body stands. So the streaming half runs on the real threads: a flying test world under fake input, M on the first frame, W held, 150 frames of 10 ms: 30 ticks and 172 columns behind the Atlas, the eye moved 0.000 blocks.
- `esc_closes_the_atlas_and_not_the_world`: played world, M on tick 41, Esc on 52 closes the Atlas and the world plays on; the second Esc on 63 brings up the pause screen. Test world: under `game_loop_source`, Esc with the Atlas up closes it and the source goes on; a second Esc ends the run.
- `tiles_written_while_playing_are_there_after_a_reopen` now walks a real played world: 64 columns generated, 16384 explored pixels round the start read from the store, the world closed through the session (which saves), the Atlas opened again on `<world>/atlas` with nothing streamed: 0 of them differ, no file refused. The store-only version stays in `run_ui_atlas`.
- The program test also checks the run reached all 91 ticks with the Atlas up and down (`after 91 ticks`).

**18. M crashed a flying test world under real input.** Expected: M opens the Atlas in any world with a ui. Actual: `game_loop_input_frame` sent walk input with a screen up, which before the Atlas could only happen with a body; with the Atlas up in a flying world `player_look_send` got a null player (SIGSEGV in the new Esc test, `run/src/player/index.c:111`). Done: with a screen up the walk and hands are sent only if there is a body, and the eye's flying input is zeroed. The headless flying tick gave the Atlas no input at all; it now goes through `game_screen_input` as walking does. Wiring in `run/src/game/input/index.c` and `run/src/game/index.c`.

**19. `game/index.c` back under the split line.** The merge with main (which had moved the screen functions to `game/input/`) left it at 1518. Done: `game_fly` and `game_walk_view` (now public) and later `game_pause_set` moved to `run/src/game/input/` with their READMEs; the Atlas's `game_screen_input` and `game_screen_up` changes went there too. It is 1496 lines.

**20. `bench/world_map/`.** It is one file of 1417 lines, and a bench is one program a directory, so `--layer` there would pass 1500 with no way to split. Done: `bench/world_map/atlas/` (`bench_world_map_atlas <directory> [seed] [--layer <key|all>] [--zoom <0 to 5>] [--flat]`) draws every layer through `ui_atlas_draw_picture` over a seed's real column tops, made with the game's own `terrain_generate_box` and `ui_atlas_column_top` and written into an Atlas store as the sim writes them, 512 by 512. Release, seed 2198175988018515243: zoom 4 `--flat`, 130 by 130 columns in 9.4 s, "13 of 13 climate fields the same bytes as climate_layer_draw", exit 0; zoom 5, 184 by 184 columns in 18.5 s, 0 pixels ungenerated on every layer. Zoom 6 first lost a 64-row band on every layer (32768 pixels ungenerated): 100 level-0 tiles and their mips filled the 160 slots of a store with no directory, which forgets the least recently used. It is now refused past zoom 5 (64 level-0 tiles at most). The agent looked at `terrain` (coast, sea, hill-shaded snowy and green land, leaf tops as dots) and `temperature_day`.

**21. The readout now shows now values on now layers** (Issues found 9 said normals only). On a now layer it is `climate_weather_at` at the explored ground (sea level over the sea) at the world's `t`, with the last census's week cell: `11.2 °C  0.4 mm an hour  wind 6.1 from SW  1013 hPa  current warm  Temperate`, worked out again every half second as the picture is, on main, borrowing the band arena (main's only). `the_reading_is_what_the_climate_functions_say` holds both kinds at 20 places. `ui_atlas_readout` takes the world.

**22. A data race tsan found in the new threaded test.** `game_frame_draw` on the gpu thread reads `game->threads_running` (plan 004 phase 08's `game_weather_draw` call), which main wrote after starting the threads. Done: it is set before `thread_start`, which hands it to both threads, and set back to 0 on a failed start. `run_game` and `run_ui_atlas_game` pass on tsan with no report.

**23. The last runs (2026-11-07, the second agent).** Debug: the Verification's list plus `run_ui_atlas_game` (13 tests) pass. Sanitize: `run_ui_atlas`, `run_ui_atlas_game`, `run_ui_font` pass with no report. Tsan: `run_ui_atlas` passes; `run_ui_atlas_game` and `run_game` pass after the fix in 22. Release (`-O3 -flto`, frame check) links clean. `bench_atlas`: open over 100 km² median 16.5 ms (met), main's frame 0.72 ms (met), column write 0.09 µs (met); missed as before: phase 05 fields 65 to 67 ms, rain radar 61 ms, temperature now 432 ms, column top 101 µs (phase 11's). `bench_climate --pictures ... --layer all --zoom 6`: exit 0. **The whole debug suite, 147 of 147, in 2314 s**, after every change above but the phase file's. `pgrep -af "$PWD/build"` printed nothing of this worktree's after the runs.

**24. The gate check (2026-11-07): M then a climate layer crashed every test world.** Expected: in any world with a ui, `M` then `2` shows the temperature layer. Actual: `--headless --test-world --walk`, `M`, then `2`: SIGSEGV, every worker in `climate_normals_days_at` with `climate=0x0` (gdb: `layer_band_climate` → `climate_layer_draw_scaled` → `layer_field_value`). A test world has no climate (`game->borrowed_climate` is nullptr), and `atlas_draw_start` passed it on; the layer table's climate bands and overlays (marks, systems: `climate_seed`) all read it. No test drew a layer with no climate: the program test only showed `terrain`. Done (`5756a425`): `atlas_draw_start` passes the climate only when it is open, and `ui_atlas_draw_band` and `ui_atlas_draw_finish` draw a climate layer's ground only, with no overlay, when there is none. New test `every_layer_draws_in_a_test_world_with_no_climate` in `run_ui_atlas_game`: the built program presses `m`, then each of `1` to `9` four times; it failed on the old binary (exit 139, no screenshot) and passes on the new one.

**25. Two layout faults seen in the program's screenshots at 640 by 480.** The temperature legend's five numbers ran into each other, so `-11.2` lost its minus under `-30` and read `11.2`; and the readout's panel, drawn last at the bottom left, covered the legend's at the bottom right. Done (`5756a425`): the legend writes five numbers where they fit apart, else three, else the two ends; the readout goes above the legend's top where the two would meet (at 1280 or more across they do not). Still seen at 640 by 480 and left: a long legend title (`Normal wind (blocks a second)`) runs past its panel, and the day slider covers the legend's arrow line. Goldens are the pictures, not the frame's panels, so none changed.

**26. The phase 05 layers missed 60 ms, and phase 11 may not fix them.** Expected: every layer under 60 ms at 1920 by 1080 on 16 workers. Actual: the nine phase 05 fields 65 to 68 ms, the radar 61 ms (Issues found 13). Phase 11's file allows changes to `run/src/climate/` and `run/src/ui/atlas/` only to turn literals into constants (its Owned paths), so the miss would have stayed. Cause: each `climate_normals_at` call worked out the wettest day from 73 calls of `climate_harmonic_at2`, four double `cos`/`sin` each: about half of its 8 µs. Done (`85686290`): `climate_harmonic_max2` in `run/src/climate/harmonic/` reads those 292 values from a table written out bit for bit (`%a` of glibc's own `cos` and `sin` of the same theta), with the same sum in the same order (`-std=c23`, so no contraction). New test `the_largest_of_every_fifth_day_is_the_same_bits_as_the_calls` in `run_climate_harmonic`: 20 000 random coefficient sets, 0 differ, debug and release. Every pinned climate digest holds (the whole debug suite, 28). After: phase 05 fields 35.5 to 37.6 ms, radar 29.9 ms, temperature now 313 ms (was 464). Wiring outside this phase: `run/src/climate/harmonic/` and one call in `run/src/climate/index.c`.

**27. Still missed, for Ahmed to decide.** `bench_atlas`, release, 16 workers: **temperature now 313 ms** against 60, and **a column's top 98 µs** against 20. Temperature now calls `climate_weather_at` every 32 pixels, which builds a day's weather lists for each call (Issues found 7); a column's top reads all 96 sections, 786 KB, so it is bound by memory unless air sections can be skipped without reading them, which the column job does not know. Neither is cheap to fix here, and phase 11's file forbids it there. In practice: temperature now is redrawn twice a second only while that one layer is up, on the workers, so main's frame stays at 0.72 ms; the column top is a small part of a column job. Ahmed: accept these two targets as missed, or name the plan that fixes them.

**28. The gate checker's runs (2026-11-07), on main merged at `17cb9d1c` plus the commits above.** Debug: the Verification's list plus `run_ui_atlas_game`, 13 of 13. Sanitize: `run_ui_atlas` and `run_ui_atlas_game` pass, no report. Tsan: both pass, no race. Release (`-O3 -flto`, `-Wframe-larger-than=16384`) links clean. `bench_atlas`: open over 100 km² median 16.2 ms (met), main's frame 0.72 ms (met), column write 0.08 µs (met), every layer but temperature now under 60 ms; missed: Issues found 27. `bench_climate --pictures <dir> 2198175988018515243 --layer all --zoom 6`: exit 0, 26 PNGs; `--zoom 8 --flat`: "13 of 13 climate fields the same bytes as climate_layer_draw". **The whole debug suite: 147 of 147 in 2241 s.** Worked examples through the real code (a scratch program on `draw/` and `tile/`): shading (70, 74, 72, 72) gives 1.0797, rising west 0.4000, flat 1.0000; 25 °C between (254, 224, 139) and (244, 109, 67) gives (249, 167, 103); the mip example gives (25, 26, 5) at height 70, and four ungenerated children stay ungenerated; marching squares (1010, 1013, 1015, 1011) at 1012 gives one segment (2.67, 0) to (1.00, 4.00): all as the Background works them. Scripted play, read by the agent: a played world through the menus (seed 7, `--menu --headless`, 640 by 480): the Atlas over hill-shaded explored land with grey round it, the player's mark, temperature on day 80 and `]` to day 81, normal wind's arrows from the east, currents, pressure now (uniform over 250 blocks; the readout says "the weather here is not worked out yet" until the climate tiles within 6 km are built, Issues found 7), climate class with its 11-row legend, Esc back to the world; `<world>/atlas/L0_*.tile` written. The map tool's `atlas_climate_class.png` and `atlas_currents.png` (classes over shaded land; red warm-current arrows) looked right. No stubs or TODOs in the phase's paths; every public `ui_atlas_*` function has a caller. `pgrep -af "$PWD/build"` printed nothing after the runs.
