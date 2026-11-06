# run/src/climate/weather/flow

**Purpose:** flow accumulation by D8 and the flood index (master plan 7.5; the phase 07 file's worked grid), on cells the caller lists.

- **Receivers:** each cell sends its water to the lowest of its 8 neighbours that is kept and lower than itself, the first in the fixed order N, NE, E, SE, S, SW, W, NW on a tie. A sea cell, or a cell with no lower neighbour (a pit), keeps its water.
- **Accumulation:** cells sorted by height descending, then y, then x (a stable merge sort, no recursion), and each cell's total added into its receiver, highest first, so one pass is enough. Both this week's runoff (`q_week`) and the long-run means (`q_mean`) are accumulated over the same graph, in `float64_t`.
- **Flood index:** `F = Q_w / max(20, 3 Q̄)`: a channel holds three times its usual week, and a dry hillside needs 20 cell-mm before it floods. Over 1 is water over the banks.

The orders are fixed, so the same cells give the same bytes whoever calls.

**Tier:** Borrow by depth: allocates nothing; the caller gives the arrays.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any, one call at a time per array.
**Public functions:** `climate_flow_receivers`, `climate_flow_accumulate`, `climate_flow_flood`.
**May be called by:** its parent `run/src/climate/weather/` (the flow job over the kept cells), the tests (the worked grid).
**Depends on:** `types`.
