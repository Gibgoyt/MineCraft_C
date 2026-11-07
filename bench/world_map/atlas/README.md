# bench/world_map/atlas

**Purpose:** plan 004 phase 09's map tool on `bench/world_map`'s side: a seed's real world as the Atlas shows it, every
layer, with no display and no card. The picture's land is made column by column with the game's own
`terrain_generate_box` and `ui_atlas_column_top`, written into an Atlas store as the sim writes it, and each layer is drawn
by `ui_atlas_draw_picture` over that store, the code the game draws with. `bench/climate --pictures ... --layer` draws the
same layers over the terrain's height estimate, which is quick and shows 16 km; this shows the real tops, so a few km.
A sub-directory, not a mode of `bench/world_map/index.c`, as that file is near the standard's line for splitting.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits; a `ui_t` and its Atlas with no directory (nothing on
disk but the pictures), a `terrain_t`, a `climate_t`, a job system of 16 workers.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map_atlas <directory>
[seed] [--layer <key|all>] [--zoom <0 to 5>] [--flat]`. The default seed is `TERRAIN_SEED_DEFAULT`, the default layer
`all`, the default zoom 4 (4 blocks a pixel, 2048 blocks a side). A seed is decimal, or hex with `0x`. A zoom past 5 would
need more level-0 tiles than the store keeps with their mips (a store with no directory forgets the least recently
used), and is refused. Exit status 0; 1 if `--flat` found a climate field that is
not `climate_layer_draw`'s bytes; 2 if it could not start or a picture could not be written.
**May be called by:** people.
**Depends on:** `terrain`, `climate`, `ui` and `ui/atlas`, `display/headless/png`, `job`, `thread`, `heap`, `time`,
`world/column`, `world/section`, and the generated `tables/blocks/`.

## What it writes

`<seed>_atlas_<key>.png` for each layer asked for: 512 by 512 round (0, 0), north up, normals layers on day 196, now
layers at t = 196.4 with census week 28, as `bench_climate`'s. Each line it prints gives the draw's time and how many
pixels are ungenerated, which is 0: everything shown was made.

**`--flat`** draws with hill shading off and the climate sampled at every pixel. At a whole number of blocks a pixel
(zoom 0, 2 or 4) each phase 03 to 05 climate field is then held to `climate_layer_draw`'s bytes at the same place, and
the last line says how many were the same.
