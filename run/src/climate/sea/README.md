# run/src/climate/sea

**Purpose:** which water is sea, and how far every cell is from it: the flood fill and the distance transform of plan 004 master plan 6.2, on a square grid the caller gives. It knows nothing of tiles, terrain or the cache, so tests feed it synthetic grids.
**Tier:** Arena by depth. It allocates nothing: every array is lent by the caller, from the worker's Job arena in the game.
**Owns:** nothing.
**Lifetime:** none; each call stands alone.
**Threads:** any. Both functions are pure.
**Public functions:**

- `climate_sea_classify`: labels the 4-connected water components (north, south, east, west; never a corner, so water does not leak where two land cells meet), numbered 1, 2, ... in row-major order of their first cell, by a breadth-first fill from an explicit queue (recursion is not allowed). A component is **sea** if it touches the grid's edge or has at least `CLIMATE_SEA_AREA_MIN_CELLS` (8192, 33.6 km²) cells; otherwise a **large lake** from `CLIMATE_LAKE_LARGE_CELLS` (489, 2.003 km²), or a **small lake**, which every climate purpose treats as land. Returns the component count.
- `climate_sea_distance`: the exact squared distance in cells² to the nearest marked cell, and that cell's index, by Felzenszwalb and Huttenlocher's lower envelope of parabolas: a sweep along every row, then one along every column over the rows' answers. Squared distances are whole numbers, exact in `uint32_t`; only the crossings are `float64_t`. Ties go to the lower index in each sweep. `CLIMATE_SEA_FAR` where nothing is marked.

**Working memory** for `climate_sea_distance` on a grid `side` across, in `climate_sea_scratch_t`: `words`, `5 * side + 1` `uint32_t` (the sweep's input, its output, its arg-min, the envelope's parabolas and a copy of one column of the row pass's nearest x), and `crossings`, `side + 1` `float64_t`.

**May be called by:** its parent `run/src/climate/`, and `run/src/climate/cell/`, which runs both for each tile's 272 by 272 window.
**Depends on:** `types`.

Why the window rule makes sense for an infinite world, and the worked examples, are in `plans/004-ahmed-bohemoth-climate-system-phase-03-height-estimate-and-climate-cells.md`.
