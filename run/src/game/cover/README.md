# run/src/game/cover

**Purpose:** plan 004 phase 08's snow and ice on loaded ground, from the census's week. The only part of plan 004 that changes blocks: every change is `world_edit_block_set`, so the card redraws it, the save keeps it in its deltas, light looks again, and every reader of the edit log sees it, as for anything a player does.

**The rule for one block column** (`game_cover_column`), idempotent:

1. The week's `snow_mm` and `ice_cm` there: bilinear between the centres of the four cells round the point moved by phase 03's warp (`game_cover_week_of_climate`), so the edge wobbles and does not follow the 64-block grid. A cell of a tile never through a census (fresh) is no week at all; with none near, nothing changes.
2. **Snow off** at or under 10 mm: every `snow_layer` in the column goes back to air. Only this lays them (the block has no item), so any found is its own. Sections whose palette has no `snow_layer` are not looked at.
3. The top: the highest voxel that is not air. A `snow_layer` there stays (it is between the lines, or over them).
4. **Ice on** over 5 cm (30 cm on a sea cell, Risks R5): the top is a `water` source. **Ice off** at or under 2 cm: the top is `ice` with `water` under it. Ice a player covered is not the top and is left (R6).
5. **Snow on** over 20 mm: the top is a whole solid cube, not leaves, `snow_block`, `ice`, `packed_ice`, `blue_ice` or `lava`, and the voxel over it is in the world. A roof keeps new snow off what is under it.

Between the on and off lines nothing changes, so the edge of the snow does not flicker week to week (R4).

**When:** after each census (`game_cover_step`, from `game_sim_step`, paused or not): every column within the view, nearest the map's middle first, `GAME_COVER_COLUMNS_PER_STEP` (64) a step, while the census screen holds Continue and shows the columns on its bar (`game/session`). And each column as it streams in (`game_cover_stream_in`, after its commit), so land first loaded in winter is white, and land saved in winter and loaded in summer is not. It never loads a column to edit it. While a census writes the week, neither runs.

**Where it lives:** `game_cover_t` is in the game's World arena beside the column jobs (`game_cover_of`, in `game/index.c`), as `game_t` is at its size.

**Tier:** Arena by depth; it allocates nothing.
**Owns:** nothing.
**Lifetime:** World.
**Threads:** the sim, holding the map. `game_cover_busy` is main's too: its numbers are relaxed atomics, and only a progress bar depends on them.
**Public functions:** `game_cover_column`, `game_cover_week_of_climate`, `game_cover_step`, `game_cover_stream_in`, `game_cover_busy`; `game_cover_of` is `game`'s.
**May be called by:** its parent `run/src/game/`, `game/session` (Continue waits for it), the tests.
**Depends on:** `game`'s header, `game/census` (when a census has finished), `climate` and `climate/weather` (the week store, the warp), `world` (the map, the edits, the fluid levels), the generated block table, `log`, `time`.
