# run/src/game/report

**Purpose:** what `game` says about itself: what the HUD and the debug overlay show now, and the log's lines for the end of a run. Not in the plan's list: `game` reached its size for splitting with Stage 14.5, and these only read.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** `main`.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`):

- `game_hud_state_read`, `game_hud_debug_read`: what the HUD and the overlay show now, from what the other threads last said.
- `game_report`: what the frames took, and the books of what was dropped.

**May be called by:** `run/src/game/` and its callers.
**Depends on:** `game`'s header, `log`, `render/entities`, `tables/blocks`, `time`.
