# run/src/ui/atlas

**Purpose:** the Atlas on `M` (plan 004 phase 09, master plan 9.3): a full-screen map of every column the world has
generated, ungenerated land grey, with 26 layers chosen by the number keys, while the game keeps running. This part holds
what the Atlas knows of the ground (the store: 160 tiles of 512 by 512 pixels in memory, each pixel a column top's RGB565
colour and height, at 8 levels each half the last), the store's lock and its files under `<world>/atlas/`, the view and
its keys, the drawing and saving jobs, and each frame's fixed parts: the picture, title, legend, day slider, readout and
the player's mark.
**Tier:** Arena (depth 2). Its memory is the region `ui_t` reserves with its own (`run/src/ui/index.c`), handed over at
`ui_atlas_store_start`; it carves it into the slots' pixels, two pictures and their grey masks, the field the overlay's
isobars read, and three arenas (drawing, saving, and bands drawn on main with no job system). Nothing here touches the heap.
**Owns:** nothing; it borrows the region.
**Lifetime:** a world's: `ui_atlas_store_start` as it opens, `ui_atlas_store_stop` as it closes (which writes every dirty
tile). The view is remembered until the world closes.

**Threads:**

- The **sim** writes a finished column's 256 pixels (`ui_atlas_column_write`, about 2 µs under the lock) and submits the
  saving job every 30 s of world time (`ui_atlas_save_step`).
- The **column job's worker** works out the column's top (`ui_atlas_column_top`, in `tile/`).
- **Main** owns the view, the input and the frame; it never writes tiles. The readout is worked out on main when the
  pointer's block, the layer or the day changes: normals for the day, or on a now layer `climate_weather_at` (about
  0.2 ms) every half second, borrowing the band arena, which only main uses.
- The **drawing job** builds the weather systems' day list for a now layer, makes stale mips the zoom reads, then spawns
  16 band jobs; the last band to finish runs the overlay and stores `draw_done` with release. Main sees it with acquire and
  swaps the pictures. If `job_spawn` finds the queues full, the drawing job leaves the bands not yet queued to main
  (`bands_owed`), whose draw loop queues them on the next frames (`ui_atlas_frame`, or at once in `ui_atlas_draw_wait`);
  no band is ever run on the drawing job's stack. The **saving job** writes dirty tiles with `file_write_atomic`, then makes and writes every stale mip.
- Every slot field and pixel is under the store's lock, a ticket lock (first come, first served, yielding while it waits), so 16 band jobs reading cannot starve the sim's writes: a pthread mutex let a tight reader hold a writer off for 1.5 s in the lock test. Files are read and encoded outside it; a tile file is
  copied in or out under it. The longest wait of a column write is kept in `wait_ns_most`.

**Files:** one per tile, `L<level>_<tile_x>_<tile_y>.tile` (see `tile/`). A world with no `atlas/` directory opens fine and
the Atlas fills as columns load, so no save version changes. A damaged or foreign file is ignored, said once, never
deleted. A tile made before its file was read (`merge`) keeps what it has and takes the rest from the file.

**Mips:** a level-0 write puts its seven ancestors in the stale list; the drawing job (up to the level it reads) and the
saving job (all) make them again, bottom up, each from its four children in memory or on disk.

**Redraw:** a picture is drawn only when the view main wants differs from the one the front picture was drawn from: the
layer, zoom, centre, size, the day on a normals layer, the half second of world time on a now layer, the census week on a
week layer, and the store's revision sampled at most every 500 ms. While a picture is drawn the old one is shown, shifted
by the pan, the uncovered edge dark.

**Keys while up:** `M` and `Esc` close it; the wheel zooms by √2 about the pointer; a left drag pans; `1` to `9` choose a
group's first layer, again for the next in it; `[` and `]` move the day (with Shift by 30) on normals layers; `P` centres on
the player; the day slider along the bottom. `game_screen_up` counts the Atlas, so Esc reaches it, not the pause screen.

**Public functions:** `ui_atlas_store_start`, `ui_atlas_store_stop`, `ui_atlas_column_write`, `ui_atlas_store_read`,
`ui_atlas_save_step`, `ui_atlas_save_now`, `ui_atlas_mips_make`, `ui_atlas_input`, `ui_atlas_up`, `ui_atlas_hide`,
`ui_atlas_frame`, `ui_atlas_frame_draw`, `ui_atlas_draw_wait`, `ui_atlas_readout`, `ui_atlas_layer_scale`.

**May be called by:** `run/src/ui/` (start and stop), `run/src/game/` and `run/src/game/atlas/`, tests, `bench/atlas`.
**Depends on:** `ui/atlas/tile`, `ui/atlas/layer`, `ui/atlas/draw`, `ui/compose`, `ui/font`, `ui/widget`, `climate`
(`climate_normals_at`, `climate_air_at`, `climate_cell_get`, `climate_latitude_deg`, the week store), `climate/layer`
(scales), `file`, `job`, `log`, `time`, `sched_yield`.
