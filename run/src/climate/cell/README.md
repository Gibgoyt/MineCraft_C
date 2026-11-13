# run/src/climate/cell

**Purpose:** `climate_cell_t`, the 128 bytes one 64 by 64 block cell keeps (master plan 6.9); building one tile's 256 cells from its window; and the bilinear blend between four cells that makes regions blend.
**Tier:** Arena by depth. The tile build takes its working arrays from the arena it is given, the worker's Job arena: about 2.4 MiB of the 32.
**Owns:** nothing.
**Lifetime:** none of its own; what it takes from the arena dies with the job.
**Threads:** any. Both functions are pure: the same window gives the same bytes on any worker, as every sum adds in a fixed order.
**Public functions:**

- `climate_cell_build_tile`: the phase 03 file's job steps 2 to 7. Water is a centre at or below 0 that is not a dry basin, and deep water one at or below `CLIMATE_SEA_FLOOR_BLOCKS` (-30): the window's heights are the mask's, where a dry basin's centre stands at 1 at the least (`run/src/climate/mask/`, plan 007 phase 05), so the rule reads `h <= 0` and a dry basin is land. `climate_sea_classify` says which water is sea in this window alone. Two runs of `climate_sea_distance`: to the sea, and to land, where a small lake counts as land. The ground samples: 64 by 64 at `tile * 1024 + 8`, 16 apart. Then each cell, `iy` then `ix`: `lat_deg` from `climate_latitude_deg` at the centre; `z_ground` the 16 samples summed row by row in `float32_t`, over 16; the distances `min(8192, max(0, 64 sqrt(D) - 32))` blocks, the coast taken half a cell in from the marked centre, with the unit vector toward it; `kappa = 1 - det_exp(-d_s / 1500)` (`expf` before plan 006 phase 02). A sea cell has `flags = CLIMATE_CELL_SEA`, no sea distance and κ 0, and its land distance; a large lake both distances and `CLIMATE_CELL_LAKE`; a small lake `CLIMATE_CELL_LAKE_SMALL` (for the maps only) and is land for everything else. `hurricane_days` is `{255, 255}`; everything later phases fill is 0. The cells are zeroed first, so padding is always 0 and cells compare and hash as bytes.
- `climate_cell_build_tile` also hands its window out as a `climate_window_t` (heights, kinds, both transforms' distances and nearest cells, the tile), pointing into the Job arena for the rest of the job: what `air/` reads (plan 004 phase 04). No number changed when it did.
- `climate_cell_distance_blocks`, `climate_cell_kappa`: the distance and κ formulas every cell uses, for `air/`'s ring of neighbours.
- `climate_cell_blend`: four cells, south-west, south-east, north-west, north-east, blended along x and then y. The sea direction is blended and made unit again, or (0, 0) if it blends to nearly nothing. The flags are the nearest corner's. Latitude is not blended: the caller works it out from the exact y.

Constants (`CLIMATE_CELL_BLOCKS`, `CLIMATE_TILE_CELLS`, `CLIMATE_MARGIN_CELLS`, `CLIMATE_WINDOW_CELLS`, `CLIMATE_WARP_BLOCKS`, `CLIMATE_READ_REACH_BLOCKS`, `CLIMATE_GROUND_SAMPLES_SIDE`, `CLIMATE_SEA_DISTANCE_MAX`, `CLIMATE_KAPPA_SCALE`) and the ground function's type, `climate_ground_fn_t`, are in this part's header, where every other part finds them.

**May be called by:** its parent `run/src/climate/`, and `run/src/climate/mask/` for the constants.
**Depends on:** `run/src/climate/sea/` (sibling: the fill and the transform), `run/src/climate/harmonic/` (sibling: how many coefficients a stored wave has, plan 004 phase 11), its parent for `climate_latitude_deg`, `arena`, `types`, `math/det`, libm for `sqrt`.
