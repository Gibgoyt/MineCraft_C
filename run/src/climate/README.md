# run/src/climate

**Purpose:** the climate of plan 004, worked out strictly top to bottom (master plan section 3). Latitude from y alone (master plan 5.1, phase 02); its part `sun/` gives where the sun is. From phase 03, the **climate cells**: every 64 by 64 block cell's latitude, mean ground height, whether it is sea, a large lake or land, how far and which way the nearest connected sea is, how far and which way the nearest land is from a sea cell, and its continentality κ. Cells are worked out a tile (16 by 16 cells, 1024 by 1024 blocks) at a time by background jobs, nearest the player first, kept in a cache, never saved: they come back from the seed. Later phases add currents, belts, wind, temperature and rain to the same cells.
**Tier:** Owner. It reserves one `heap_region_t`, `CLIMATE_REGION_BYTES` (146 MiB of address space, only touched pages cost memory): the state at its start, the warp bank's fields, the two tables' slots, the tiles' cells (128 MiB) and the mask pages (16 MiB). Nothing is allocated after `climate_init`.
**Owns:** that region, through `climate_t.owned_region`; `climate_t` is 32 bytes, in `app_t`, because main's frame holds `app_t` and sits at the 16 KiB limit, so everything else, `climate_state_t` included, is in the region.
**Lifetime:** Process for the region (`climate_init`, `climate_deinit` in `app_init`, `app_deinit`). World for what is in it: `climate_open` binds a terrain, seeds the warp and empties both tables; `climate_close` waits for its jobs and forgets the world.
**Threads:** latitude, `climate_cell_get`, `climate_sample_at`, `climate_area_ready` and `climate_warp_at` from any thread. `climate_schedule` from the sim, once a step. `climate_prepare_area`, `climate_prepare_tile` and `climate_close` from any thread but a worker. `climate_init`, `climate_deinit`, `climate_open` and `climate_open_ground` with nothing else touching it. Tile and page jobs run on the workers; a job never waits.
**Public functions:**

- `climate_latitude_deg`: `phi(y) = (180 / pi) asin(sin(pi y / 2Y))`, in degrees, positive north, with `Y = CLIMATE_POLE_Y = 10000 * 90 / 23.5 = 38297.87` blocks. A triangle wave: 0 at y = 0, 23.5 at the tropic (10,000), 66.5 at the polar circle (28,298), 90 at the pole (38,298), 0 again at 76,596, and -90 at -38,298. y is first brought into one period, `CLIMATE_LATITUDE_PERIOD = 4Y`, so a y out at the world's edge keeps its digits. x plays no part.
- `climate_init`, `climate_deinit`: the region, a pair.
- `climate_open`, `climate_open_ground`, `climate_close`, `climate_is_open`: a world. `climate_open` takes the terrain, and its ground is `terrain_height_estimate_area`; `climate_open_ground` takes any pure ground function, for tests and the bench, so real and synthetic worlds go down one code path.
- `climate_schedule`: the sim's, each step. Moves the keep square to the player and queues up to `CLIMATE_JOBS_IN_FLIGHT_MAX` (4) tiles not yet built within `radius_blocks` plus the read reach (96), nearest the player's block to the tile's centre first, ties by y then x. A full shared queue undoes the claim, for next step. Never waits.
- `climate_area_ready`: whether every tile a read anywhere in a block rectangle touches is built: the gate that holds a column back until its tiles are there.
- `climate_prepare_area`: builds all of a rectangle's tiles on all workers and waits: mask pages first, a job each, so no two jobs build one page, then the tiles. Tells a progress callback as jobs finish. A new world calls it over spawn ± (render distance × 16 + 2048) before play.
- `climate_prepare_tile`: one tile in one job, its window's missing pages built inside it: what a tile costs, for the bench and the tests.
- `climate_cell_get`: the cell holding a block, copied out under the read protocol.
- `climate_sample_at`: the warped bilinear read. The point is moved by two warp fields, `"climate_cells/warp_x"` and `"climate_cells/warp_y"` (2D, 1/256 cycles a block, 2 octaves, spread at 0.35), up to 32 blocks each way; then the four cells round it are blended. Latitude is never blended: it comes from the exact y, so a read at the pole says 90.
- `climate_warp_at`, `climate_warp_set`: where a read is taken from, and a test's hook to turn the warp off.

**A tile job**, in order: its window from 17 by 17 mask pages (read, or claimed and built, or built privately), then `climate_cell_build_tile` (`cell/`), then publish. Each tile is built by exactly one job, in a fixed order, so the bytes are the same on 1 worker or 16.

**The sea rule in an infinite world:** each tile decides inside its own window, the tile plus 8 km each way (17 by 17 tiles). Water touching the window's edge, or over 33.6 km² inside it, is sea; enclosed water over 2 km² is a large lake; smaller lakes are land for every climate purpose. A tile's cells depend only on its window, so on nothing else that has been worked out. Distances are capped at 8192 blocks, which the 8 km margin covers.

**May be called by:** `main`, which holds the `climate_t` and starts and stops it; `game`, which borrows it, schedules tiles, gates columns and prepares a new world's spawn; `mob` and `player` for latitude (`compile/src/standards_lint/rules_include/index.c` ranks it 8, under `render`); later climate parts. `render` is never given it: it is handed a sun direction as three floats.
**Depends on:** its parts `sea/`, `cache/`, `mask/`, `cell/`, `layer/` and `sun/`; `terrain` (the height estimate and the noise bank, nothing else), `job`, `heap`, `arena`, `time`, `log`, `assert`, `types`, glibc's libm.

Positive y is north and positive x is east (Ahmed, 2026-11-04). The period is 153,191.49 blocks, not a whole number, so the tropic stays at exactly 10,000: shifting y by the whole 153,191 moves latitude by about 0.001 degrees (phase 02 file, Issues found 2).

Everything about the cells, with worked examples, is in `plans/004-ahmed-bohemoth-climate-system-phase-03-height-estimate-and-climate-cells.md`.
