# run/src/climate/mask

**Purpose:** the coarse land mask of plan 004 master plan 6.2: the ground's height at every cell centre, a page (256 heights, 1 KiB) per tile, kept in a sparse table; and a tile's window, its 17 by 17 pages, which is what the tile's flood fill and distance transforms run over.
**Tier:** Arena by depth. It allocates nothing: the table is in the Owner's region, and a window is lent by the caller from the worker's Job arena. A page is built through a 1 KiB stack array.
**Owns:** nothing.
**Lifetime:** none of its own.
**Threads:** any. A page is a pure function of the ground, so a private copy built while another thread holds the claim is the same bytes, and nothing here ever waits.
**Public functions:**

- `climate_mask_page_build`: one page from the ground function, at `origin = tile * 1024 + 32`, `step = 64`, 16 by 16. 256 estimates.
- `climate_mask_page_get`: a page from the table under the read protocol, or built into the table when this claims its slot (inside the mask keep square), or built privately when someone else holds the claim or it is outside the keep square.
- `climate_mask_window`: the 17 by 17 pages, `tile - 8` to `tile + 8`, in row-major order, into a 272 by 272 window, cell `(i, j)` at `j * 272 + i`, row 0 at the south. Optionally counts where each page came from, for tests and the bench.

The height is kept, not only land or sea, because phase 04 wants slopes from it and phase 07 wants water to run downhill.

**May be called by:** its parent `run/src/climate/`.
**Depends on:** `run/src/climate/cache/` (sibling: the slots and the protocol) and `run/src/climate/cell/` (sibling: the constants and `climate_ground_fn_t`), `types`.
